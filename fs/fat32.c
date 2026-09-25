#include "common.h"
#include "blockdev.h"
#include "file.h"
#include "fat32.h"
#include "kernel.h"

#define ATA_DATA_PORT         0x1F0
#define ATA_SECTOR_COUNT_PORT 0x1F2
#define ATA_LBA_LOW_PORT      0x1F3
#define ATA_LBA_MID_PORT      0x1F4
#define ATA_LBA_HIGH_PORT     0x1F5
#define ATA_DRIVE_PORT        0x1F6
#define ATA_COMMAND_PORT      0x1F7
#define ATA_STATUS_PORT       0x1F7

#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_STATUS_BSY        0x80
#define ATA_STATUS_DRQ        0x08
#define ATA_WAIT_LIMIT        1000000U

#define FAT32_ATTR_DIRECTORY  0x10
#define FAT32_ATTR_VOLUME_ID  0x08
#define FAT32_ATTR_LFN        0x0FU   /* LFN directory-entry attribute marker */
#define FAT32_ATTR_LFN_LAST   0x40U   /* ordinal bit: last LFN entry in chain  */
#define FAT32_LFN_CHARS_PER_ENTRY 13U
#define FAT32_LFN_MAX_ENTRIES 20U     /* ceil(255 / 13) */
#define FAT32_NAME_MAX        255U
#define FAT32_COMP_NAME_MAX   260U    /* path component buffer (long names) */
#define FAT32_CLUSTER_FREE    0x00000000
#define FAT32_CLUSTER_END     0x0FFFFFF8
#define FAT32_CLUSTER_BAD     0x0FFFFFF7
#define FAT32_CLUSTER_MASK    0x0FFFFFFF

typedef struct {
    uint8_t jump_boot[3];
    uint8_t oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t reserved2[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t fs_type[8];
} __attribute__((packed)) fat32_bpb_t;

typedef struct {
    uint8_t name[11];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat32_dir_entry_t;

typedef struct {
    bool is_root;
    uint32_t start_cluster;
} fat32_dir_ref_t;

typedef struct {
    bool valid;
    bool is_root;
    uint32_t sector_lba;
    uint16_t byte_offset;
    fat32_dir_entry_t entry;
} fat32_dir_slot_t;

static fat32_bpb_t g_bpb;
static uint32_t g_fat_lba;
static uint32_t g_root_lba;
static uint32_t g_root_sectors;
static uint32_t g_data_lba;
static uint32_t g_volume_lba;
static uint32_t g_cluster_count;
static bool g_fat32_ready;
static char g_fat32_read_cache_path[256];
static bool g_fat32_read_cache_valid;
static uint32_t g_fat32_read_cache_start_cluster;
static uint32_t g_fat32_read_cache_cluster_index;
static uint32_t g_fat32_read_cache_cluster;
static uint8_t g_fat32_write_buffer[8 * 512];
static bool g_fat32_fat_cache_valid;
static uint32_t g_fat32_fat_cache_lba;
static uint8_t g_fat32_fat_cache[512];

static void fat32_set_entry_cluster(fat32_dir_entry_t *entry, uint32_t cluster);
static uint32_t fat32_entry_cluster(const fat32_dir_entry_t *entry);

/* ---- Long File Name (LFN) support -------------------------------------- */
typedef struct {
    char     long_name[FAT32_NAME_MAX + 1U]; /* decoded long name (or 8.3 short) */
    fat32_dir_entry_t entry;                  /* the real (short) directory entry */
    uint32_t sector_lba;
    uint16_t byte_offset;
    uint32_t lfn_count;                       /* preceding LFN entries */
    uint32_t lfn_lbas[FAT32_LFN_MAX_ENTRIES]; /* location of each LFN entry */
    uint16_t lfn_offs[FAT32_LFN_MAX_ENTRIES];
} fat32_dir_item_t;

typedef struct {
    fat32_dir_ref_t dir;
    uint32_t sector_index;
    uint8_t  sector[512];
    bool     sector_loaded;
    uint32_t sector_cluster;
    uint32_t entry_i;
    bool     done;
    fat32_dir_entry_t lfn[FAT32_LFN_MAX_ENTRIES];
    uint32_t lfn_lbas[FAT32_LFN_MAX_ENTRIES];
    uint16_t lfn_offs[FAT32_LFN_MAX_ENTRIES];
    uint32_t lfn_count;
} fat32_dir_cursor_t;

static bool fat32_find_item_in_dir(const fat32_dir_ref_t *dir, const char *name,
                                   fat32_dir_item_t *item_out);
static bool fat32_create_named_entry(fat32_dir_ref_t *dir, const char *long_name,
                                     fat32_dir_slot_t *short_slot_out);

static void fat32_clear_read_cache(void)
{
    g_fat32_read_cache_path[0] = '\0';
    g_fat32_read_cache_valid = false;
    g_fat32_read_cache_start_cluster = 0;
    g_fat32_read_cache_cluster_index = 0;
    g_fat32_read_cache_cluster = 0;
}

static void fat32_clear_fat_cache(void)
{
    g_fat32_fat_cache_valid = false;
    g_fat32_fat_cache_lba = 0;
}

static uint32_t fat32_total_sectors(void)
{
    return g_bpb.total_sectors_16 != 0 ? g_bpb.total_sectors_16 : g_bpb.total_sectors_32;
}

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t) data[0] | ((uint16_t) data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t) data[0] |
           ((uint32_t) data[1] << 8) |
           ((uint32_t) data[2] << 16) |
           ((uint32_t) data[3] << 24);
}

static bool fat32_bpb_is_valid(const fat32_bpb_t *bpb)
{
    return bpb->bytes_per_sector == 512 &&
           bpb->sectors_per_cluster != 0 &&
           bpb->fat_size_16 == 0 &&
           bpb->root_entry_count == 0 &&
           bpb->fat_size_32 != 0 &&
           bpb->root_cluster >= 2;
}

static bool fat32_partition_type(uint8_t type)
{
    return type == 0x0B || type == 0x0C || type == 0xEF;
}

/* 本卷所在的块设备序号，由 fat32_init() 从 file_blockdev_hint() 取。
 * >= 0 时所有扇区 I/O 走 drivers/storage/blockdev.c 的块设备抽象；
 * < 0（未知）时回退到下面的 legacy ATA PIO。
 *
 * 为什么必须走块设备层：过去这里直接读写 0x1F0 端口，在没有 legacy IDE
 * 控制器的机器上（AHCI/NVMe-only，含 QEMU -machine q35）端口读回全 0，
 * BPB 全是 0 → 挂载失败 → 读不到 kernel.exe → BSOD。 */
static int32_t g_fat32_blockdev = -1;

/* 写失败只报一次，避免坏设备上刷屏。 */
static bool g_fat32_write_warned;

/* ============================================================
 *  Legacy ATA PIO 回退路径
 *
 *  仅在 g_fat32_blockdev < 0（拿不到块设备提示）时使用。所有 LBA 参数都是
 *  整盘绝对扇区号，与块设备层的语义完全一致，两条路径可以逐字节互换。
 * ============================================================ */

static bool ata_wait_not_busy(void)
{
    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; i++) {
        if ((inb(ATA_STATUS_PORT) & ATA_STATUS_BSY) == 0) {
            return true;
        }
    }
    return false;
}

static bool ata_wait_data_ready(void)
{
    if (!ata_wait_not_busy()) {
        return false;
    }
    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; i++) {
        if ((inb(ATA_STATUS_PORT) & ATA_STATUS_DRQ) != 0) {
            return true;
        }
    }
    return false;
}

/* 读一个扇区。返回 false 表示 ATA 控制器没有在状态寄存器上给出预期响应
 * （BSY 一直不落 / DRQ 一直不置），此时缓冲区被清零。
 * 返回值的意义在于让调用方能区分"读到了全零"和"根本没读到" —— 这是挂载
 * 失败最常见的根因，过去因为没有返回值而完全无法定位。 */
static bool ata_read_sector(uint32_t lba, void *buffer)
{
    uint16_t *dst = (uint16_t *) buffer;

    if (g_fat32_blockdev >= 0) {
        if (blockdev_raw_read_sectors(g_fat32_blockdev, lba, 1, buffer)) {
            return true;
        }
        memset(buffer, 0, 512);
        return false;
    }

    if (!ata_wait_not_busy()) {
        memset(buffer, 0, 512);
        return false;
    }
    outb(ATA_DRIVE_PORT, (uint8_t) (0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT_PORT, 1);
    outb(ATA_LBA_LOW_PORT, (uint8_t) (lba & 0xFF));
    outb(ATA_LBA_MID_PORT, (uint8_t) ((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH_PORT, (uint8_t) ((lba >> 16) & 0xFF));
    outb(ATA_COMMAND_PORT, ATA_CMD_READ_SECTORS);

    if (!ata_wait_data_ready()) {
        memset(buffer, 0, 512);
        return false;
    }
    for (uint32_t i = 0; i < 256; i++) {
        dst[i] = inw(ATA_DATA_PORT);
    }
    return true;
}

static void ata_read_sectors(uint32_t lba, uint8_t count, void *buffer)
{
    uint16_t *dst = (uint16_t *) buffer;

    if (count == 0) {
        return;
    }
    if (g_fat32_blockdev >= 0) {
        /* 失败时整体清零：调用方拿不到返回值（本函数为 void），必须保证
         * 失败后缓冲区不会残留上一次的旧内容被当成有效数据。 */
        if (!blockdev_raw_read_sectors(g_fat32_blockdev, lba, count, buffer)) {
            memset(buffer, 0, (uint32_t) count * 512u);
        }
        return;
    }
    if (!ata_wait_not_busy()) {
        memset(buffer, 0, (uint32_t) count * 512u);
        return;
    }
    outb(ATA_DRIVE_PORT, (uint8_t) (0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT_PORT, count);
    outb(ATA_LBA_LOW_PORT, (uint8_t) (lba & 0xFF));
    outb(ATA_LBA_MID_PORT, (uint8_t) ((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH_PORT, (uint8_t) ((lba >> 16) & 0xFF));
    outb(ATA_COMMAND_PORT, ATA_CMD_READ_SECTORS);

    for (uint8_t sector = 0; sector < count; sector++) {
        if (!ata_wait_data_ready()) {
            memset(dst, 0, (uint32_t) (count - sector) * 512u);
            return;
        }
        for (uint32_t i = 0; i < 256; i++) {
            *dst++ = inw(ATA_DATA_PORT);
        }
    }
}

static void ata_write_sector(uint32_t lba, const void *buffer)
{
    const uint16_t *src = (const uint16_t *) buffer;

    if (g_fat32_blockdev >= 0) {
        if (!blockdev_raw_write_sectors(g_fat32_blockdev, lba, 1, buffer) &&
            !g_fat32_write_warned) {
            g_fat32_write_warned = true;
            log_write("fat32: warning - block device rejected write (volume read-only)");
        }
        return;
    }
    if (!ata_wait_not_busy()) {
        return;
    }
    outb(ATA_DRIVE_PORT, (uint8_t) (0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT_PORT, 1);
    outb(ATA_LBA_LOW_PORT, (uint8_t) (lba & 0xFF));
    outb(ATA_LBA_MID_PORT, (uint8_t) ((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH_PORT, (uint8_t) ((lba >> 16) & 0xFF));
    outb(ATA_COMMAND_PORT, ATA_CMD_WRITE_SECTORS);

    if (!ata_wait_data_ready()) {
        return;
    }
    for (uint32_t i = 0; i < 256; i++) {
        outw(ATA_DATA_PORT, src[i]);
    }
    (void) ata_wait_not_busy();
}

static void ata_write_sectors(uint32_t lba, uint8_t count, const void *buffer)
{
    const uint16_t *src = (const uint16_t *) buffer;

    if (count == 0) {
        return;
    }
    if (g_fat32_blockdev >= 0) {
        if (!blockdev_raw_write_sectors(g_fat32_blockdev, lba, count, buffer) &&
            !g_fat32_write_warned) {
            g_fat32_write_warned = true;
            log_write("fat32: warning - block device rejected write (volume read-only)");
        }
        return;
    }
    if (!ata_wait_not_busy()) {
        return;
    }
    outb(ATA_DRIVE_PORT, (uint8_t) (0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT_PORT, count);
    outb(ATA_LBA_LOW_PORT, (uint8_t) (lba & 0xFF));
    outb(ATA_LBA_MID_PORT, (uint8_t) ((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH_PORT, (uint8_t) ((lba >> 16) & 0xFF));
    outb(ATA_COMMAND_PORT, ATA_CMD_WRITE_SECTORS);

    for (uint8_t sector = 0; sector < count; sector++) {
        if (!ata_wait_data_ready()) {
            return;
        }
        for (uint32_t i = 0; i < 256; i++) {
            outw(ATA_DATA_PORT, *src++);
        }
    }
    (void) ata_wait_not_busy();
}

static bool fat32_is_end_cluster(uint32_t cluster)
{
    cluster &= FAT32_CLUSTER_MASK;
    return cluster >= FAT32_CLUSTER_END && cluster != FAT32_CLUSTER_BAD;
}

/* ============================================================
 *  FAT32 Write-Ahead Log (WAL)
 *
 *  The log lives in the last FAT32_WAL_SECTORS sectors of the volume, which
 *  are carved out of g_cluster_count so no data cluster can ever overlap it.
 *  Each record occupies 3 sectors: header | old data | new data.
 *
 *  On every metadata write (FAT entry / directory entry / directory sector)
 *  we first append a record {target_lba, old, new, status=IN_PROGRESS}, then
 *  perform the real sector write, then flip the record to COMMITTED.
 *
 *  On mount we replay:
 *    - IN_PROGRESS records  -> undo  (restore the old image)
 *    - COMMITTED records    -> redo  (write the new image)
 *  A clean unmount writes the log header CLEAN, so a normal boot skips all
 *  recovery; an abnormal shutdown leaves it DIRTY and the boot path then runs
 *  chkdsk as a second safety net.
 * ============================================================ */
#define FAT32_WAL_SECTORS       512U
#define FAT32_WAL_REC_SECTORS   3U
#define FAT32_WAL_ST_FREE       0U
#define FAT32_WAL_ST_INPROGRESS 1U
#define FAT32_WAL_ST_COMMITTED  2U
#define FAT32_WAL_CLEAN         1U
#define FAT32_WAL_DIRTY         0U

static bool     g_wal_on;
static uint32_t g_wal_log_lba;
static uint32_t g_wal_next;
static uint32_t g_wal_max;
static uint32_t g_wal_seq;
static bool     g_wal_dirty_boot;

static bool wal_lba_in_log(uint32_t lba)
{
    return lba >= g_wal_log_lba && lba < g_wal_log_lba + FAT32_WAL_SECTORS;
}

static uint32_t wal_rec_hdr_lba(uint32_t i)
{
    return g_wal_log_lba + 1U + i * FAT32_WAL_REC_SECTORS;
}

static void wal_store_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v & 0xFFU);
    p[1] = (uint8_t) ((v >> 8) & 0xFFU);
    p[2] = (uint8_t) ((v >> 16) & 0xFFU);
    p[3] = (uint8_t) ((v >> 24) & 0xFFU);
}

static uint32_t wal_load_le32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) |
           ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static uint32_t wal_checksum_sectors(const uint8_t *old_img, const uint8_t *new_img)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < 512U; i += 4U) {
        uint32_t o = (uint32_t) old_img[i] | ((uint32_t) old_img[i + 1] << 8) |
                     ((uint32_t) old_img[i + 2] << 16) | ((uint32_t) old_img[i + 3] << 24);
        uint32_t n = (uint32_t) new_img[i] | ((uint32_t) new_img[i + 1] << 8) |
                     ((uint32_t) new_img[i + 2] << 16) | ((uint32_t) new_img[i + 3] << 24);
        sum += o + n;
    }
    return sum;
}

static void wal_write_header(uint32_t clean, uint32_t next)
{
    uint8_t hdr[512];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'M'; hdr[1] = 'W'; hdr[2] = 'L'; hdr[3] = 'G';
    wal_store_le32(hdr + 4, 1U);           /* version */
    wal_store_le32(hdr + 8, clean);
    wal_store_le32(hdr + 12, next);
    wal_store_le32(hdr + 16, g_wal_max);
    ata_write_sector(g_wal_log_lba, hdr);
}

/* Print "prefix<decimal>" to the serial console (used during early boot
 * before the VFS log file exists). */
static void wal_log_u32(const char *prefix, uint32_t v)
{
    char buf[32];
    uint32_t p = 0;
    char tmp[12];
    uint32_t n = 0;

    while (prefix != NULL && *prefix != '\0' && p < 20U) {
        buf[p++] = *prefix++;
    }
    if (v == 0U) {
        buf[p++] = '0';
    } else {
        while (v > 0U && n < 11U) {
            tmp[n++] = (char) ('0' + (v % 10U));
            v /= 10U;
        }
        while (n > 0U && p < 31U) {
            buf[p++] = tmp[--n];
        }
    }
    buf[p] = '\0';
    serial_write(buf);
}

/* The single logged-write primitive. All FAT / directory metadata writes
 * funnel through here. Falls back to a raw write when WAL is off or when the
 * target lives inside the log area itself (no recursion). */
static void fat32_logged_write_sector(uint32_t target_lba, const void *newbuf)
{
    uint8_t old_img[512];
    uint8_t hdr[512];
    uint32_t idx;
    uint32_t h;

    if (!g_wal_on || newbuf == NULL || wal_lba_in_log(target_lba)) {
        ata_write_sector(target_lba, newbuf);
        return;
    }
    if (target_lba < g_volume_lba || target_lba >= g_volume_lba + fat32_total_sectors()) {
        ata_write_sector(target_lba, newbuf);
        return;
    }

    ata_read_sector(target_lba, old_img);

    if (g_wal_next >= g_wal_max) {
        /* Log full: every earlier record was already applied to disk, so we
         * checkpoint by restarting the circular log from slot 0. */
        g_wal_next = 0;
    }
    idx = g_wal_next;
    h = wal_rec_hdr_lba(idx);

    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'W'; hdr[1] = 'R'; hdr[2] = 'E'; hdr[3] = 'C';
    wal_store_le32(hdr + 4, g_wal_seq++);
    wal_store_le32(hdr + 8, target_lba);
    wal_store_le32(hdr + 12, wal_checksum_sectors(old_img, (const uint8_t *) newbuf));
    wal_store_le32(hdr + 16, FAT32_WAL_ST_INPROGRESS);

    ata_write_sector(h, hdr);               /* log: header (IN_PROGRESS) */
    ata_write_sector(h + 1U, old_img);      /* log: old image */
    ata_write_sector(h + 2U, newbuf);       /* log: new image */

    ata_write_sector(target_lba, newbuf);   /* the real write */

    wal_store_le32(hdr + 16, FAT32_WAL_ST_COMMITTED);
    ata_write_sector(h, hdr);               /* commit the record */

    g_wal_next = idx + 1U;
    wal_write_header(FAT32_WAL_DIRTY, g_wal_next);
}

/* Replay the log at mount time. Runs with g_wal_on == false so the recovery
 * writes themselves are never logged. */
static void wal_recover(void)
{
    uint8_t hdr[512];
    uint32_t clean, limit, maxrec;
    uint32_t undone = 0;
    uint32_t redone = 0;

    ata_read_sector(g_wal_log_lba, hdr);
    if (hdr[0] != 'M' || hdr[1] != 'W' || hdr[2] != 'L' || hdr[3] != 'G') {
        /* No valid log yet -> start out clean. */
        g_wal_next = 0;
        g_wal_dirty_boot = false;
        wal_write_header(FAT32_WAL_CLEAN, 0U);
        return;
    }

    clean = wal_load_le32(hdr + 8);
    limit = wal_load_le32(hdr + 12);
    maxrec = wal_load_le32(hdr + 16);
    if (maxrec == 0U) {
        maxrec = g_wal_max;
    }
    if (limit > maxrec) {
        limit = maxrec;
    }

    for (uint32_t i = 0; i < limit; i++) {
        uint8_t rh[512];
        uint32_t st, tl;
        uint32_t h = wal_rec_hdr_lba(i);

        ata_read_sector(h, rh);
        if (rh[0] != 'W' || rh[1] != 'R' || rh[2] != 'E' || rh[3] != 'C') {
            continue;
        }
        st = wal_load_le32(rh + 16);
        tl = wal_load_le32(rh + 8);
        if (st == FAT32_WAL_ST_INPROGRESS) {
            uint8_t oldb[512];
            ata_read_sector(h + 1U, oldb);
            ata_write_sector(tl, oldb);     /* undo */
            undone++;
        } else if (st == FAT32_WAL_ST_COMMITTED) {
            uint8_t newb[512];
            ata_read_sector(h + 2U, newb);
            ata_write_sector(tl, newb);     /* redo */
            redone++;
        }
    }

    g_wal_dirty_boot = (clean == FAT32_WAL_DIRTY);
    /* Reset the log to a clean, empty state after replay. */
    g_wal_next = 0;
    wal_write_header(FAT32_WAL_CLEAN, 0U);

    if (undone != 0U || redone != 0U) {
        wal_log_u32("fat32-wal: recovered undone=", undone);
        wal_log_u32(" redo=", redone);
        serial_write("\r\n");
    }
}

void fat32_wal_mark_clean(void)
{
    if (!g_fat32_ready || g_wal_max == 0U) {
        return;
    }
    wal_write_header(FAT32_WAL_CLEAN, 0U);
    g_wal_next = 0;
}

bool fat32_wal_dirty_at_mount(void)
{
    return g_wal_dirty_boot;
}

static void fat32_format_component(char out[11], const char *name)
{
    uint32_t i = 0;
    uint32_t j = 0;
    bool ext = false;

    memset(out, ' ', 11);
    while (name[i] != '\0') {
        char ch = name[i++];
        if (ch == '.') {
            ext = true;
            j = 8;
            continue;
        }
        if (j >= 11) {
            break;
        }
        if (ch >= 'a' && ch <= 'z') {
            ch = (char) (ch - 'a' + 'A');
        }
        out[j++] = ch;
        if (!ext && j == 8) {
            ext = true;
            j = 8;
        }
    }
}

static void fat32_decode_name(char out[13], const uint8_t name[11])
{
    uint32_t i = 0;
    uint32_t j = 0;
    bool have_ext = false;

    while (i < 8 && name[i] != ' ') {
        out[j++] = (char) name[i++];
    }
    while (i < 8) i++;
    for (uint32_t k = 8; k < 11; k++) {
        if (name[k] != ' ') {
            have_ext = true;
            break;
        }
    }
    if (have_ext) {
        out[j++] = '.';
        for (uint32_t k = 8; k < 11 && name[k] != ' '; k++) {
            out[j++] = (char) name[k];
        }
    }
    out[j] = '\0';
}

static uint32_t fat32_cluster_to_lba(uint32_t cluster)
{
    return g_data_lba + (uint32_t) (cluster - 2) * g_bpb.sectors_per_cluster;
}

static uint32_t fat32_get_fat_entry(uint32_t cluster)
{
    uint8_t sectors[1024];
    uint32_t value;
    uint32_t fat_offset = cluster * 4;
    uint32_t sector_lba = g_fat_lba + (fat_offset / g_bpb.bytes_per_sector);
    uint32_t sector_offset = fat_offset % g_bpb.bytes_per_sector;

    if (sector_offset <= g_bpb.bytes_per_sector - sizeof(value)) {
        if (!g_fat32_fat_cache_valid || g_fat32_fat_cache_lba != sector_lba) {
            ata_read_sector(sector_lba, g_fat32_fat_cache);
            g_fat32_fat_cache_lba = sector_lba;
            g_fat32_fat_cache_valid = true;
        }
        memcpy(&value, g_fat32_fat_cache + sector_offset, sizeof(value));
    } else {
        ata_read_sectors(sector_lba, 2, sectors);
        memcpy(&value, sectors + sector_offset, sizeof(value));
    }
    return value & FAT32_CLUSTER_MASK;
}

static void fat32_set_fat_entry(uint32_t cluster, uint32_t value)
{
    uint8_t sector[1024];
    uint32_t fat_offset = cluster * 4;
    uint32_t sector_index = fat_offset / g_bpb.bytes_per_sector;
    uint32_t sector_offset = fat_offset % g_bpb.bytes_per_sector;

    for (uint8_t fat = 0; fat < g_bpb.fat_count; fat++) {
        uint32_t sector_lba = g_fat_lba + fat * g_bpb.fat_size_32 + sector_index;
        uint32_t current;
        if (sector_offset <= g_bpb.bytes_per_sector - sizeof(current)) {
            ata_read_sector(sector_lba, sector);
            memcpy(&current, sector + sector_offset, sizeof(current));
            current = (current & ~FAT32_CLUSTER_MASK) | (value & FAT32_CLUSTER_MASK);
            memcpy(sector + sector_offset, &current, sizeof(current));
            fat32_logged_write_sector(sector_lba, sector);
            if (fat == 0 && g_fat32_fat_cache_valid && g_fat32_fat_cache_lba == sector_lba) {
                memcpy(g_fat32_fat_cache, sector, sizeof(g_fat32_fat_cache));
            }
        } else {
            ata_read_sectors(sector_lba, 2, sector);
            memcpy(&current, sector + sector_offset, sizeof(current));
            current = (current & ~FAT32_CLUSTER_MASK) | (value & FAT32_CLUSTER_MASK);
            memcpy(sector + sector_offset, &current, sizeof(current));
            fat32_logged_write_sector(sector_lba, sector);
            fat32_logged_write_sector(sector_lba + 1U, (const uint8_t *) sector + 512U);
            if (fat == 0) {
                fat32_clear_fat_cache();
            }
        }
    }
}

static uint32_t fat32_allocate_cluster(void)
{
    for (uint32_t cluster = 2; cluster < g_cluster_count + 2; cluster++) {
        if (fat32_get_fat_entry(cluster) == FAT32_CLUSTER_FREE) {
            fat32_set_fat_entry(cluster, FAT32_CLUSTER_END);
            return cluster;
        }
    }
    return 0;
}

static void fat32_zero_cluster(uint32_t cluster)
{
    uint8_t sector[512];
    memset(sector, 0, sizeof(sector));
    for (uint8_t i = 0; i < g_bpb.sectors_per_cluster; i++) {
        ata_write_sector(fat32_cluster_to_lba(cluster) + i, sector);
    }
}

static void fat32_free_chain(uint32_t cluster)
{
    while (cluster >= 2 && cluster < g_cluster_count + 2) {
        uint32_t next = fat32_get_fat_entry(cluster);
        fat32_set_fat_entry(cluster, FAT32_CLUSTER_FREE);
        if (fat32_is_end_cluster(next) || next == FAT32_CLUSTER_FREE || next == FAT32_CLUSTER_BAD) {
            break;
        }
        cluster = next;
    }
}

__attribute__((unused)) static bool fat32_dir_is_root(const fat32_dir_ref_t *dir)
{
    return dir->is_root;
}

__attribute__((unused)) static uint32_t fat32_dir_sector_count(const fat32_dir_ref_t *dir)
{
    (void) dir;
    return g_bpb.sectors_per_cluster;
}

static bool fat32_read_dir_sector(const fat32_dir_ref_t *dir, uint32_t sector_index, uint8_t *buffer, uint32_t *cluster_out)
{
    uint32_t cluster = dir->start_cluster;
    uint32_t remaining = sector_index;

    while (cluster >= 2 && cluster < g_cluster_count + 2) {
        if (remaining < g_bpb.sectors_per_cluster) {
            ata_read_sector(fat32_cluster_to_lba(cluster) + remaining, buffer);
            if (cluster_out) *cluster_out = cluster;
            return true;
        }
        remaining -= g_bpb.sectors_per_cluster;
        cluster = fat32_get_fat_entry(cluster);
        if (fat32_is_end_cluster(cluster)) {
            break;
        }
    }
    return false;
}

static bool fat32_write_dir_sector(const fat32_dir_ref_t *dir, uint32_t sector_index, const uint8_t *buffer)
{
    uint32_t cluster = dir->start_cluster;
    uint32_t remaining = sector_index;

    while (cluster >= 2 && cluster < g_cluster_count + 2) {
        if (remaining < g_bpb.sectors_per_cluster) {
            fat32_logged_write_sector(fat32_cluster_to_lba(cluster) + remaining, buffer);
            return true;
        }
        remaining -= g_bpb.sectors_per_cluster;
        cluster = fat32_get_fat_entry(cluster);
        if (fat32_is_end_cluster(cluster)) {
            break;
        }
    }
    return false;
}

static bool fat32_extend_dir(fat32_dir_ref_t *dir)
{
    uint32_t new_cluster;
    uint32_t cluster;

    new_cluster = fat32_allocate_cluster();
    if (new_cluster == 0) {
        return false;
    }
    fat32_zero_cluster(new_cluster);

    cluster = dir->start_cluster;
    while (!fat32_is_end_cluster(fat32_get_fat_entry(cluster))) {
        cluster = fat32_get_fat_entry(cluster);
    }
    fat32_set_fat_entry(cluster, new_cluster);
    fat32_set_fat_entry(new_cluster, FAT32_CLUSTER_END);
    return true;
}

static bool fat32_find_entry_in_dir(const fat32_dir_ref_t *dir, const char *name, fat32_dir_slot_t *slot_out)
{
    fat32_dir_item_t item;

    if (!fat32_find_item_in_dir(dir, name, &item)) {
        return false;
    }
    slot_out->valid = true;
    slot_out->is_root = dir->is_root;
    slot_out->sector_lba = item.sector_lba;
    slot_out->byte_offset = item.byte_offset;
    slot_out->entry = item.entry;
    return true;
}

static bool fat32_find_free_slot(fat32_dir_ref_t *dir, fat32_dir_slot_t *slot_out)
{
    uint8_t sector[512];
    uint32_t sector_cluster;
    uint32_t sector_limit = g_bpb.sectors_per_cluster == 0 ? 1u : g_bpb.sectors_per_cluster * 32u;

    for (uint32_t sector_index = 0; sector_index < sector_limit; sector_index++) {
        if (!fat32_read_dir_sector(dir, sector_index, sector, &sector_cluster)) {
            if (fat32_extend_dir(dir)) {
                continue;
            }
            return false;
        }
        for (uint32_t i = 0; i < 512 / sizeof(fat32_dir_entry_t); i++) {
            fat32_dir_entry_t *entry = ((fat32_dir_entry_t *) sector) + i;
            if (entry->name[0] == 0x00 || entry->name[0] == 0xE5) {
                slot_out->valid = true;
                slot_out->is_root = dir->is_root;
                slot_out->sector_lba = fat32_cluster_to_lba(sector_cluster) + (sector_index % g_bpb.sectors_per_cluster);
                slot_out->byte_offset = (uint16_t) (i * sizeof(fat32_dir_entry_t));
                memset(&slot_out->entry, 0, sizeof(slot_out->entry));
                return true;
            }
        }
    }
    return false;
}

static bool fat32_write_slot(const fat32_dir_ref_t *dir, const fat32_dir_slot_t *slot)
{
    uint8_t sector[512];
    uint32_t sector_lba = slot->sector_lba;
    (void) dir;

    ata_read_sector(sector_lba, sector);
    memcpy(sector + slot->byte_offset, &slot->entry, sizeof(fat32_dir_entry_t));
    fat32_logged_write_sector(sector_lba, sector);
    return true;
}

/* ============================================================
 *  Long File Name (LFN) machinery
 * ============================================================ */

/* Checksum over the 11-byte 8.3 short name, per FAT spec. */
static uint8_t fat32_lfn_checksum(const uint8_t *short_name)
{
    uint8_t sum = 0;
    for (uint32_t i = 0; i < 11U; i++) {
        sum = (uint8_t) (((sum & 1U) ? 0x80U : 0U) + (uint8_t) (sum >> 1) + short_name[i]);
    }
    return sum;
}

/* Byte offset of UTF-16LE char `idx` (0..12) inside a 32-byte LFN entry. */
static const uint8_t g_lfn_char_off[FAT32_LFN_CHARS_PER_ENTRY] =
    {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

static uint16_t fat32_lfn_get_char(const fat32_dir_entry_t *e, uint32_t idx)
{
    const uint8_t *b = (const uint8_t *) e;
    uint8_t off = g_lfn_char_off[idx];
    return (uint16_t) b[off] | ((uint16_t) b[off + 1U] << 8);
}

static void fat32_lfn_set_char(fat32_dir_entry_t *e, uint32_t idx, uint16_t ch)
{
    uint8_t *b = (uint8_t *) e;
    uint8_t off = g_lfn_char_off[idx];
    b[off] = (uint8_t) (ch & 0xFFU);
    b[off + 1U] = (uint8_t) ((ch >> 8) & 0xFFU);
}

static void fat32_cursor_init(fat32_dir_cursor_t *c, const fat32_dir_ref_t *dir)
{
    c->dir = *dir;
    c->sector_index = 0;
    c->sector_loaded = false;
    c->entry_i = 0;
    c->done = false;
    c->lfn_count = 0;
}

/* Assemble the long name for the just-found real entry from buffered LFN. */
static void fat32_cursor_build_name(fat32_dir_cursor_t *c, fat32_dir_item_t *out)
{
    uint32_t pos = 0;

    out->lfn_count = c->lfn_count;
    for (uint32_t k = 0; k < c->lfn_count; k++) {
        out->lfn_lbas[k] = c->lfn_lbas[k];
        out->lfn_offs[k] = c->lfn_offs[k];
    }

    if (c->lfn_count == 0U) {
        fat32_decode_name(out->long_name, out->entry.name);
        return;
    }

    for (uint32_t block = 1; block <= c->lfn_count; block++) {
        for (uint32_t k = 0; k < c->lfn_count; k++) {
            uint8_t ord = (uint8_t) (c->lfn[k].name[0] & 0x1FU);

            if (ord != block) {
                continue;
            }
            for (uint32_t idx = 0; idx < FAT32_LFN_CHARS_PER_ENTRY; idx++) {
                uint16_t ch = fat32_lfn_get_char(&c->lfn[k], idx);
                if (ch == 0x0000U || pos >= FAT32_NAME_MAX) {
                    out->long_name[pos] = '\0';
                    return;
                }
                out->long_name[pos++] = (ch < 0x80U) ? (char) ch : '?';
            }
            break;
        }
    }
    out->long_name[pos] = '\0';
}

/* Advance to the next real (non-LFN, non-deleted) directory entry. */
static bool fat32_cursor_next(fat32_dir_cursor_t *c, fat32_dir_item_t *out)
{
    while (!c->done) {
        if (!c->sector_loaded) {
            if (!fat32_read_dir_sector(&c->dir, c->sector_index, c->sector, &c->sector_cluster)) {
                c->done = true;
                return false;
            }
            c->sector_loaded = true;
            c->entry_i = 0;
        }
        while (c->entry_i < 16U) {
            fat32_dir_entry_t *e = ((fat32_dir_entry_t *) c->sector) + c->entry_i;
            uint32_t lba = fat32_cluster_to_lba(c->sector_cluster) +
                           (c->sector_index % g_bpb.sectors_per_cluster);
            uint16_t off = (uint16_t) (c->entry_i * sizeof(fat32_dir_entry_t));

            if (e->name[0] == 0x00U) {
                c->done = true;
                return false;
            }
            if (e->name[0] == 0xE5U) {
                c->entry_i++;
                c->lfn_count = 0;
                continue;
            }
            if (e->attr == FAT32_ATTR_LFN) {
                if (c->lfn_count < FAT32_LFN_MAX_ENTRIES) {
                    c->lfn[c->lfn_count] = *e;
                    c->lfn_lbas[c->lfn_count] = lba;
                    c->lfn_offs[c->lfn_count] = off;
                    c->lfn_count++;
                }
                c->entry_i++;
                continue;
            }
            if ((e->attr & FAT32_ATTR_VOLUME_ID) != 0U) {
                c->entry_i++;
                c->lfn_count = 0;
                continue;
            }

            out->entry = *e;
            out->sector_lba = lba;
            out->byte_offset = off;
            out->long_name[0] = '\0';
            fat32_cursor_build_name(c, out);
            c->entry_i++;
            c->lfn_count = 0;
            return true;
        }
        c->sector_loaded = false;
        c->sector_index++;
    }
    return false;
}

static bool fat32_find_item_in_dir(const fat32_dir_ref_t *dir, const char *name,
                                    fat32_dir_item_t *item_out)
{
    fat32_dir_cursor_t cur;
    char target83[11];

    fat32_format_component(target83, name);
    fat32_cursor_init(&cur, dir);
    while (fat32_cursor_next(&cur, item_out)) {
        if (item_out->entry.name[0] == '.' &&
            (item_out->entry.name[1] == ' ' || item_out->entry.name[1] == '.')) {
            continue;
        }
        if (strcasecmp(item_out->long_name, name) == 0) {
            return true;
        }
        if (memcmp(item_out->entry.name, target83, 11) == 0) {
            return true;
        }
    }
    return false;
}

/* Mark a real entry and all its preceding LFN entries as deleted. */
static void fat32_delete_item_slots(const fat32_dir_item_t *item)
{
    uint8_t sector[512];

    for (uint32_t k = 0; k < item->lfn_count; k++) {
        ata_read_sector(item->lfn_lbas[k], sector);
        sector[item->lfn_offs[k]] = 0xE5U;
        fat32_logged_write_sector(item->lfn_lbas[k], sector);
    }
    ata_read_sector(item->sector_lba, sector);
    sector[item->byte_offset] = 0xE5U;
    fat32_logged_write_sector(item->sector_lba, sector);
}

/* Does this name need an LFN chain (i.e. is it not already a clean 8.3)? */
static bool fat32_name_needs_lfn(const char *name)
{
    char tmp[11];
    char decoded[13];

    fat32_format_component(tmp, name);
    fat32_decode_name(decoded, (const uint8_t *) tmp);
    return strcmp(decoded, name) != 0;
}

/* Allocate `count` consecutive free slots at the end of the directory (the
 * 0x00 end marker), extending the directory when it runs out of clusters. */
static bool fat32_alloc_append_slots(fat32_dir_ref_t *dir, uint32_t count,
                                     fat32_dir_slot_t *slots)
{
    for (uint32_t attempt = 0; attempt < 128U; attempt++) {
        uint8_t sector[512];
        uint32_t si = 0;
        uint32_t ei = 0;
        uint32_t sc = 0;
        bool found = false;

        while (fat32_read_dir_sector(dir, si, sector, &sc)) {
            fat32_dir_entry_t *entries = (fat32_dir_entry_t *) sector;
            for (ei = 0; ei < 16U; ei++) {
                if (entries[ei].name[0] == 0x00U) {
                    found = true;
                    goto found_end;
                }
            }
            si++;
        }
found_end:
        if (!found) {
            if (!fat32_extend_dir(dir)) {
                return false;
            }
            continue;
        }

        {
            uint32_t avail = 16U - ei;
            uint32_t s2 = si + 1U;
            while (fat32_read_dir_sector(dir, s2, sector, NULL)) {
                avail += 16U;
                s2++;
            }
            if (avail < count) {
                if (!fat32_extend_dir(dir)) {
                    return false;
                }
                continue;
            }
            {
                uint32_t csi = si;
                uint32_t cei = ei;
                uint32_t csc;
                fat32_read_dir_sector(dir, csi, sector, &csc);
                for (uint32_t k = 0; k < count; k++) {
                    slots[k].valid = true;
                    slots[k].is_root = dir->is_root;
                    slots[k].sector_lba = fat32_cluster_to_lba(csc) +
                                          (csi % g_bpb.sectors_per_cluster);
                    slots[k].byte_offset = (uint16_t) (cei * sizeof(fat32_dir_entry_t));
                    memset(&slots[k].entry, 0, sizeof(slots[k].entry));
                    cei++;
                    if (cei >= 16U) {
                        cei = 0;
                        csi++;
                        fat32_read_dir_sector(dir, csi, sector, &csc);
                    }
                }
                return true;
            }
        }
    }
    return false;
}

/* Build an 8.3 short name (out[11], space padded) for a long display name. */
static void fat32_lfn_make_short(const char *long_name, uint32_t num, uint8_t out[11])
{
    char base[64];
    char ext[8];
    uint32_t blen = 0;
    uint32_t elen = 0;
    const char *dot = NULL;
    uint32_t blimit;
    uint32_t k = 0;
    char numbuf[8];
    uint32_t nl = 0;

    memset(out, ' ', 11);

    for (const char *p = long_name; *p != '\0'; p++) {
        if (*p == '.') {
            dot = p;
        }
    }
    blimit = dot != NULL ? (uint32_t) (dot - long_name) : (uint32_t) strlen(long_name);
    for (uint32_t i = 0; i < blimit && blen < sizeof(base) - 1U; i++) {
        char c = long_name[i];
        if (c == ' ' || c == '.' || c == '/') {
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            c = (char) (c - 'a' + 'A');
        }
        base[blen++] = c;
    }
    base[blen] = '\0';

    if (dot != NULL) {
        const char *p = dot + 1;
        while (*p != '\0' && elen < 3U) {
            if (*p != ' ' && *p != '.') {
                char c = *p;
                if (c >= 'a' && c <= 'z') {
                    c = (char) (c - 'a' + 'A');
                }
                ext[elen++] = c;
            }
            p++;
        }
    }

    k = 0;
    while (k < 6U && k < blen) {
        out[k] = (uint8_t) base[k];
        k++;
    }
    out[k++] = (uint8_t) '~';
    if (num == 0U) {
        numbuf[nl++] = '0';
    }
    while (num > 0U && nl < sizeof(numbuf)) {
        numbuf[nl++] = (char) ('0' + (num % 10U));
        num /= 10U;
    }
    for (uint32_t i = 0U; i < nl / 2U; i++) {
        char t = numbuf[i];
        numbuf[i] = numbuf[nl - 1U - i];
        numbuf[nl - 1U - i] = t;
    }
    for (uint32_t i = 0U; i < nl && k < 8U; i++) {
        out[k++] = (uint8_t) numbuf[i];
    }
    for (uint32_t i = 0U; i < elen && i < 3U; i++) {
        out[8U + i] = (uint8_t) ext[i];
    }
}

/* Create a new directory entry (LFN chain + short entry) for `long_name` in
 * `dir`. Returns the short-entry slot in *short_slot_out; the caller fills in
 * attr / first cluster / size and calls fat32_write_slot. */
static bool fat32_create_named_entry(fat32_dir_ref_t *dir, const char *long_name,
                                     fat32_dir_slot_t *short_slot_out)
{
    fat32_dir_slot_t slots[FAT32_LFN_MAX_ENTRIES + 1U];

    if (!fat32_name_needs_lfn(long_name)) {
        if (!fat32_find_free_slot(dir, short_slot_out)) {
            return false;
        }
        fat32_format_component((char *) short_slot_out->entry.name, long_name);
        return true;
    }

    {
        uint8_t short11[11];
        uint8_t chksum;
        uint32_t namelen = (uint32_t) strlen(long_name);
        uint32_t lfn_count;
        bool got = false;

        if (namelen > FAT32_NAME_MAX) {
            namelen = FAT32_NAME_MAX;
        }

        /* Resolve an unused 8.3 short name. */
        for (uint32_t num = 1U; num < 100000U; num++) {
            fat32_dir_slot_t probe;
            char dec[13];

            fat32_lfn_make_short(long_name, num, short11);
            fat32_decode_name(dec, short11);
            if (!fat32_find_entry_in_dir(dir, dec, &probe)) {
                got = true;
                break;
            }
        }
        if (!got) {
            return false;
        }

        lfn_count = (namelen + FAT32_LFN_CHARS_PER_ENTRY - 1U) / FAT32_LFN_CHARS_PER_ENTRY;
        if (lfn_count == 0U) {
            lfn_count = 1U;
        }
        if (lfn_count > FAT32_LFN_MAX_ENTRIES) {
            lfn_count = FAT32_LFN_MAX_ENTRIES;
        }

        if (!fat32_alloc_append_slots(dir, lfn_count + 1U, slots)) {
            return false;
        }

        chksum = fat32_lfn_checksum(short11);

        /* Physical order: slots[0] holds the LAST block (ordinal|0x40),
         * slots[lfn_count-1] holds block 1, slots[lfn_count] is the short entry. */
        for (uint32_t k = 0; k < lfn_count; k++) {
            uint32_t ord = lfn_count - k;
            fat32_dir_entry_t *e = &slots[k].entry;
            bool passed_end = false;

            memset(e, 0, sizeof(*e));
            e->name[0] = (uint8_t) ord;
            if (ord == lfn_count) {
                e->name[0] |= FAT32_ATTR_LFN_LAST;
            }
            e->attr = FAT32_ATTR_LFN;
            e->create_time_tenth = chksum;
            for (uint32_t idx = 0; idx < FAT32_LFN_CHARS_PER_ENTRY; idx++) {
                uint32_t gi = (ord - 1U) * FAT32_LFN_CHARS_PER_ENTRY + idx;
                uint16_t ch;
                if (gi < namelen) {
                    ch = (uint16_t) (uint8_t) long_name[gi];
                } else if (!passed_end) {
                    ch = 0x0000U;
                    passed_end = true;
                } else {
                    ch = 0xFFFFU;
                }
                fat32_lfn_set_char(e, idx, ch);
            }
            fat32_write_slot(dir, &slots[k]);
        }

        memcpy(slots[lfn_count].entry.name, short11, 11);
        *short_slot_out = slots[lfn_count];
    }
    return true;
}

static bool fat32_split_path_component(const char **path_ptr, char component[FAT32_COMP_NAME_MAX])
{
    uint32_t i = 0;
    const char *path = *path_ptr;

    while (*path == '/') {
        path++;
    }
    if (*path == '\0') {
        *path_ptr = path;
        component[0] = '\0';
        return false;
    }

    while (*path != '\0' && *path != '/' && i < FAT32_NAME_MAX) {
        component[i++] = *path++;
    }
    component[i] = '\0';
    while (*path == '/') {
        path++;
    }
    *path_ptr = path;
    return true;
}

static bool fat32_resolve_parent(const char *path, fat32_dir_ref_t *parent_out, char final_component[FAT32_COMP_NAME_MAX])
{
    fat32_dir_ref_t dir;
    const char *cursor = path;
    char component[FAT32_COMP_NAME_MAX];
    char next_component[FAT32_COMP_NAME_MAX];
    fat32_dir_slot_t slot;

    dir.is_root = true;
    dir.start_cluster = g_bpb.root_cluster;

    if (!fat32_split_path_component(&cursor, component)) {
        final_component[0] = '\0';
        *parent_out = dir;
        return true;
    }

    while (1) {
        const char *saved = cursor;
        if (!fat32_split_path_component(&saved, next_component)) {
            strlcpy(final_component, component, FAT32_COMP_NAME_MAX);
            *parent_out = dir;
            return true;
        }

        if (!fat32_find_entry_in_dir(&dir, component, &slot)) {
            return false;
        }
        if ((slot.entry.attr & FAT32_ATTR_DIRECTORY) == 0) {
            return false;
        }
        dir.is_root = false;
        dir.start_cluster = fat32_entry_cluster(&slot.entry);
        strlcpy(component, next_component, FAT32_COMP_NAME_MAX);
        cursor = saved;
    }
}

static bool fat32_resolve_path(const char *path, fat32_dir_ref_t *parent_out, fat32_dir_slot_t *slot_out, char final_component[FAT32_COMP_NAME_MAX])
{
    if (!fat32_resolve_parent(path, parent_out, final_component)) {
        return false;
    }
    if (final_component[0] == '\0') {
        slot_out->valid = false;
        return true;
    }
    return fat32_find_entry_in_dir(parent_out, final_component, slot_out);
}

static void fat32_set_entry_cluster(fat32_dir_entry_t *entry, uint32_t cluster)
{
    entry->first_cluster_low = (uint16_t) (cluster & 0xFFFF);
    entry->first_cluster_high = (uint16_t) ((cluster >> 16) & 0xFFFF);
}

static uint32_t fat32_entry_cluster(const fat32_dir_entry_t *entry)
{
    return ((uint32_t) entry->first_cluster_high << 16) | entry->first_cluster_low;
}

static void fat32_init_dot_entries(fat32_dir_ref_t *new_dir, uint32_t self_cluster, uint32_t parent_cluster)
{
    uint8_t sector[512];
    fat32_dir_entry_t *entries = (fat32_dir_entry_t *) sector;

    memset(sector, 0, sizeof(sector));
    memset(entries[0].name, ' ', 11);
    entries[0].name[0] = '.';
    entries[0].attr = FAT32_ATTR_DIRECTORY;
    fat32_set_entry_cluster(&entries[0], self_cluster);

    memset(entries[1].name, ' ', 11);
    entries[1].name[0] = '.';
    entries[1].name[1] = '.';
    entries[1].attr = FAT32_ATTR_DIRECTORY;
    fat32_set_entry_cluster(&entries[1], parent_cluster);

    fat32_write_dir_sector(new_dir, 0, sector);
}

bool fat32_init(void)
{
    uint8_t sector[512];
    uint32_t data_sectors;
    uint32_t data_start;
    int32_t mount_hint = file_mount_partition_hint();
    bool read_ok;

    /* 卷在哪块设备上由 lib/file.c 通过 file_blockdev_hint() 告知。拿到就把
     * 所有 I/O 交给块设备抽象（AHCI/NVMe/virtio 都能用）；拿不到才回退到
     * legacy ATA PIO。 */
    g_fat32_blockdev = file_blockdev_hint();
    g_fat32_write_warned = false;

    if (mount_hint >= 0) {
        g_volume_lba = (uint32_t) mount_hint;
        read_ok = ata_read_sector(g_volume_lba, sector);
    } else {
        read_ok = ata_read_sector(0, sector);
        g_volume_lba = 0;
    }
    memcpy(&g_bpb, sector, sizeof(g_bpb));

    /* 挂载失败时把判据全部打出来：ATA 读是否成功、BPB 关键字段是什么、
    * 是否通过有效性检查。缺了这些，开机只能看到一句 "mount failed"。 */
    kernel_log_hex_u32("fat32: mount hint ", (uint32_t) mount_hint);
    kernel_log_hex_u32("fat32: blockdev index ", (uint32_t) g_fat32_blockdev);
    log_write_bool_event("fat32: ata read ok", read_ok);
    log_write_bool_event("fat32: bpb valid", fat32_bpb_is_valid(&g_bpb));
    kernel_log_hex_u32("fat32: bps ", g_bpb.bytes_per_sector);
    kernel_log_hex_u32("fat32: sec_per_clus ", g_bpb.sectors_per_cluster);
    kernel_log_hex_u32("fat32: rsvd_sec ", g_bpb.reserved_sector_count);
    kernel_log_hex_u32("fat32: num_fats ", g_bpb.fat_count);
    kernel_log_hex_u32("fat32: fat_size_16 ", g_bpb.fat_size_16);
    kernel_log_hex_u32("fat32: fat_size_32 ", g_bpb.fat_size_32);
    kernel_log_hex_u32("fat32: root_cluster ", g_bpb.root_cluster);
    kernel_log_hex_u32("fat32: hidden_sec ", g_bpb.hidden_sectors);
    kernel_log_hex_u32("fat32: total_sec32 ", g_bpb.total_sectors_32);

    if (!fat32_bpb_is_valid(&g_bpb)) {
        if (mount_hint >= 0) {
            log_write("fat32: bpb rejected (explicit partition hint)");
            g_fat32_ready = false;
            return false;
        }
        if (read_le16(sector + 510) != 0xAA55) {
            log_write("fat32: bpb rejected (missing 0xAA55 and bad BPB)");
            g_fat32_ready = false;
            return false;
        }

        for (uint8_t i = 0; i < 4; i++) {
            uint8_t *entry = sector + 446 + (uint32_t) i * 16;
            if (fat32_partition_type(entry[4]) && read_le32(entry + 12) != 0) {
                g_volume_lba = read_le32(entry + 8);
                ata_read_sector(g_volume_lba, sector);
                memcpy(&g_bpb, sector, sizeof(g_bpb));
                break;
            }
        }

        if (!fat32_bpb_is_valid(&g_bpb)) {
            log_write("fat32: bpb rejected (no usable partition found)");
            g_fat32_ready = false;
            return false;
        }
    } else if (g_bpb.hidden_sectors != 0) {
        uint8_t volume_sector[512];
        fat32_bpb_t volume_bpb;

        ata_read_sector(g_bpb.hidden_sectors, volume_sector);
        memcpy(&volume_bpb, volume_sector, sizeof(volume_bpb));
        if (fat32_bpb_is_valid(&volume_bpb)) {
            memcpy(sector, volume_sector, sizeof(sector));
            memcpy(&g_bpb, &volume_bpb, sizeof(g_bpb));
            g_volume_lba = g_bpb.hidden_sectors;
        }
    }

    g_fat_lba = g_volume_lba + g_bpb.reserved_sector_count;
    g_root_sectors = g_bpb.sectors_per_cluster;
    data_start = (uint32_t) g_bpb.reserved_sector_count + (uint32_t) g_bpb.fat_count * g_bpb.fat_size_32;
    g_data_lba = g_volume_lba + data_start;
    g_root_lba = fat32_cluster_to_lba(g_bpb.root_cluster);
    if (fat32_total_sectors() <= data_start) {
        log_write("fat32: bpb rejected (volume smaller than data area)");
        g_fat32_ready = false;
        return false;
    }

    data_sectors = fat32_total_sectors() - data_start;

    /* Carve the tail of the volume out for the write-ahead log so that no
     * data cluster can ever be allocated over it. If the volume is too small
     * to spare the space, WAL stays off and behaviour is unchanged. */
    g_wal_on = false;
    g_wal_max = 0;
    g_wal_next = 0;
    g_wal_seq = 0;
    g_wal_dirty_boot = false;
    if (data_sectors > FAT32_WAL_SECTORS + 32U) {
        g_wal_log_lba = g_volume_lba + fat32_total_sectors() - FAT32_WAL_SECTORS;
        g_wal_max = (FAT32_WAL_SECTORS - 1U) / FAT32_WAL_REC_SECTORS;
        data_sectors -= FAT32_WAL_SECTORS;
        wal_recover();
        g_wal_on = true;
    } else {
        g_wal_log_lba = 0;
    }

    g_cluster_count = data_sectors / g_bpb.sectors_per_cluster;
    fat32_clear_read_cache();
    fat32_clear_fat_cache();
    g_fat32_ready = true;
    kernel_log_hex_u32("fat32: volume mounted, clusters ", g_cluster_count);
    return true;
}

uint16_t fat32_root_entry_count(void)
{
    char buffer[4096];
    uint16_t count = 0;

    if (!fat32_list_root(buffer, sizeof(buffer))) {
        return 0;
    }

    for (uint32_t i = 0; buffer[i] != '\0'; i++) {
        if (buffer[i] == '\n') {
            count++;
        }
    }
    return count;
}

bool fat32_exists(const char *path)
{
    fat32_dir_ref_t parent;
    fat32_dir_slot_t slot;
    char final_component[FAT32_COMP_NAME_MAX];

    if (!g_fat32_ready) {
        return false;
    }
    return fat32_resolve_path(path, &parent, &slot, final_component) && slot.valid;
}

bool fat32_is_dir(const char *path)
{
    fat32_dir_ref_t parent;
    fat32_dir_slot_t slot;
    char final_component[FAT32_COMP_NAME_MAX];

    if (!g_fat32_ready) {
        return false;
    }
    if (path == NULL || path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        return true;
    }
    if (!fat32_resolve_path(path, &parent, &slot, final_component) || !slot.valid) {
        return false;
    }
    return (slot.entry.attr & FAT32_ATTR_DIRECTORY) != 0;
}

bool fat32_stat(const char *path, fat32_stat_t *stat_out)
{
    fat32_dir_ref_t parent;
    fat32_dir_slot_t slot;
    char final_component[FAT32_COMP_NAME_MAX];

    if (!g_fat32_ready || stat_out == NULL) {
        return false;
    }
    if (!fat32_resolve_path(path, &parent, &slot, final_component) || !slot.valid) {
        return false;
    }

    memset(stat_out, 0, sizeof(*stat_out));
    stat_out->file_size = slot.entry.file_size;
    stat_out->attr = slot.entry.attr;
    stat_out->is_dir = (slot.entry.attr & FAT32_ATTR_DIRECTORY) != 0;
    stat_out->create_date = slot.entry.create_date;
    stat_out->create_time = slot.entry.create_time;
    stat_out->write_date = slot.entry.write_date;
    stat_out->write_time = slot.entry.write_time;
    stat_out->access_date = slot.entry.last_access_date;

    return true;
}

bool fat32_set_attr(const char *path, uint8_t attr_mask, uint8_t attr_value)
{
    fat32_dir_ref_t parent;
    fat32_dir_slot_t slot;
    char final_component[FAT32_COMP_NAME_MAX];

    if (!g_fat32_ready || path == NULL) {
        return false;
    }
    if (!fat32_resolve_path(path, &parent, &slot, final_component) || !slot.valid) {
        return false;
    }
    fat32_clear_read_cache();
    slot.entry.attr = (uint8_t) ((slot.entry.attr & ~attr_mask) | (attr_value & attr_mask));
    return fat32_write_slot(&parent, &slot);
}

int32_t fat32_file_size(const char *path)
{
    fat32_dir_ref_t parent;
    fat32_dir_slot_t slot;
    char final_component[FAT32_COMP_NAME_MAX];

    if (!g_fat32_ready || !fat32_resolve_path(path, &parent, &slot, final_component) || !slot.valid) {
        return -1;
    }
    if ((slot.entry.attr & FAT32_ATTR_DIRECTORY) != 0) {
        return -1;
    }
    return (int32_t) slot.entry.file_size;
}

int32_t fat32_read_file(const char *path, void *buffer, uint32_t buffer_size)
{
    fat32_dir_ref_t parent;
    fat32_dir_slot_t slot;
    char final_component[FAT32_COMP_NAME_MAX];
    uint32_t cluster;
    uint32_t bytes_left;
    uint8_t *dst = (uint8_t *) buffer;

    if (!g_fat32_ready || !fat32_resolve_path(path, &parent, &slot, final_component) || !slot.valid) {
        return -1;
    }
    if ((slot.entry.attr & FAT32_ATTR_DIRECTORY) != 0) {
        return -3;
    }

    cluster = fat32_entry_cluster(&slot.entry);
    bytes_left = slot.entry.file_size;
    while (cluster >= 2 && cluster < g_cluster_count + 2 && bytes_left > 0) {
        for (uint8_t sector_index = 0; sector_index < g_bpb.sectors_per_cluster && bytes_left > 0; sector_index++) {
            uint8_t sector[512];
            uint32_t chunk = bytes_left > 512 ? 512 : bytes_left;

            if ((uint32_t) (dst - (uint8_t *) buffer) + chunk > buffer_size) {
                return -2;
            }

            ata_read_sector(fat32_cluster_to_lba(cluster) + sector_index, sector);
            memcpy(dst, sector, chunk);
            dst += chunk;
            bytes_left -= chunk;
        }
        cluster = fat32_get_fat_entry(cluster);
        if (fat32_is_end_cluster(cluster)) {
            break;
        }
    }

    return (int32_t) slot.entry.file_size;
}

int32_t fat32_read_file_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size)
{
    fat32_dir_ref_t parent;
    fat32_dir_slot_t slot;
    char final_component[FAT32_COMP_NAME_MAX];
    uint32_t cluster;
    uint32_t first_cluster;
    uint32_t file_size;
    uint32_t cluster_bytes;
    uint32_t target_cluster_index;
    uint32_t walk_clusters;
    uint32_t offset_in_cluster;
    uint32_t bytes_left;
    uint8_t *dst = (uint8_t *) buffer;
    uint32_t bytes_read = 0;

    if (!g_fat32_ready || buffer == NULL || !fat32_resolve_path(path, &parent, &slot, final_component) || !slot.valid) {
        return -1;
    }
    if ((slot.entry.attr & FAT32_ATTR_DIRECTORY) != 0) {
        return -3;
    }

    file_size = slot.entry.file_size;
    if (offset >= file_size || buffer_size == 0) {
        return 0;
    }
    bytes_left = file_size - offset;
    if (bytes_left > buffer_size) {
        bytes_left = buffer_size;
    }

    first_cluster = fat32_entry_cluster(&slot.entry);
    cluster = first_cluster;
    cluster_bytes = (uint32_t) g_bpb.sectors_per_cluster * 512u;
    target_cluster_index = offset / cluster_bytes;
    offset_in_cluster = offset % cluster_bytes;

    if (g_fat32_read_cache_valid &&
        strcmp(g_fat32_read_cache_path, path) == 0 &&
        g_fat32_read_cache_start_cluster == first_cluster &&
        g_fat32_read_cache_cluster >= 2 &&
        g_fat32_read_cache_cluster < g_cluster_count + 2 &&
        g_fat32_read_cache_cluster_index <= target_cluster_index) {
        cluster = g_fat32_read_cache_cluster;
        walk_clusters = target_cluster_index - g_fat32_read_cache_cluster_index;
    } else {
        walk_clusters = target_cluster_index;
    }

    while (walk_clusters > 0 && cluster >= 2 && cluster < g_cluster_count + 2) {
        cluster = fat32_get_fat_entry(cluster);
        walk_clusters--;
        if (fat32_is_end_cluster(cluster)) {
            return (int32_t) bytes_read;
        }
    }

    strlcpy(g_fat32_read_cache_path, path, sizeof(g_fat32_read_cache_path));
    g_fat32_read_cache_valid = strlen(path) < sizeof(g_fat32_read_cache_path);
    g_fat32_read_cache_start_cluster = first_cluster;
    g_fat32_read_cache_cluster_index = target_cluster_index;
    g_fat32_read_cache_cluster = cluster;

    while (cluster >= 2 && cluster < g_cluster_count + 2 && bytes_left > 0) {
        if (offset_in_cluster == 0 && bytes_left >= cluster_bytes) {
            uint32_t contiguous_clusters = 1;
            uint32_t probe_cluster = cluster;
            uint32_t max_contiguous_clusters = bytes_left / cluster_bytes;
            uint32_t max_command_clusters = 255u / g_bpb.sectors_per_cluster;

            if (max_command_clusters == 0) {
                max_command_clusters = 1;
            }
            if (max_contiguous_clusters > max_command_clusters) {
                max_contiguous_clusters = max_command_clusters;
            }
            while (contiguous_clusters < max_contiguous_clusters) {
                uint32_t next_cluster = fat32_get_fat_entry(probe_cluster);

                if (next_cluster != probe_cluster + 1u) {
                    break;
                }
                probe_cluster = next_cluster;
                contiguous_clusters++;
            }
            if (contiguous_clusters > 1u) {
                uint32_t sectors = contiguous_clusters * (uint32_t) g_bpb.sectors_per_cluster;
                uint32_t bytes = sectors * 512u;
                uint32_t next_cluster;

                ata_read_sectors(fat32_cluster_to_lba(cluster), (uint8_t) sectors, dst);
                dst += bytes;
                bytes_read += bytes;
                bytes_left -= bytes;
                g_fat32_read_cache_cluster_index += contiguous_clusters - 1u;
                g_fat32_read_cache_cluster = probe_cluster;
                if (bytes_left == 0) {
                    break;
                }
                next_cluster = fat32_get_fat_entry(probe_cluster);
                if (fat32_is_end_cluster(next_cluster)) {
                    break;
                }
                cluster = next_cluster;
                g_fat32_read_cache_cluster_index++;
                g_fat32_read_cache_cluster = cluster;
                continue;
            }
        }
        for (uint8_t sector_index = 0; sector_index < g_bpb.sectors_per_cluster && bytes_left > 0; sector_index++) {
            uint8_t sector[512];
            uint32_t sector_offset = (uint32_t) sector_index * 512u;
            uint32_t start = 0;
            uint32_t chunk;
            uint32_t whole_sectors;

            if (offset_in_cluster >= sector_offset + 512u) {
                continue;
            }
            if (offset_in_cluster > sector_offset) {
                start = offset_in_cluster - sector_offset;
            }
            if (start == 0 && bytes_left >= 512u) {
                whole_sectors = bytes_left / 512u;
                if (whole_sectors > (uint32_t) g_bpb.sectors_per_cluster - sector_index) {
                    whole_sectors = (uint32_t) g_bpb.sectors_per_cluster - sector_index;
                }
                if (whole_sectors > 255u) {
                    whole_sectors = 255u;
                }
                if (whole_sectors > 1u) {
                    ata_read_sectors(fat32_cluster_to_lba(cluster) + sector_index, (uint8_t) whole_sectors, dst);
                    dst += whole_sectors * 512u;
                    bytes_read += whole_sectors * 512u;
                    bytes_left -= whole_sectors * 512u;
                    sector_index = (uint8_t) (sector_index + whole_sectors - 1u);
                    continue;
                }
            }
            chunk = 512u - start;
            if (chunk > bytes_left) {
                chunk = bytes_left;
            }
            ata_read_sector(fat32_cluster_to_lba(cluster) + sector_index, sector);
            memcpy(dst, sector + start, chunk);
            dst += chunk;
            bytes_read += chunk;
            bytes_left -= chunk;
        }
        offset_in_cluster = 0;
        if (bytes_left == 0) {
            break;
        }
        cluster = fat32_get_fat_entry(cluster);
        if (fat32_is_end_cluster(cluster)) {
            break;
        }
        g_fat32_read_cache_cluster_index++;
        g_fat32_read_cache_cluster = cluster;
    }

    return (int32_t) bytes_read;
}

int32_t fat32_write_file(const char *path, const void *buffer, uint32_t size)
{
    fat32_dir_ref_t parent;
    fat32_dir_slot_t slot;
    char final_component[FAT32_COMP_NAME_MAX];
    uint32_t first_cluster = 0;
    uint32_t previous_cluster = 0;
    uint32_t remaining = size;
    const uint8_t *src = (const uint8_t *) buffer;

    if (!g_fat32_ready || !fat32_resolve_parent(path, &parent, final_component) || final_component[0] == '\0') {
        return -1;
    }
    fat32_clear_read_cache();

    if (!fat32_find_entry_in_dir(&parent, final_component, &slot)) {
        if (!fat32_create_named_entry(&parent, final_component, &slot)) {
            return -2;
        }
    } else if ((slot.entry.attr & FAT32_ATTR_DIRECTORY) != 0) {
        return -3;
    } else if (fat32_entry_cluster(&slot.entry) >= 2) {
        fat32_free_chain(fat32_entry_cluster(&slot.entry));
    }

    while (remaining > 0) {
        uint32_t cluster = fat32_allocate_cluster();
        uint32_t sector_index = 0;

        if (cluster == 0) {
            if (first_cluster != 0) {
                fat32_free_chain(first_cluster);
            }
            return -4;
        }
        if (first_cluster == 0) {
            first_cluster = cluster;
        }
        if (previous_cluster != 0) {
            fat32_set_fat_entry(previous_cluster, cluster);
        }
        fat32_set_fat_entry(cluster, FAT32_CLUSTER_END);
        previous_cluster = cluster;

        while (sector_index < g_bpb.sectors_per_cluster) {
            uint32_t batch = (uint32_t) g_bpb.sectors_per_cluster - sector_index;
            uint32_t batch_bytes;

            if (batch > 8u) {
                batch = 8u;
            }
            batch_bytes = batch * 512u;
            memset(g_fat32_write_buffer, 0, batch_bytes);
            if (remaining > 0) {
                uint32_t chunk = remaining < batch_bytes ? remaining : batch_bytes;
                memcpy(g_fat32_write_buffer, src, chunk);
                src += chunk;
                remaining -= chunk;
            }
            ata_write_sectors(fat32_cluster_to_lba(cluster) + sector_index,
                              (uint8_t) batch,
                              g_fat32_write_buffer);
            sector_index += batch;
        }
    }

    slot.entry.attr = 0;
    fat32_set_entry_cluster(&slot.entry, first_cluster);
    slot.entry.file_size = size;
    if (!fat32_write_slot(&parent, &slot)) {
        return -5;
    }
    return (int32_t) size;
}

bool fat32_delete(const char *path)
{
    fat32_dir_ref_t parent;
    fat32_dir_slot_t slot;
    char final_component[FAT32_COMP_NAME_MAX];

    if (!g_fat32_ready || !fat32_resolve_path(path, &parent, &slot, final_component) || !slot.valid) {
        return false;
    }
    fat32_clear_read_cache();
    if ((slot.entry.attr & FAT32_ATTR_DIRECTORY) != 0) {
        return false;
    }
    if (fat32_entry_cluster(&slot.entry) >= 2) {
        fat32_free_chain(fat32_entry_cluster(&slot.entry));
    }
    slot.entry.name[0] = 0xE5;
    return fat32_write_slot(&parent, &slot);
}

bool fat32_mkdir(const char *path)
{
    fat32_dir_ref_t parent;
    fat32_dir_ref_t new_dir;
    fat32_dir_slot_t slot;
    char final_component[FAT32_COMP_NAME_MAX];
    uint32_t new_cluster;
    uint32_t parent_cluster;

    if (!g_fat32_ready || !fat32_resolve_parent(path, &parent, final_component) || final_component[0] == '\0') {
        return false;
    }
    if (fat32_find_entry_in_dir(&parent, final_component, &slot)) {
        return false;
    }

    new_cluster = fat32_allocate_cluster();
    if (new_cluster == 0) {
        return false;
    }
    fat32_zero_cluster(new_cluster);

    if (!fat32_create_named_entry(&parent, final_component, &slot)) {
        fat32_free_chain(new_cluster);
        return false;
    }
    slot.entry.attr = FAT32_ATTR_DIRECTORY;
    fat32_set_entry_cluster(&slot.entry, new_cluster);
    slot.entry.file_size = 0;
    if (!fat32_write_slot(&parent, &slot)) {
        fat32_free_chain(new_cluster);
        return false;
    }

    new_dir.is_root = false;
    new_dir.start_cluster = new_cluster;
    parent_cluster = parent.is_root ? 0 : parent.start_cluster;
    fat32_init_dot_entries(&new_dir, new_cluster, parent_cluster);
    return true;
}

bool fat32_rmdir(const char *path)
{
    fat32_dir_ref_t parent;
    fat32_dir_slot_t slot;
    char final_component[FAT32_COMP_NAME_MAX];
    fat32_dir_ref_t dir;
    uint8_t sector[512];

    if (!g_fat32_ready || !fat32_resolve_path(path, &parent, &slot, final_component) || !slot.valid) {
        return false;
    }
    if ((slot.entry.attr & FAT32_ATTR_DIRECTORY) == 0) {
        return false;
    }

    dir.is_root = false;
    dir.start_cluster = fat32_entry_cluster(&slot.entry);
    for (uint32_t sector_index = 0; fat32_read_dir_sector(&dir, sector_index, sector, NULL); sector_index++) {
        for (uint32_t i = 0; i < 512 / sizeof(fat32_dir_entry_t); i++) {
            fat32_dir_entry_t *entry = ((fat32_dir_entry_t *) sector) + i;
            if (entry->name[0] == 0x00) {
                goto empty_done;
            }
            if (entry->name[0] == 0xE5 || entry->attr == FAT32_ATTR_LFN) {
                continue;
            }
            if (entry->name[0] == '.' && (entry->name[1] == ' ' || entry->name[1] == '.')) {
                continue;
            }
            return false;
        }
    }
empty_done:
    fat32_free_chain(fat32_entry_cluster(&slot.entry));
    slot.entry.name[0] = 0xE5;
    return fat32_write_slot(&parent, &slot);
}

bool fat32_rename(const char *oldpath, const char *newpath)
{
    fat32_dir_ref_t old_parent, new_parent;
    fat32_dir_item_t old_item;
    char old_component[FAT32_COMP_NAME_MAX];
    char new_component[FAT32_COMP_NAME_MAX];
    fat32_dir_slot_t existing;
    fat32_dir_slot_t new_slot;
    fat32_dir_entry_t old_e;
    bool is_dir;
    uint32_t old_cluster;

    if (!g_fat32_ready || oldpath == NULL || newpath == NULL) {
        return false;
    }

    /* Resolve the source entry (with its LFN chain). */
    if (!fat32_resolve_parent(oldpath, &old_parent, old_component) || old_component[0] == '\0') {
        return false;
    }
    if (!fat32_find_item_in_dir(&old_parent, old_component, &old_item)) {
        return false;
    }

    /* Resolve the destination parent directory. */
    if (!fat32_resolve_parent(newpath, &new_parent, new_component) || new_component[0] == '\0') {
        return false;
    }

    /* Never overwrite an existing destination. */
    if (fat32_find_entry_in_dir(&new_parent, new_component, &existing)) {
        return false;
    }

    is_dir = (old_item.entry.attr & FAT32_ATTR_DIRECTORY) != 0;
    old_cluster = fat32_entry_cluster(&old_item.entry);

    /* Never move a directory into itself. */
    if (is_dir && !new_parent.is_root && new_parent.start_cluster == old_cluster) {
        return false;
    }

    fat32_clear_read_cache();
    old_e = old_item.entry;

    /* Create the new entry in the destination first (LFN chain included), so
     * that a failure leaves the source untouched. */
    if (!fat32_create_named_entry(&new_parent, new_component, &new_slot)) {
        return false;
    }

    /* Copy over all metadata; stamp a fresh write time. */
    new_slot.entry.attr = old_e.attr;
    fat32_set_entry_cluster(&new_slot.entry, fat32_entry_cluster(&old_e));
    new_slot.entry.file_size = old_e.file_size;
    new_slot.entry.create_time = old_e.create_time;
    new_slot.entry.create_date = old_e.create_date;
    new_slot.entry.last_access_date = old_e.last_access_date;
    new_slot.entry.create_time_tenth = old_e.create_time_tenth;
    new_slot.entry.write_date = 0x5462;  /* 2024-01-01 placeholder */
    new_slot.entry.write_time = 0x6000;  /* 12:00:00 placeholder */
    fat32_write_slot(&new_parent, &new_slot);

    /* Remove the source entry and its LFN chain. */
    fat32_delete_item_slots(&old_item);

    /* Moving a directory to a new parent: fix its internal ".." entry. */
    if (is_dir) {
        bool same_dir = (old_parent.is_root == new_parent.is_root) &&
                        (old_parent.is_root || old_parent.start_cluster == new_parent.start_cluster);
        if (!same_dir) {
            fat32_dir_ref_t moved;
            uint8_t sector[512];
            uint32_t new_parent_cluster = new_parent.is_root ? 0U : new_parent.start_cluster;

            moved.is_root = false;
            moved.start_cluster = old_cluster;
            if (fat32_read_dir_sector(&moved, 0U, sector, NULL)) {
                fat32_dir_entry_t *dotdot = ((fat32_dir_entry_t *) sector) + 1U;
                if (dotdot->name[0] == '.' && dotdot->name[1] == '.') {
                    fat32_set_entry_cluster(dotdot, new_parent_cluster);
                    fat32_write_dir_sector(&moved, 0U, sector);
                }
            }
        }
    }
    return true;
}

bool fat32_list_dir(const char *path, char *buffer, uint32_t buffer_size)
{
    fat32_dir_ref_t dir;
    fat32_dir_ref_t parent;
    fat32_dir_slot_t slot;
    char final_component[FAT32_COMP_NAME_MAX];
    fat32_dir_cursor_t cur;
    fat32_dir_item_t item;
    uint32_t used = 0;

    if (!g_fat32_ready || buffer_size == 0) {
        return false;
    }

    if (path == NULL || path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        dir.is_root = true;
        dir.start_cluster = g_bpb.root_cluster;
    } else {
        if (!fat32_resolve_path(path, &parent, &slot, final_component) || !slot.valid) {
            return false;
        }
        if ((slot.entry.attr & FAT32_ATTR_DIRECTORY) == 0) {
            return false;
        }
        dir.is_root = false;
        dir.start_cluster = fat32_entry_cluster(&slot.entry);
    }

    buffer[0] = '\0';
    fat32_cursor_init(&cur, &dir);
    while (fat32_cursor_next(&cur, &item)) {
        const char *p = item.long_name;

        if (item.entry.name[0] == '.' &&
            (item.entry.name[1] == ' ' || item.entry.name[1] == '.')) {
            continue;
        }
        while (*p != '\0') {
            if (used + 2U >= buffer_size) {
                return false;
            }
            buffer[used++] = *p++;
        }
        if (item.entry.attr & FAT32_ATTR_DIRECTORY) {
            if (used + 2U >= buffer_size) {
                return false;
            }
            buffer[used++] = '/';
        }
        buffer[used++] = '\n';
        buffer[used] = '\0';
    }
    return true;
}

bool fat32_list_root(char *buffer, uint32_t buffer_size)
{
    return fat32_list_dir("/", buffer, buffer_size);
}

/* 磁盘空间查询 */
uint64_t fat32_total_bytes(uint64_t *free_out, uint32_t *cluster_size_out)
{
    uint32_t i;
    uint32_t free_clusters = 0;
    uint32_t total_clusters = g_cluster_count;
    uint32_t cluster_sectors = g_bpb.sectors_per_cluster;
    uint32_t sector_bytes = g_bpb.bytes_per_sector;
    uint64_t cluster_bytes = (uint64_t)cluster_sectors * sector_bytes;

    if (!g_fat32_ready || total_clusters == 0) {
        return 0;
    }

    for (i = 2; i < total_clusters + 2; i++) {
        if (fat32_get_fat_entry(i) == FAT32_CLUSTER_FREE) {
            free_clusters++;
        }
    }

    if (cluster_size_out != NULL) {
        *cluster_size_out = (uint32_t)cluster_bytes;
    }
    if (free_out != NULL) {
        *free_out = (uint64_t)free_clusters * cluster_bytes;
    }
    return (uint64_t)total_clusters * cluster_bytes;
}

/* ============================================================
 *  FAT32 格式化
 * ============================================================ */

static void fat32_write_le16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t) (value & 0xFFU);
    data[1] = (uint8_t) ((value >> 8) & 0xFFU);
}

static void fat32_write_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t) (value & 0xFFU);
    data[1] = (uint8_t) ((value >> 8) & 0xFFU);
    data[2] = (uint8_t) ((value >> 16) & 0xFFU);
    data[3] = (uint8_t) ((value >> 24) & 0xFFU);
}

bool fat32_format(uint32_t volume_lba, uint64_t sector_count, uint32_t sector_size)
{
    uint8_t boot[512];
    uint8_t zero[512];
    uint64_t volume_bytes;
    uint32_t spc;
    uint32_t reserved = 32U;
    uint32_t fat_count = 2U;
    uint32_t fat_size = 1U;
    uint32_t data_start;
    uint32_t data_sectors;
    uint32_t cluster_count;
    uint32_t i;
    uint32_t fat_lba;
    uint32_t data_lba;

    /* 当前 PIO 写路径按 512 字节扇区实现 */
    if (sector_size != 512U || sector_count < reserved + fat_count + 2U) {
        return false;
    }

    volume_bytes = sector_count * (uint64_t) sector_size;
    if (volume_bytes < (uint64_t) 256U * 1024U * 1024U) {
        spc = 8U;
    } else if (volume_bytes <= (uint64_t) 2U * 1024U * 1024U * 1024U) {
        spc = 16U;
    } else {
        spc = 32U;
    }

    /* 迭代求解每个 FAT 需要的扇区数（FAT 大小影响数据区大小，反之亦然） */
    for (i = 0; i < 16U; i++) {
        uint32_t new_fat_size;
        if (sector_count <= reserved + (uint64_t) fat_count * fat_size) {
            return false;
        }
        data_sectors = (uint32_t) sector_count - reserved - fat_count * fat_size;
        cluster_count = data_sectors / spc;
        /* 需要的簇项数 = 数据簇 + 保留簇0/1 */
        new_fat_size = ((cluster_count + 2U) * 4U + sector_size - 1U) / sector_size;
        if (new_fat_size == fat_size) {
            break;
        }
        fat_size = new_fat_size;
    }
    if (sector_count <= (uint64_t) reserved + (uint64_t) fat_count * fat_size) {
        return false;
    }
    data_start = reserved + fat_count * fat_size;
    data_sectors = (uint32_t) sector_count - data_start;
    cluster_count = data_sectors / spc;
    if (cluster_count < 1U) {
        return false;
    }

    memset(boot, 0, sizeof(boot));
    memset(zero, 0, sizeof(zero));

    /* 引导扇区 / BPB */
    boot[0] = 0xEB;
    boot[1] = 0x58;
    boot[2] = 0x90;
    memcpy(boot + 3, "MSDOS5.0", 8);
    fat32_write_le16(boot + 11, 512U);          /* bytes per sector */
    boot[13] = (uint8_t) spc;                    /* sectors per cluster */
    fat32_write_le16(boot + 14, reserved);       /* reserved sectors */
    boot[16] = (uint8_t) fat_count;               /* number of FATs */
    fat32_write_le16(boot + 17, 0U);            /* root entries (0 for FAT32) */
    fat32_write_le16(boot + 19, 0U);             /* total sectors 16 */
    boot[21] = 0xF8;                             /* media descriptor */
    fat32_write_le16(boot + 22, 0U);             /* FAT size 16 */
    fat32_write_le16(boot + 24, 32U);            /* sectors per track */
    fat32_write_le16(boot + 26, 255U);           /* head count */
    fat32_write_le32(boot + 28, 0U);             /* hidden sectors */
    fat32_write_le32(boot + 32, (uint32_t) sector_count); /* total sectors 32 */
    fat32_write_le32(boot + 36, fat_size);       /* sectors per FAT */
    fat32_write_le16(boot + 40, 0U);            /* ext flags */
    fat32_write_le16(boot + 42, 0U);            /* fs version */
    fat32_write_le32(boot + 44, 2U);            /* root cluster */
    fat32_write_le16(boot + 48, 1U);            /* fs info sector */
    fat32_write_le16(boot + 50, 6U);            /* backup boot sector */
    boot[64] = 0x80;                             /* drive number */
    boot[66] = 0x29;                             /* boot signature */
    fat32_write_le32(boot + 67, 0x1234ABCDU);   /* volume serial */
    memcpy(boot + 71, "NO NAME    ", 11);       /* volume label */
    memcpy(boot + 82, "FAT32   ", 8);            /* fs type string */
    fat32_write_le16(boot + 510, 0xAA55U);       /* boot signature */

    ata_write_sector(volume_lba, boot);
    ata_write_sector(volume_lba + 6U, boot);    /* 备份引导扇区 */

    fat_lba = volume_lba + reserved;
    data_lba = volume_lba + data_start;

    /* 清零两个 FAT 表 */
    for (uint32_t f = 0; f < fat_count; f++) {
        uint32_t base = fat_lba + f * fat_size;
        for (uint32_t s = 0; s < fat_size; s++) {
            ata_write_sector(base + s, zero);
        }
    }

    /* 设置保留簇项：FAT[0] = 0x0FFFFFF8，FAT[1] = 0x0FFFFFFF */
    for (uint32_t f = 0; f < fat_count; f++) {
        uint8_t fat0[512];
        uint32_t base = fat_lba + f * fat_size;
        ata_read_sector(base, fat0);
        fat32_write_le32(fat0 + 0, 0x0FFFFFF8U);
        fat32_write_le32(fat0 + 4, 0x0FFFFFFFU);
        ata_write_sector(base, fat0);
    }

    /* 清零根目录簇（簇 2） */
    for (uint32_t s = 0; s < spc; s++) {
        ata_write_sector(data_lba + s, zero);
    }

    return true;
}

/* ============================================================
 *  FAT32 文件系统检查（chkdsk）
 * ============================================================ */

#define FAT32_CHK_MAX_CLUSTERS 262144U
static uint8_t g_chk_mark[FAT32_CHK_MAX_CLUSTERS];
static uint8_t g_chk_ref[FAT32_CHK_MAX_CLUSTERS];   /* 引用计数：>=2 表示交叉链接 */

/* 丢失簇恢复：把孤立簇链保存为 /FOUND.000/FILE00N.CHK */
#define FAT32_FOUND_DIR        "FOUND.000"
#define FAT32_FOUND_MAX_CLUST  64U
static uint8_t  g_chk_found_buf[FAT32_FOUND_MAX_CLUST * 512U];
static uint32_t g_chk_found_index;

static uint32_t fat32_chk_get_fat(uint32_t table, uint32_t cluster)
{
    uint32_t fat_offset = cluster * 4U;
    uint32_t sector_idx = fat_offset / 512U;
    uint32_t sector_off = fat_offset % 512U;
    uint32_t lba = g_fat_lba + table * g_bpb.fat_size_32 + sector_idx;
    uint8_t sec[512];
    uint32_t value;

    ata_read_sector(lba, sec);
    memcpy(&value, sec + sector_off, sizeof(value));
    return value & FAT32_CLUSTER_MASK;
}

static void fat32_chk_set_fat(uint32_t table, uint32_t cluster, uint32_t value)
{
    uint32_t fat_offset = cluster * 4U;
    uint32_t sector_idx = fat_offset / 512U;
    uint32_t sector_off = fat_offset % 512U;
    uint32_t lba = g_fat_lba + table * g_bpb.fat_size_32 + sector_idx;
    uint8_t sec[512];
    uint32_t current;

    ata_read_sector(lba, sec);
    memcpy(&current, sec + sector_off, sizeof(current));
    current = (current & ~FAT32_CLUSTER_MASK) | (value & FAT32_CLUSTER_MASK);
    memcpy(sec + sector_off, &current, sizeof(current));
    ata_write_sector(lba, sec);
}

/* 遍历一个目录，标记其簇链并递归子目录 */
static void fat32_chk_walk_dir(uint32_t dir_cluster, uint32_t depth,
                                uint32_t *errors, uint32_t *files)
{
    fat32_dir_ref_t dir;
    uint8_t sec[512];
    uint32_t total = g_cluster_count;

    if (depth > 16U || dir_cluster < 2U || dir_cluster >= total + 2U) {
        return;
    }
    dir.is_root = false;
    dir.start_cluster = dir_cluster;
    g_chk_mark[dir_cluster] = 1U;

    for (uint32_t sector_index = 0; fat32_read_dir_sector(&dir, sector_index, sec, NULL); sector_index++) {
        for (uint32_t i = 0; i < 512U / sizeof(fat32_dir_entry_t); i++) {
            fat32_dir_entry_t *entry = ((fat32_dir_entry_t *) sec) + i;
            uint32_t start;
            uint32_t cluster;

            if (entry->name[0] == 0x00) {
                return;
            }
            if (entry->name[0] == 0xE5 || (entry->attr & FAT32_ATTR_VOLUME_ID)) {
                continue;
            }
            if (entry->name[0] == '.' && (entry->name[1] == ' ' || entry->name[1] == '.')) {
                continue;
            }

            start = fat32_entry_cluster(entry);
            /* 目录项有效性：起始簇号必须在合法范围 */
            if (start != 0U && (start < 2U || start >= total + 2U)) {
                (*errors)++;
                continue;
            }
            /* 文件大小字段合理性 */
            if ((entry->attr & FAT32_ATTR_DIRECTORY) == 0 && entry->file_size > (uint32_t) 0x40000000U) {
                (*errors)++;
            }

            if (start < 2U) {
                continue;
            }

            /* 标记该文件/目录的簇链，检测环 */
            cluster = start;
            while (cluster >= 2U && cluster < total + 2U) {
                if (g_chk_mark[cluster] == 2U) {
                    /* 环：同一条链里重复出现 */
                    (*errors)++;
                    break;
                }
                g_chk_mark[cluster] = 2U;
                if (cluster < FAT32_CHK_MAX_CLUSTERS) {
                    g_chk_ref[cluster]++;
                }
                cluster = fat32_get_fat_entry(cluster);
                if (fat32_is_end_cluster(cluster) || cluster == FAT32_CLUSTER_FREE) {
                    break;
                }
            }

            if ((entry->attr & FAT32_ATTR_DIRECTORY) != 0) {
                fat32_chk_walk_dir(start, depth + 1U, errors, files);
            } else {
                (*files)++;
            }
        }
    }
}

/* 把一条丢失的簇链读入缓冲区，并写为 /FOUND.000/FILE00N.CHK，
 * 然后释放原始孤立簇链。链过长时只恢复前 FAT32_FOUND_MAX_CLUST 个簇。 */
static void fat32_chk_recover_lost(uint32_t start, uint32_t total, uint32_t *fixed)
{
    uint32_t chain[FAT32_FOUND_MAX_CLUST];
    uint32_t n = 0;
    uint32_t c = start;
    uint32_t cluster_bytes = (uint32_t) g_bpb.sectors_per_cluster * 512U;
    char path[28];
    uint32_t p = 0;
    const char *pre = "/" FAT32_FOUND_DIR "/FILE00";

    /* 追踪孤立链（有界） */
    while (c >= 2U && c < total + 2U && n < FAT32_FOUND_MAX_CLUST) {
        uint32_t v = fat32_chk_get_fat(0U, c);
        chain[n++] = c;
        if (fat32_is_end_cluster(v) || v == FAT32_CLUSTER_FREE || v == FAT32_CLUSTER_BAD) {
            break;
        }
        c = v;
    }

    /* 把链上每个簇读入缓冲区 */
    for (uint32_t i = 0; i < n; i++) {
        uint32_t lba = fat32_cluster_to_lba(chain[i]);
        for (uint8_t s = 0; s < g_bpb.sectors_per_cluster; s++) {
            ata_read_sector(lba + s, g_chk_found_buf + (i * cluster_bytes) + (uint32_t) s * 512U);
        }
    }

    /* 确保 /FOUND.000 目录存在 */
    if (!fat32_exists("/" FAT32_FOUND_DIR)) {
        fat32_mkdir("/" FAT32_FOUND_DIR);
    }

    while (*pre != '\0' && p < sizeof(path) - 8U) {
        path[p++] = *pre++;
    }
    path[p++] = (char) ('0' + (g_chk_found_index / 10U) % 10U);
    path[p++] = (char) ('0' + g_chk_found_index % 10U);
    path[p++] = '.';
    path[p++] = 'C';
    path[p++] = 'H';
    path[p++] = 'K';
    path[p] = '\0';

    if (fat32_write_file(path, g_chk_found_buf, n * cluster_bytes) >= 0) {
        wal_log_u32("chkdsk: recovered lost chain -> ", g_chk_found_index);
        serial_write(" (FOUND.000)\r\n");
        g_chk_found_index++;
        (*fixed)++;
    }

    /* 释放原始孤立簇链（两个 FAT 都更新） */
    for (uint32_t i = 0; i < n; i++) {
        fat32_chk_set_fat(0U, chain[i], FAT32_CLUSTER_FREE);
        fat32_chk_set_fat(1U, chain[i], FAT32_CLUSTER_FREE);
    }
}

bool fat32_chkdsk(fat32_chkdsk_report_t *report)
{
    uint32_t total;
    uint32_t errors = 0;
    uint32_t fixed = 0;
    uint32_t files = 0;

    if (!g_fat32_ready || report == NULL) {
        return false;
    }
    memset(report, 0, sizeof(*report));
    total = g_cluster_count;
    if (total == 0U || total >= FAT32_CHK_MAX_CLUSTERS) {
        return false;
    }
    memset(g_chk_mark, 0, (uint32_t) total);
    memset(g_chk_ref, 0, (uint32_t) total);
    g_chk_found_index = 0;

    /* 1. FAT 表一致性：FAT1 以 FAT0 为准修正 */
    for (uint32_t c = 2U; c < total + 2U; c++) {
        uint32_t v0 = fat32_chk_get_fat(0U, c);
        uint32_t v1 = fat32_chk_get_fat(1U, c);
        if (v0 != v1) {
            errors++;
            fat32_chk_set_fat(1U, c, v0);
            fixed++;
        }
    }

    /* 2/4. 遍历目录树，标记被引用的簇链并检查目录项有效性 */
    fat32_chk_walk_dir(g_bpb.root_cluster, 0U, &errors, &files);

    /* 交叉链接：同一个簇被两条以上目录链引用 */
    for (uint32_t c = 2U; c < total + 2U; c++) {
        if (g_chk_ref[c] >= 2U) {
            errors++;
        }
    }

    /* 3. 丢失簇：FAT 标记已用但没有任何目录项指向。
     *    修复：尽量把整条链恢复为 /FOUND.000/FILE00N.CHK，再释放原链。 */
    for (uint32_t c = 2U; c < total + 2U; c++) {
        uint32_t v = fat32_chk_get_fat(0U, c);
        if (v != FAT32_CLUSTER_FREE && g_chk_mark[c] == 0U) {
            errors++;
            fat32_chk_recover_lost(c, total, &fixed);
        }
    }

    report->errors = errors;
    report->fixed = fixed;
    report->files = files;
    report->total_kb = (uint64_t) fat32_total_sectors() * 512U / 1024U;
    report->volume_serial = g_bpb.volume_id;
    return true;
}
/* ============================================================
 *  Symbolic links: content magic "MONIOSLNK:" + target path
 *  (plain files, no special directory-entry attribute bits)
 * ============================================================ */

#define FAT32_SYMLINK_MAGIC       "MONIOSLNK:"
#define FAT32_SYMLINK_MAGIC_LEN   10U
#define FAT32_SYMLINK_BUF_SIZE    320U

bool fat32_symlink(const char *target, const char *linkpath)
{
    uint8_t buf[FAT32_SYMLINK_BUF_SIZE];
    uint32_t i = 0;

    if (!g_fat32_ready || target == NULL || linkpath == NULL) {
        return false;
    }
    memcpy(buf, FAT32_SYMLINK_MAGIC, FAT32_SYMLINK_MAGIC_LEN);
    while (target[i] != '\0' &&
           FAT32_SYMLINK_MAGIC_LEN + i + 1U < sizeof(buf)) {
        /* backend paths use '/', normalize separator style */
        buf[FAT32_SYMLINK_MAGIC_LEN + i] =
            (uint8_t) (target[i] == '\\' ? '/' : target[i]);
        i++;
    }
    return fat32_write_file(linkpath, buf, FAT32_SYMLINK_MAGIC_LEN + i) >= 0;
}

bool fat32_is_symlink(const char *path)
{
    uint8_t buf[FAT32_SYMLINK_MAGIC_LEN];
    int32_t n;

    if (!g_fat32_ready || path == NULL) {
        return false;
    }
    n = fat32_read_file(path, buf, sizeof(buf));
    return n >= (int32_t) FAT32_SYMLINK_MAGIC_LEN &&
           memcmp(buf, FAT32_SYMLINK_MAGIC, FAT32_SYMLINK_MAGIC_LEN) == 0;
}

int32_t fat32_read_symlink(const char *path, char *target, uint32_t target_size)
{
    uint8_t buf[FAT32_SYMLINK_BUF_SIZE];
    int32_t n;
    uint32_t tlen;
    uint32_t copy_len;

    if (!g_fat32_ready || path == NULL || target == NULL || target_size == 0U) {
        return -1;
    }
    n = fat32_read_file(path, buf, sizeof(buf) - 1U);
    if (n < (int32_t) FAT32_SYMLINK_MAGIC_LEN) {
        return -1;
    }
    if (memcmp(buf, FAT32_SYMLINK_MAGIC, FAT32_SYMLINK_MAGIC_LEN) != 0) {
        return -1;
    }
    tlen = (uint32_t) n - FAT32_SYMLINK_MAGIC_LEN;
    copy_len = tlen < target_size - 1U ? tlen : target_size - 1U;
    memcpy(target, buf + FAT32_SYMLINK_MAGIC_LEN, copy_len);
    target[copy_len] = '\0';
    /* restore Windows-style backslashes for upper layer path_resolve */
    for (uint32_t i = 0; i < copy_len; i++) {
        if (target[i] == '/') {
            target[i] = '\\';
        }
    }
    return (int32_t) tlen;
}
/* ============================================================
 *  Raw volume access + streaming image writer (one-click backup)
 *
 *  Expose the mounted FAT32 volume to the backup tool:
 *   - volume geometry / raw sector read-write (absolute LBA =
 *     g_volume_lba + rel_lba) for raw partition imaging.
 *   - an append-only streaming file writer so a multi-MB image can be
 *     built chunk-by-chunk without staging it all in RAM.
 * ============================================================ */

bool fat32_volume_geometry(uint32_t *out_start_lba, uint64_t *out_total_sectors,
                           uint32_t *out_sector_size, uint32_t *out_cluster_sectors)
{
    if (!g_fat32_ready) {
        return false;
    }
    if (out_start_lba != NULL) {
        *out_start_lba = g_volume_lba;
    }
    if (out_total_sectors != NULL) {
        *out_total_sectors = (uint64_t) fat32_total_sectors();
    }
    if (out_sector_size != NULL) {
        *out_sector_size = 512u;
    }
    if (out_cluster_sectors != NULL) {
        *out_cluster_sectors = g_bpb.sectors_per_cluster;
    }
    return true;
}

bool fat32_read_volume(uint32_t rel_lba, uint32_t count, void *buffer)
{
    uint8_t *dst = (uint8_t *) buffer;
    uint32_t done = 0;

    if (!g_fat32_ready || buffer == NULL || count == 0) {
        return false;
    }
    while (done < count) {
        uint32_t batch = count - done;
        if (batch > 255u) {
            batch = 255u;
        }
        ata_read_sectors(g_volume_lba + rel_lba + done, (uint8_t) batch, dst);
        dst += batch * 512u;
        done += batch;
    }
    return true;
}

bool fat32_write_volume(uint32_t rel_lba, uint32_t count, const void *buffer)
{
    const uint8_t *src = (const uint8_t *) buffer;
    uint32_t done = 0;

    if (!g_fat32_ready || buffer == NULL || count == 0) {
        return false;
    }
    while (done < count) {
        uint32_t batch = count - done;
        if (batch > 255u) {
            batch = 255u;
        }
        ata_write_sectors(g_volume_lba + rel_lba + done, (uint8_t) batch, src);
        src += batch * 512u;
        done += batch;
    }
    return true;
}

/* --- append-only streaming writer --- */

static bool g_append_open;
static fat32_dir_ref_t g_append_parent;
static fat32_dir_slot_t g_append_slot;
static uint32_t g_append_first_cluster;
static uint32_t g_append_prev_cluster;
static uint32_t g_append_size;
static uint32_t g_append_pending;
static uint8_t  g_append_cluster_buf[128u * 512u];

static void fat32_append_flush_cluster(bool final)
{
    uint32_t cluster_bytes = (uint32_t) g_bpb.sectors_per_cluster * 512u;
    uint32_t cluster = fat32_allocate_cluster();
    uint32_t spc = g_bpb.sectors_per_cluster;
    uint32_t i = 0;

    if (cluster == 0) {
        return;
    }
    if (g_append_first_cluster == 0) {
        g_append_first_cluster = cluster;
    }
    if (g_append_prev_cluster != 0) {
        fat32_set_fat_entry(g_append_prev_cluster, cluster);
    }
    fat32_set_fat_entry(cluster, FAT32_CLUSTER_END);
    g_append_prev_cluster = cluster;

    if (final && g_append_pending < cluster_bytes) {
        memset(g_append_cluster_buf + g_append_pending, 0,
               cluster_bytes - g_append_pending);
    }
    while (i < spc) {
        uint32_t batch = spc - i;
        if (batch > 8u) {
            batch = 8u;
        }
        ata_write_sectors(fat32_cluster_to_lba(cluster) + i, (uint8_t) batch,
                          g_append_cluster_buf + i * 512u);
        i += batch;
    }
}

int32_t fat32_append_open(const char *backend_path)
{
    fat32_dir_ref_t parent;
    fat32_dir_slot_t slot;
    char final_component[FAT32_COMP_NAME_MAX];

    if (!g_fat32_ready ||
        !fat32_resolve_parent(backend_path, &parent, final_component) ||
        final_component[0] == '\0') {
        return -1;
    }
    fat32_clear_read_cache();
    if (fat32_find_entry_in_dir(&parent, final_component, &slot)) {
        if ((slot.entry.attr & FAT32_ATTR_DIRECTORY) != 0) {
            return -2;
        }
        if (fat32_entry_cluster(&slot.entry) >= 2) {
            fat32_free_chain(fat32_entry_cluster(&slot.entry));
        }
    } else {
        if (!fat32_create_named_entry(&parent, final_component, &slot)) {
            return -3;
        }
    }

    g_append_parent = parent;
    g_append_slot = slot;
    g_append_first_cluster = 0;
    g_append_prev_cluster = 0;
    g_append_size = 0;
    g_append_pending = 0;
    g_append_open = true;
    return 0;
}

int32_t fat32_append_write(const void *buffer, uint32_t len)
{
    const uint8_t *src = (const uint8_t *) buffer;
    uint32_t cluster_bytes = (uint32_t) g_bpb.sectors_per_cluster * 512u;
    uint32_t remaining = len;

    if (!g_append_open || buffer == NULL) {
        return -1;
    }
    while (remaining > 0) {
        uint32_t space = cluster_bytes - g_append_pending;
        uint32_t n = remaining < space ? remaining : space;

        memcpy(g_append_cluster_buf + g_append_pending, src, n);
        g_append_pending += n;
        src += n;
        remaining -= n;
        g_append_size += n;
        if (g_append_pending == cluster_bytes) {
            fat32_append_flush_cluster(false);
            g_append_pending = 0;
        }
    }
    return (int32_t) len;
}

int32_t fat32_append_close(void)
{
    int32_t sz;

    if (!g_append_open) {
        return -1;
    }
    if (g_append_pending > 0) {
        fat32_append_flush_cluster(true);
    }
    g_append_slot.entry.attr = 0;
    fat32_set_entry_cluster(&g_append_slot.entry, g_append_first_cluster);
    g_append_slot.entry.file_size = g_append_size;
    if (!fat32_write_slot(&g_append_parent, &g_append_slot)) {
        g_append_open = false;
        return -2;
    }
    sz = (int32_t) g_append_size;
    g_append_open = false;
    return sz;
}
