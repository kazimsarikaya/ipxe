/*
 * Copyright (C) 2007 Michael Brown <mbrown@fensystems.co.uk>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * You can also choose to distribute this program under the terms of
 * the Unmodified Binary Distribution Licence (as given in the file
 * COPYING.UBDL), provided that you have satisfied its requirements.
 */

FILE_LICENCE ( GPL2_OR_LATER_OR_UBDL );

/**
 * @file
 *
 * hobbyos image format
 *
 */

 #include <pxe.h>
 #include <pxe_call.h>
 #include <pic8259.h>
 #include <ipxe/uaccess.h>
 #include <ipxe/image.h>
 #include <ipxe/segment.h>
 #include <ipxe/netdevice.h>
 #include <ipxe/features.h>
 #include <ipxe/console.h>
 #include <ipxe/efi/efi.h>
 #include <ipxe/efi/IndustryStandard/PeImage.h>

FEATURE ( FEATURE_IMAGE, "HOBBYOS", DHCP_EB_FEATURE_HOBBYOS, 1 );


typedef struct disk_slot {
	uint8_t type;
	uint8_t unused[7];
	uint64_t start;
	uint64_t end;
} __attribute__ ((packed)) disk_slot_t;

typedef struct disk_slot_table {
	disk_slot_t slots[10];
} __attribute__ ((packed)) disk_slot_table_t;

#define HOBBYOS_SLOT_TABLE_OFFSET 0x310
#define HOBBYOS_SLOT_TABLE_PXE_STAGE4 0x85

static int hobbyos_load_initrds( struct image* image, disk_slot_table_t* slot_table ){
	struct image* initrd;
	int slot = 4;

	DBGC(image, "HOBBYOS %p loading initrds to %p", image, slot_table);

	for_each_image ( initrd ) {

		if ( initrd == image )
			continue;

		DBGC ( image, "HOBBYOS %p initrd %p phy start: %#08lx end: %#08lx])\n",
		       image, initrd,
		       user_to_phys ( initrd->data, 0 ), user_to_phys ( initrd->data,  initrd->len));

		slot_table->slots[slot].type = HOBBYOS_SLOT_TABLE_PXE_STAGE4;
		slot_table->slots[slot].start = user_to_phys ( initrd->data, 0 );
		slot_table->slots[slot].end = user_to_phys ( initrd->data,  initrd->len);

		slot++;
	}

	return 0;
}

/**
 * Execute hobbyos image
 *
 * @v image		hobbyos image
 * @ret rc		Return status code
 */
static int hobbyos_exec ( struct image* image ) {
	userptr_t buffer = real_to_user ( 0, 0x7c00 );
	int rc = 0;
	disk_slot_table_t* slot_table;

	slot_table = (disk_slot_table_t*)(image->data + HOBBYOS_SLOT_TABLE_OFFSET);

	DBGC(image, "IMAGE %p try to fill slot table %p\n", image, slot_table);

	if((rc = hobbyos_load_initrds(image, slot_table)) != 0) {
		DBGC(image, "IMAGE %p could not load initrds %s\n", image, strerror(rc));
		return rc;
	}

	/* Verify and prepare segment */
	if ( ( rc = prep_segment ( buffer, image->len, image->len ) ) != 0 ) {
		DBGC ( image, "IMAGE %p could not prepare segment: %s\n",
		       image, strerror ( rc ) );
		return rc;
	}

	/* Copy image to segment */
	memcpy_user ( buffer, 0, image->data, 0, image->len );

	/* Reset console since hobbyos NBP will probably use it */
	console_reset();

	/* Start hobbyos NBP */
	rc = pxe_start_nbp();

	return rc;
}

/**
 * Probe hobbyos image
 *
 * @v image		hobbyos file
 * @ret rc		Return status code
 */
int hobbyos_probe ( struct image* image ) {
	uint32_t signature;

	/* Images too large to fit in base memory cannot be hobbyos
	 * images.  We include this check to help prevent unrecognised
	 * images from being marked as hobbyos images, since hobbyos images
	 * have no signature we can check against.
	 */
	if ( image->len > ( 0xa0000 - 0x7c00 ) )
		return -ENOEXEC;

	/* Rejecting zero-length images is also useful, since these
	 * end up looking to the user like bugs in ihobbyos.
	 */
	if ( !image->len )
		return -ENOEXEC;

	copy_from_user ( &signature, image->data, 0x202, sizeof (signature) );

	if(signature != 0x45585021) {
		return -ENOEXEC;
	}

	return 0;
}

/** hobbyos image type */
struct image_type hobbyos_image_type[] __image_type ( PROBE_HOBBYOS ) = {
	{
		.name = "HOBBYOS",
		.probe = hobbyos_probe,
		.exec = hobbyos_exec,
	},
};
