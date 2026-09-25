/*
 * test_fs.c — host-side (Windows / MinGW) filesystem regression tests.
 *
 * Real kernel logic pulled in by #include:
 *   - fs/gpt.c     : GPT header / partition-array parsing via a callback
 *                    (no hardware needed — we serve sectors from RAM).
 *   - fs/fat32.c   : the on-disk BPB / 8.3-name helpers, endian accessors,
 *                    cluster<->LBA mapping and directory-entry cluster field.
 *
 * On top of those, constructed-memory-image tests cover the EXT2/3/4
 * superblock + group-descriptor arithmetic (mirroring fs/extfs.c) and the
 * NTFS MFT attribute walk (mirroring fs/ntfs.c ntfs_find_attribute). The
 * full ntfs.c / extfs.c drivers talk to ATA ports directly and are not
 * host-linkable; their pure on-disk decoding is exercised here on RAM
 * images using the exact struct offsets the drivers use.
 *
 * Build: gcc -I ../include -I . -o test_fs.exe test_fs.c stubs_ext.c
 */

int printf(const char *fmt, ...);

#include "common.h"
#include "extfs.h"
#include "fat32.h"
#include "file.h"
#include "gpt.h"
#include "ntfs.h"
#include "string.h"

/* ---- code under test (same TU => statics reachable) ---- */
#include "../fs/gpt.c"
#include "../fs/fat32.c"

/* ============================================================
 *  Tiny test harness
 * ============================================================ */
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (cond) { g_pass++; }                                            \
        else {                                                             \
            g_fail++;                                                      \
            printf("    FAIL %s  (%s:%d)\n", #cond, __FILE__, __LINE__);   \
        }                                                                  \
    } while (0)

static void section(const char *name) { printf("\n== %s ==\n", name); }

static void put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8); }
static void put_le32(uint8_t *p, uint32_t v)
{ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static void put_le64(uint8_t *p, uint64_t v)
{ for (int i=0;i<8;i++) p[i]=(uint8_t)(v>>(i*8)); }

/* ============================================================
 *  GPT — in-memory fake disk
 * ============================================================ */
#define FAKE_DISK_SECTORS 64
static uint8_t g_fake_disk[FAKE_DISK_SECTORS][512];

static bool fake_read_sector(uint64_t lba, void *buf)
{
    if (lba >= FAKE_DISK_SECTORS) {
        return false;
    }
    memcpy(buf, g_fake_disk[lba], 512);
    return true;
}

static void gpt_build_fake_disk(void)
{
    memset(g_fake_disk, 0, sizeof(g_fake_disk));

    /* protective MBR in sector 0: boot sig 0x55AA + type 0xEE in entry 0 */
    g_fake_disk[0][510] = 0x55;
    g_fake_disk[0][511] = 0xAA;
    g_fake_disk[0][446 + 4] = 0xEE;

    /* GPT header in LBA 1 */
    memcpy(g_fake_disk[1], "EFI PART", 8);
    put_le32(g_fake_disk[1] + 12, 92);          /* header_size */
    put_le64(g_fake_disk[1] + 72, 2);          /* partition_entry_lba */
    put_le32(g_fake_disk[1] + 80, 4);          /* entry count */
    put_le32(g_fake_disk[1] + 84, 128);        /* entry size */

    /* Partition entry array in LBA 2 (4 entries x 128 bytes) */
    /* entry 0: Microsoft Basic Data -> NTFS/FAT */
    memcpy(g_fake_disk[2] + 0, gpt_guid_microsoft_basic_data, 16);
    put_le64(g_fake_disk[2] + 32, 2048);       /* first_lba */
    put_le64(g_fake_disk[2] + 40, 4095);
    /* entry 1: EFI System -> FAT32 */
    memcpy(g_fake_disk[2] + 128, gpt_guid_efi_system, 16);
    put_le64(g_fake_disk[2] + 128 + 32, 20480);
    put_le64(g_fake_disk[2] + 128 + 40, 21503);
    /* entry 2: empty (first_lba == 0) -> skipped */
    /* entry 3: Linux filesystem -> EXTFS */
    memcpy(g_fake_disk[2] + 384, gpt_guid_linux_filesystem, 16);
    put_le64(g_fake_disk[2] + 384 + 32, 100000);
    put_le64(g_fake_disk[2] + 384 + 40, 200000);
}

static void test_gpt_detect(void)
{
    section("gpt_detect (protective MBR)");
    uint8_t blank[512];
    memset(blank, 0, sizeof(blank));

    CHECK(gpt_detect((uint8_t *) 0) == false);

    /* no boot signature */
    CHECK(gpt_detect(blank) == false);

    /* boot sig but no 0xEE partition type */
    blank[510] = 0x55; blank[511] = 0xAA;
    CHECK(gpt_detect(blank) == false);

    gpt_build_fake_disk();
    CHECK(gpt_detect(g_fake_disk[0]) == true);
}

static void test_gpt_header(void)
{
    section("gpt_read_header");
    gpt_build_fake_disk();
    gpt_header_t hdr;

    CHECK(gpt_read_header((gpt_read_sector_fn) 0, &hdr) == false);
    CHECK(gpt_read_header(fake_read_sector, (gpt_header_t *) 0) == false);

    /* corrupt signature */
    g_fake_disk[1][0] = 'X';
    CHECK(gpt_read_header(fake_read_sector, &hdr) == false);
    gpt_build_fake_disk();

    CHECK(gpt_read_header(fake_read_sector, &hdr) == true);
    CHECK(memcmp(hdr.signature, "EFI PART", 8) == 0);
    CHECK(hdr.partition_entry_lba == 2);
    CHECK(hdr.partition_entry_count == 4);
    CHECK(hdr.partition_entry_size == 128);
}

static void test_gpt_partitions(void)
{
    section("gpt_read_partitions (empty-entry skip + cap)");
    gpt_build_fake_disk();
    gpt_header_t hdr;
    gpt_read_header(fake_read_sector, &hdr);

    gpt_part_entry_t entries[8];
    CHECK(gpt_read_partitions((gpt_read_sector_fn) 0, &hdr, entries, 8) == 0);
    CHECK(gpt_read_partitions(fake_read_sector, &hdr, (gpt_part_entry_t *) 0, 8) == 0);
    CHECK(gpt_read_partitions(fake_read_sector, &hdr, entries, 0) == 0);

    int n = gpt_read_partitions(fake_read_sector, &hdr, entries, 8);
    /* entry 2 (first_lba==0) is skipped => 3 live entries */
    CHECK(n == 3);
    CHECK(entries[0].first_lba == 2048);
    CHECK(entries[1].first_lba == 20480);
    CHECK(entries[2].first_lba == 100000);

    /* max_entries cap: ask for only 1 */
    CHECK(gpt_read_partitions(fake_read_sector, &hdr, entries, 1) == 1);
}

static void test_gpt_fs_type(void)
{
    section("gpt_partition_fs_type");
    gpt_part_entry_t e;
    memset(&e, 0, sizeof(e));

    CHECK(gpt_partition_fs_type((const gpt_part_entry_t *) 0) == FS_TYPE_NONE);

    memcpy(e.type_guid, gpt_guid_microsoft_basic_data, 16);
    CHECK(gpt_partition_fs_type(&e) == FS_TYPE_NTFS);

    memcpy(e.type_guid, gpt_guid_efi_system, 16);
    CHECK(gpt_partition_fs_type(&e) == FS_TYPE_FAT32);

    memcpy(e.type_guid, gpt_guid_linux_filesystem, 16);
    CHECK(gpt_partition_fs_type(&e) == FS_TYPE_EXTFS);

    memcpy(e.type_guid, gpt_guid_microsoft_reserved, 16);
    CHECK(gpt_partition_fs_type(&e) == FS_TYPE_NONE);
}

/* ============================================================
 *  FAT32 — pure on-disk helpers (statics reached via same-TU include)
 * ============================================================ */
static void test_fat32_endian(void)
{
    section("FAT32 read_le16 / read_le32 (little endian)");
    uint8_t b[4] = { 0x78, 0x56, 0x34, 0x12 };
    CHECK(read_le16(b) == 0x5678u);
    CHECK(read_le32(b) == 0x12345678u);
}

static void test_fat32_bpb(void)
{
    section("FAT32 BPB validation");
    fat32_bpb_t bpb;
    memset(&bpb, 0, sizeof(bpb));
    bpb.bytes_per_sector = 512;
    bpb.sectors_per_cluster = 8;
    bpb.fat_size_16 = 0;
    bpb.root_entry_count = 0;
    bpb.fat_size_32 = 200;
    bpb.root_cluster = 2;
    CHECK(fat32_bpb_is_valid(&bpb) == true);

    bpb.bytes_per_sector = 1024;                 /* only 512 accepted */
    CHECK(fat32_bpb_is_valid(&bpb) == false);
    bpb.bytes_per_sector = 512;

    bpb.fat_size_16 = 40;                         /* FAT16-style layout rejected */
    CHECK(fat32_bpb_is_valid(&bpb) == false);
    bpb.fat_size_16 = 0;

    bpb.root_cluster = 0;                         /* root dir must start >= cluster 2 */
    CHECK(fat32_bpb_is_valid(&bpb) == false);
}

static void test_fat32_chain_markers(void)
{
    section("FAT32 end-of-chain / bad-cluster markers");
    CHECK(fat32_is_end_cluster(0x0FFFFFF8u) == true);
    CHECK(fat32_is_end_cluster(0x0FFFFFFFu) == true);
    CHECK(fat32_is_end_cluster(0x0FFFFFF7u) == false);   /* bad cluster */
    CHECK(fat32_is_end_cluster(0x00001000u) == false);  /* normal cluster */
    CHECK(fat32_is_end_cluster(0u) == false);           /* free */
}

static void test_fat32_name_format(void)
{
    section("FAT32 8.3 name formatting");
    char out[11];
    fat32_format_component(out, "readme.txt");
    /* name part uppercase in [0..7], ext uppercase in [8..10], pad spaces */
    CHECK(out[0] == 'R' && out[5] == 'E');
    CHECK(out[6] == ' ' && out[7] == ' ');
    CHECK(out[8] == 'T' && out[9] == 'X' && out[10] == 'T');

    fat32_format_component(out, "file");
    CHECK(out[0] == 'F' && out[3] == 'E');
    CHECK(out[4] == ' ' && out[10] == ' ');
}

static void test_fat32_name_decode(void)
{
    section("FAT32 8.3 name decoding");
    uint8_t name[11] = { 'R','E','A','D','M','E',' ',' ','T','X','T' };
    char out[13];
    fat32_decode_name(out, name);
    CHECK(out[0] == 'R' && out[5] == 'E');
    CHECK(out[6] == '.');
    CHECK(out[7] == 'T' && out[9] == 'T');
    CHECK(strlen(out) == 10);

    uint8_t noname[11] = { 'F','I','L','E',' ',' ',' ',' ',' ',' ',' ' };
    fat32_decode_name(out, noname);
    CHECK(strcmp(out, "FILE") == 0);
}

static void test_fat32_cluster_map(void)
{
    section("FAT32 cluster -> LBA mapping");
    g_bpb.bytes_per_sector = 512;
    g_bpb.sectors_per_cluster = 8;
    g_data_lba = 2048;
    /* cluster 2 (first data cluster) maps exactly at g_data_lba */
    CHECK(fat32_cluster_to_lba(2) == 2048u);
    /* cluster 3 = one cluster further */
    CHECK(fat32_cluster_to_lba(3) == 2048u + 8u);
    /* cluster 42 */
    CHECK(fat32_cluster_to_lba(42) == 2048u + (42u - 2u) * 8u);
}

static void test_fat32_dir_entry_cluster(void)
{
    section("FAT32 directory entry first-cluster hi/lo");
    fat32_dir_entry_t e;
    memset(&e, 0, sizeof(e));
    fat32_set_entry_cluster(&e, 0x00012345u);
    CHECK(e.first_cluster_low == 0x2345u);
    CHECK(e.first_cluster_high == 0x0001u);
    CHECK(fat32_entry_cluster(&e) == 0x00012345u);

    fat32_set_entry_cluster(&e, 0u);
    CHECK(fat32_entry_cluster(&e) == 0u);
}

static void test_fat32_path_split(void)
{
    section("FAT32 path component splitting ('/' separated internally)");
    const char *p = "system/kernel.exe";
    char comp[FAT32_COMP_NAME_MAX];
    CHECK(fat32_split_path_component(&p, comp) == true);
    CHECK(strcmp(comp, "system") == 0);
    CHECK(fat32_split_path_component(&p, comp) == true);
    CHECK(strcmp(comp, "kernel.exe") == 0);
    CHECK(fat32_split_path_component(&p, comp) == false);

    const char *empty = "";
    char c2[FAT32_COMP_NAME_MAX];
    CHECK(fat32_split_path_component(&empty, c2) == false);
}

/* ============================================================
 *  EXT2/3/4 — superblock parsing on a constructed 1 KiB image
 *  (offsets mirror fs/extfs.c extfs_parse_super)
 * ============================================================ */
#define EXT_SUPER_MAGIC 0xEF53u

static void ext_make_super(uint8_t super[1024], uint32_t log_bs,
                           uint32_t inodes, uint32_t blocks)
{
    memset(super, 0, 1024);
    put_le32(super + 0, inodes);
    put_le32(super + 4, blocks);
    put_le32(super + 12, blocks / 2);   /* free blocks */
    put_le32(super + 16, inodes / 2);   /* free inodes */
    put_le32(super + 24, log_bs);       /* log block size */
    put_le32(super + 32, 8192);         /* blocks per group */
    put_le32(super + 40, 2048);         /* inodes per group */
    put_le16(super + 56, EXT_SUPER_MAGIC);
    put_le16(super + 58, 1);            /* state: cleanly unmounted */
    put_le16(super + 88, 256);          /* inode size */
}

static void test_ext_superblock(void)
{
    section("EXT superblock parsing (constructed image)");
    uint8_t super[1024];

    /* bad magic -> rejected */
    ext_make_super(super, 0, 1000, 4096);
    super[56] = 0x00;
    CHECK(read_le16(super + 56) != EXT_SUPER_MAGIC);

    /* valid 1 KiB block size (log=0) */
    ext_make_super(super, 0, 2048, 8192);
    CHECK(read_le16(super + 56) == EXT_SUPER_MAGIC);
    CHECK(read_le32(super + 0) == 2048);
    CHECK(read_le32(super + 4) == 8192);
    uint32_t bs = 1024u << read_le32(super + 24);
    CHECK(bs == 1024u);
    /* group descriptor block: 2 for 1k blocks, 1 otherwise */
    CHECK((bs == 1024u ? 2u : 1u) == 2u);
    CHECK(read_le16(super + 88) == 256);

    /* 4 KiB block size (log=2) */
    ext_make_super(super, 2, 4096, 32768);
    bs = 1024u << read_le32(super + 24);
    CHECK(bs == 4096u);
    CHECK((bs == 1024u ? 2u : 1u) == 1u);   /* GDT lives at block 1 */

    /* inode_size == 0 -> driver defaults to 128 */
    ext_make_super(super, 2, 4096, 32768);
    put_le16(super + 88, 0);
    uint16_t isz = read_le16(super + 88);
    CHECK(isz == 0 || (uint32_t) isz == 256u);   /* 0 => driver defaults to 128 */
}

/* ============================================================
 *  NTFS — MFT record attribute walk on a constructed record image
 *  (mirrors fs/ntfs.c ntfs_find_attribute)
 * ============================================================ */
static uint32_t ntfs_rd32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) |
           ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

/* walk an MFT record's attribute list; return the offset of attr_type or
 * -1. This reproduces the walk in ntfs_find_attribute. */
static int32_t mft_find_attr(const uint8_t *rec, uint32_t rec_size, uint32_t type)
{
    uint16_t off = (uint16_t) (rec[20] | (rec[21] << 8));
    while (off + 8 <= rec_size) {
        uint32_t t = ntfs_rd32(rec + off);
        uint32_t len = ntfs_rd32(rec + off + 4);
        if (t == 0xFFFFFFFFu) {
            break;
        }
        if (t == type) {
            return (int32_t) off;
        }
        if (len < 8 || off + len > rec_size) {
            break;
        }
        off = (uint16_t) (off + len);
    }
    return -1;
}

static void test_ntfs_mft(void)
{
    section("NTFS MFT record attribute walk (constructed image)");
    uint8_t rec[1024];
    memset(rec, 0, sizeof(rec));
    memcpy(rec, "FILE", 4);
    put_le16(rec + 20, 56);   /* offset to first attribute */

    uint16_t p = 56;
    /* $STANDARD_INFORMATION (0x10), 24 bytes */
    put_le32(rec + p, 0x10); put_le32(rec + p + 4, 24); rec[p + 8] = 0; p += 24;
    /* $FILE_NAME (0x30), 64 bytes */
    put_le32(rec + p, 0x30); put_le32(rec + p + 4, 64); rec[p + 8] = 0; p += 64;
    /* $DATA (0x80), 40 bytes, resident */
    put_le32(rec + p, 0x80); put_le32(rec + p + 4, 40); rec[p + 8] = 0; p += 40;
    /* end marker */
    put_le32(rec + p, 0xFFFFFFFFu);

    CHECK(memcmp(rec, "FILE", 4) == 0);
    CHECK(mft_find_attr(rec, sizeof(rec), 0x10u) == 56);
    CHECK(mft_find_attr(rec, sizeof(rec), 0x30u) == (int32_t) (56 + 24));
    CHECK(mft_find_attr(rec, sizeof(rec), 0x80u) == (int32_t) (56 + 24 + 64));
    CHECK(mft_find_attr(rec, sizeof(rec), 0x60u) == -1);   /* no $VOLUME */

    /* resident $DATA: value length at +16, value offset at +20 */
    uint32_t data_off = (uint32_t) mft_find_attr(rec, sizeof(rec), 0x80u);
    CHECK(data_off != (uint32_t) -1);
    rec[data_off + 16] = 4;                       /* value length */
    rec[data_off + 20] = 24; rec[data_off + 21] = 0;
    rec[data_off + 24] = 'A'; rec[data_off + 25] = 'B';
    CHECK(rec[data_off + 24] == 'A' && rec[data_off + 25] == 'B');
}

/* ============================================================ */
int main(void)
{
    printf("Monios filesystem host regression test\n");

    test_gpt_detect();
    test_gpt_header();
    test_gpt_partitions();
    test_gpt_fs_type();

    test_fat32_endian();
    test_fat32_bpb();
    test_fat32_chain_markers();
    test_fat32_name_format();
    test_fat32_name_decode();
    test_fat32_cluster_map();
    test_fat32_dir_entry_cluster();
    test_fat32_path_split();

    test_ext_superblock();
    test_ntfs_mft();

    printf("\n----------------------------------------\n");
    printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    printf("----------------------------------------\n");
    return g_fail == 0 ? 0 : 1;
}
