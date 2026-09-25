/*
 * stub_values.h — shared constants used by both the host stubs and the
 * VFS test program, so assertions can pin exact expected values.
 */
#ifndef STUB_VALUES_H
#define STUB_VALUES_H

/* FAT32 stub capacity */
#define STUB_FAT32_TOTAL      0x40000000ULL   /* 1 GiB  */
#define STUB_FAT32_FREE       0x08000000ULL   /* 128 MiB */
#define STUB_FAT32_CLUSTER    4096U

/* FAT16 stub capacity */
#define STUB_FAT16_TOTAL      0x02000000ULL  /* 32 MiB */
#define STUB_FAT16_FREE       0x01000000ULL   /* 16 MiB */
#define STUB_FAT16_CLUSTER    512U

/* NTFS stub (ntfs_info_t) */
#define STUB_NT2_BYTES_PER_SECTOR   512U
#define STUB_NT2_TOTAL_SECTORS      8192ULL            /* total = 4 MiB */
#define STUB_NT2_CLUSTER            4096U

/* ISO9660 stub */
#define STUB_ISO_BLOCK_SIZE 2048U
#define STUB_ISO_BLOCKS     100U

/* EXTFS stub */
#define STUB_EXT_BLOCK_SIZE  1024U
#define STUB_EXT_BLOCKS      500U
#define STUB_EXT_FREE       100U

#endif
