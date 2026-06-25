#include "common.h"
#include "extfs.h"
#include "string.h"

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
#define ATA_STATUS_ERR        0x01
#define EXT_SUPER_OFFSET      1024U
#define EXT_SUPER_MAGIC       0xEF53U
#define EXT_ROOT_INODE        2

#define EXT_DIR_FILE_TYPE     1
#define EXT_DIR_DIR_TYPE      2
#define EXT_DIR_SYMLINK_TYPE  7

#define EXT_INODE_MODE_FMT    0xF000
#define EXT_INODE_MODE_FIFO   0x1000
#define EXT_INODE_MODE_CHAR   0x2000
#define EXT_INODE_MODE_DIR    0x4000
#define EXT_INODE_MODE_BLK    0x6000
#define EXT_INODE_MODE_REG    0x8000
#define EXT_INODE_MODE_SYM    0xA000
#define EXT_INODE_MODE_SOCK   0xC000

#define EXT_INODE_FLAG_INDEX  0x0001
#define EXT_INODE_FLAG_SYMLIN 0x0002

#define EXTFS_CACHE_SIZE      8
#define EXTFS_MAX_SYMLINK     8
#define EXTFS_MAX_GROUP_DESC  256

static extfs_info_t g_extfs_info;
static uint8_t g_extfs_block[4096];
static uint8_t g_extfs_inode_buf[256];

static uint32_t g_cache_block[EXTFS_CACHE_SIZE];
static uint8_t g_cache_data[EXTFS_CACHE_SIZE][4096];
static uint32_t g_cache_lru[EXTFS_CACHE_SIZE];
static bool g_cache_dirty[EXTFS_CACHE_SIZE];
static uint32_t g_cache_counter = 0;

static uint32_t g_symlink_count = 0;

/* 组描述符缓存 */
static uint8_t g_group_desc[EXTFS_MAX_GROUP_DESC * 32];
static bool g_group_desc_loaded = false;
static uint32_t g_group_count = 0;

typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    char name[];
} ext_dir_entry_t;

typedef struct {
    uint16_t mode;
    uint16_t uid;
    uint32_t size;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint16_t gid;
    uint16_t links_count;
    uint32_t blocks;
    uint32_t flags;
    uint32_t osd1;
    uint32_t block[15];
    uint32_t generation;
    uint32_t file_acl;
    uint32_t dir_acl;
    uint32_t faddr;
    uint8_t osd2[12];
} ext_inode_t;

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

static void ata_read_sector(uint32_t lba, void *buffer)
{
    uint16_t *dst = (uint16_t *) buffer;

    if (!ata_wait_not_busy()) {
        memset(buffer, 0, 512);
        return;
    }
    outb(ATA_DRIVE_PORT, (uint8_t) (0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT_PORT, 1);
    outb(ATA_LBA_LOW_PORT, (uint8_t) (lba & 0xFF));
    outb(ATA_LBA_MID_PORT, (uint8_t) ((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH_PORT, (uint8_t) ((lba >> 16) & 0xFF));
    outb(ATA_COMMAND_PORT, ATA_CMD_READ_SECTORS);

    if (!ata_wait_data_ready()) {
        memset(buffer, 0, 512);
        return;
    }
    for (uint32_t i = 0; i < 256; i++) {
        dst[i] = inw(ATA_DATA_PORT);
    }
}

static void ata_write_sector(uint32_t lba, const void *buffer)
{
    const uint16_t *src = (const uint16_t *) buffer;

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

    /* 等待写入完成 */
    (void) ata_wait_not_busy();
}

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t) data[0] | ((uint16_t) data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
    return ((uint32_t) data[0]) |
           ((uint32_t) data[1] << 8) |
           ((uint32_t) data[2] << 16) |
           ((uint32_t) data[3] << 24);
}

static bool ext_partition_type(uint8_t type)
{
    return type == 0x83;
}

static void extfs_cache_init(void)
{
    for (int i = 0; i < EXTFS_CACHE_SIZE; i++) {
        g_cache_block[i] = 0xFFFFFFFF;
        g_cache_lru[i] = 0;
        g_cache_dirty[i] = false;
    }
}

static int extfs_cache_find(uint32_t block)
{
    for (int i = 0; i < EXTFS_CACHE_SIZE; i++) {
        if (g_cache_block[i] == block) {
            return i;
        }
    }
    return -1;
}

static int extfs_cache_find_victim(void)
{
    int victim = 0;
    uint32_t min_lru = g_cache_lru[0];

    for (int i = 1; i < EXTFS_CACHE_SIZE; i++) {
        if (g_cache_lru[i] < min_lru) {
            min_lru = g_cache_lru[i];
            victim = i;
        }
    }
    return victim;
}

static void extfs_flush_block(uint32_t block, const uint8_t *data)
{
    uint32_t lba = g_extfs_info.volume_lba + block * (g_extfs_info.block_size / 512);
    uint32_t sectors = g_extfs_info.block_size / 512;

    for (uint32_t i = 0; i < sectors; i++) {
        ata_write_sector(lba + i, data + i * 512);
    }
}

static void extfs_cache_flush_idx(int idx)
{
    if (g_cache_dirty[idx] && g_cache_block[idx] != 0xFFFFFFFF) {
        extfs_flush_block(g_cache_block[idx], g_cache_data[idx]);
        g_cache_dirty[idx] = false;
    }
}

void extfs_sync(void)
{
    for (int i = 0; i < EXTFS_CACHE_SIZE; i++) {
        extfs_cache_flush_idx(i);
    }
}

static void extfs_read_block_cached(uint32_t block, uint8_t *buffer)
{
    int cache_idx = extfs_cache_find(block);

    if (cache_idx >= 0) {
        g_cache_lru[cache_idx] = ++g_cache_counter;
        memcpy(buffer, g_cache_data[cache_idx], g_extfs_info.block_size);
        return;
    }

    /* 未命中，从磁盘读取 */
    uint32_t lba = g_extfs_info.volume_lba + block * (g_extfs_info.block_size / 512);
    uint32_t sectors = g_extfs_info.block_size / 512;

    for (uint32_t i = 0; i < sectors; i++) {
        ata_read_sector(lba + i, buffer + i * 512);
    }

    /* 加入缓存 */
    cache_idx = extfs_cache_find_victim();
    extfs_cache_flush_idx(cache_idx);  /* 先刷新旧的 dirty 块 */
    g_cache_block[cache_idx] = block;
    g_cache_lru[cache_idx] = ++g_cache_counter;
    g_cache_dirty[cache_idx] = false;
    memcpy(g_cache_data[cache_idx], buffer, g_extfs_info.block_size);
}

static void extfs_write_block_cached(uint32_t block, const uint8_t *buffer)
{
    int cache_idx = extfs_cache_find(block);

    if (cache_idx >= 0) {
        g_cache_lru[cache_idx] = ++g_cache_counter;
        memcpy(g_cache_data[cache_idx], buffer, g_extfs_info.block_size);
        g_cache_dirty[cache_idx] = true;
        return;
    }

    /* 未命中，加入缓存 */
    cache_idx = extfs_cache_find_victim();
    extfs_cache_flush_idx(cache_idx);  /* 先刷新旧的 dirty 块 */
    g_cache_block[cache_idx] = block;
    g_cache_lru[cache_idx] = ++g_cache_counter;
    g_cache_dirty[cache_idx] = true;
    memcpy(g_cache_data[cache_idx], buffer, g_extfs_info.block_size);
}

static void extfs_read_block(uint32_t block, void *buffer)
{
    extfs_read_block_cached(block, (uint8_t *) buffer);
}

static void extfs_write_block(uint32_t block, const void *buffer)
{
    extfs_write_block_cached(block, (const uint8_t *) buffer);
}

static bool extfs_read_super(uint32_t volume_lba, uint8_t super[1024])
{
    ata_read_sector(volume_lba + EXT_SUPER_OFFSET / 512u, super);
    ata_read_sector(volume_lba + EXT_SUPER_OFFSET / 512u + 1u, super + 512);
    return read_le16(super + 56) == EXT_SUPER_MAGIC;
}

static bool extfs_parse_super(uint32_t volume_lba, const uint8_t super[1024])
{
    uint32_t log_block_size;

    if (read_le16(super + 56) != EXT_SUPER_MAGIC) {
        return false;
    }
    memset(&g_extfs_info, 0, sizeof(g_extfs_info));
    g_extfs_info.present = true;
    g_extfs_info.read_only = true;
    g_extfs_info.volume_lba = volume_lba;
    g_extfs_info.inodes_count = read_le32(super + 0);
    g_extfs_info.blocks_count = read_le32(super + 4);
    g_extfs_info.free_blocks = read_le32(super + 12);
    g_extfs_info.free_inodes = read_le32(super + 16);
    log_block_size = read_le32(super + 24);
    g_extfs_info.block_size = 1024u << (log_block_size > 3 ? 3 : log_block_size);
    g_extfs_info.blocks_per_group = read_le32(super + 32);
    g_extfs_info.inodes_per_group = read_le32(super + 40);
    g_extfs_info.state = read_le16(super + 58);
    g_extfs_info.inode_size = read_le16(super + 88);
    if (g_extfs_info.inode_size == 0) {
        g_extfs_info.inode_size = 128;
    }
    g_extfs_info.feature_compat = read_le32(super + 92);
    g_extfs_info.feature_incompat = read_le32(super + 96);
    g_extfs_info.feature_ro_compat = read_le32(super + 100);

    extfs_cache_init();

    strcpy(g_extfs_info.status, "extfs: ready");
    return true;
}

static uint32_t extfs_get_group_desc_block(void)
{
    if (g_extfs_info.block_size == 1024) {
        return 2;
    } else {
        return 1;
    }
}

static bool extfs_read_group_desc(uint32_t group, uint8_t *desc)
{
    uint32_t gdt_block = extfs_get_group_desc_block();
    uint32_t desc_size = 32;
    uint32_t offset = group * desc_size;

    extfs_read_block(gdt_block, g_extfs_block);

    if (offset + desc_size > g_extfs_info.block_size) {
        return false;
    }

    memcpy(desc, g_extfs_block + offset, desc_size);
    return true;
}

static uint32_t extfs_get_inode_table_block(uint32_t group)
{
    uint8_t gdesc[32];

    if (!extfs_read_group_desc(group, gdesc)) {
        return 0;
    }

    return read_le32(gdesc + 8);
}

static uint32_t extfs_get_block_bitmap_block(uint32_t group)
{
    uint8_t gdesc[32];

    if (!extfs_read_group_desc(group, gdesc)) {
        return 0;
    }

    return read_le32(gdesc + 0);
}

static uint32_t extfs_get_inode_bitmap_block(uint32_t group)
{
    uint8_t gdesc[32];

    if (!extfs_read_group_desc(group, gdesc)) {
        return 0;
    }

    return read_le32(gdesc + 4);
}

static uint32_t extfs_get_group_free_blocks(uint32_t group)
{
    uint8_t gdesc[32];

    if (!extfs_read_group_desc(group, gdesc)) {
        return 0;
    }

    return read_le16(gdesc + 12);
}

static uint32_t extfs_get_group_free_inodes(uint32_t group)
{
    uint8_t gdesc[32];

    if (!extfs_read_group_desc(group, gdesc)) {
        return 0;
    }

    return read_le16(gdesc + 14);
}

static void extfs_set_group_free_blocks(uint32_t group, uint32_t count)
{
    uint8_t gdesc[32];
    uint32_t gdt_block = extfs_get_group_desc_block();
    uint32_t offset = group * 32;

    extfs_read_block(gdt_block, g_extfs_block);
    memcpy(gdesc, g_extfs_block + offset, 32);

    /* 更新空闲块数 */
    gdesc[12] = (uint8_t) (count & 0xFF);
    gdesc[13] = (uint8_t) ((count >> 8) & 0xFF);

    memcpy(g_extfs_block + offset, gdesc, 32);
    extfs_write_block(gdt_block, g_extfs_block);
}

static void extfs_set_group_free_inodes(uint32_t group, uint32_t count)
{
    uint8_t gdesc[32];
    uint32_t gdt_block = extfs_get_group_desc_block();
    uint32_t offset = group * 32;

    extfs_read_block(gdt_block, g_extfs_block);
    memcpy(gdesc, g_extfs_block + offset, 32);

    /* 更新空闲 inode 数 */
    gdesc[14] = (uint8_t) (count & 0xFF);
    gdesc[15] = (uint8_t) ((count >> 8) & 0xFF);

    memcpy(g_extfs_block + offset, gdesc, 32);
    extfs_write_block(gdt_block, g_extfs_block);
}

static uint32_t extfs_get_total_groups(void)
{
    uint32_t groups = g_extfs_info.blocks_count / g_extfs_info.blocks_per_group;
    if (g_extfs_info.blocks_count % g_extfs_info.blocks_per_group != 0) {
        groups++;
    }
    return groups;
}

/* 位图操作 */
static bool extfs_test_bit(const uint8_t *bitmap, uint32_t bit)
{
    return (bitmap[bit / 8] & (1 << (bit % 8))) != 0;
}

static void extfs_set_bit(uint8_t *bitmap, uint32_t bit)
{
    bitmap[bit / 8] |= (uint8_t) (1 << (bit % 8));
}

static void extfs_clear_bit(uint8_t *bitmap, uint32_t bit)
{
    bitmap[bit / 8] &= (uint8_t) ~(1 << (bit % 8));
}

/* 块分配 */
static int32_t extfs_alloc_block(uint32_t preferred_group)
{
    uint32_t total_groups = extfs_get_total_groups();

    for (uint32_t g = 0; g < total_groups; g++) {
        uint32_t group = (preferred_group + g) % total_groups;

        if (extfs_get_group_free_blocks(group) == 0) {
            continue;
        }

        uint32_t bitmap_block = extfs_get_block_bitmap_block(group);
        if (bitmap_block == 0) {
            continue;
        }

        extfs_read_block(bitmap_block, g_extfs_block);

        uint32_t blocks_in_group = g_extfs_info.blocks_per_group;
        if (group == total_groups - 1) {
            blocks_in_group = g_extfs_info.blocks_count - group * g_extfs_info.blocks_per_group;
        }

        for (uint32_t i = 0; i < blocks_in_group; i++) {
            if (!extfs_test_bit(g_extfs_block, i)) {
                /* 找到空闲块 */
                extfs_set_bit(g_extfs_block, i);
                extfs_write_block(bitmap_block, g_extfs_block);

                /* 更新组描述符 */
                uint32_t free_blocks = extfs_get_group_free_blocks(group);
                if (free_blocks > 0) {
                    extfs_set_group_free_blocks(group, free_blocks - 1);
                }

                /* 更新超级块 */
                if (g_extfs_info.free_blocks > 0) {
                    g_extfs_info.free_blocks--;
                }

                /* 清零新块 */
                memset(g_extfs_block, 0, g_extfs_info.block_size);
                uint32_t block_num = group * g_extfs_info.blocks_per_group + i;
                extfs_write_block(block_num, g_extfs_block);

                return (int32_t) block_num;
            }
        }
    }

    return -1;
}

static void extfs_free_block(uint32_t block_num)
{
    uint32_t group = block_num / g_extfs_info.blocks_per_group;
    uint32_t index_in_group = block_num % g_extfs_info.blocks_per_group;

    uint32_t bitmap_block = extfs_get_block_bitmap_block(group);
    if (bitmap_block == 0) {
        return;
    }

    extfs_read_block(bitmap_block, g_extfs_block);
    extfs_clear_bit(g_extfs_block, index_in_group);
    extfs_write_block(bitmap_block, g_extfs_block);

    /* 更新组描述符 */
    uint32_t free_blocks = extfs_get_group_free_blocks(group);
    extfs_set_group_free_blocks(group, free_blocks + 1);

    /* 更新超级块 */
    g_extfs_info.free_blocks++;
}

/* inode 分配 */
static int32_t extfs_alloc_inode(uint32_t preferred_group)
{
    uint32_t total_groups = extfs_get_total_groups();

    for (uint32_t g = 0; g < total_groups; g++) {
        uint32_t group = (preferred_group + g) % total_groups;

        if (extfs_get_group_free_inodes(group) == 0) {
            continue;
        }

        uint32_t bitmap_block = extfs_get_inode_bitmap_block(group);
        if (bitmap_block == 0) {
            continue;
        }

        extfs_read_block(bitmap_block, g_extfs_block);

        uint32_t inodes_in_group = g_extfs_info.inodes_per_group;

        /* inode 从 1 开始编号 */
        for (uint32_t i = 1; i <= inodes_in_group; i++) {
            if (!extfs_test_bit(g_extfs_block, i - 1)) {
                /* 找到空闲 inode */
                extfs_set_bit(g_extfs_block, i - 1);
                extfs_write_block(bitmap_block, g_extfs_block);

                /* 更新组描述符 */
                uint32_t free_inodes = extfs_get_group_free_inodes(group);
                if (free_inodes > 0) {
                    extfs_set_group_free_inodes(group, free_inodes - 1);
                }

                /* 更新超级块 */
                if (g_extfs_info.free_inodes > 0) {
                    g_extfs_info.free_inodes--;
                }

                uint32_t inode_num = group * g_extfs_info.inodes_per_group + i;
                return (int32_t) inode_num;
            }
        }
    }

    return -1;
}

static void extfs_free_inode(uint32_t inode_num)
{
    uint32_t group = (inode_num - 1) / g_extfs_info.inodes_per_group;
    uint32_t index_in_group = (inode_num - 1) % g_extfs_info.inodes_per_group;

    uint32_t bitmap_block = extfs_get_inode_bitmap_block(group);
    if (bitmap_block == 0) {
        return;
    }

    extfs_read_block(bitmap_block, g_extfs_block);
    extfs_clear_bit(g_extfs_block, index_in_group);
    extfs_write_block(bitmap_block, g_extfs_block);

    /* 更新组描述符 */
    uint32_t free_inodes = extfs_get_group_free_inodes(group);
    extfs_set_group_free_inodes(group, free_inodes + 1);

    /* 更新超级块 */
    g_extfs_info.free_inodes++;
}

static bool extfs_write_inode_raw(uint32_t inode_num, const ext_inode_t *inode)
{
    uint32_t group;
    uint32_t index_in_group;
    uint32_t inode_table_block;
    uint32_t inode_offset;
    ext_inode_t raw_inode;

    if (inode_num == 0) {
        return false;
    }

    group = (inode_num - 1) / g_extfs_info.inodes_per_group;
    index_in_group = (inode_num - 1) % g_extfs_info.inodes_per_group;

    inode_table_block = extfs_get_inode_table_block(group);
    if (inode_table_block == 0) {
        return false;
    }

    inode_offset = index_in_group * g_extfs_info.inode_size;
    uint32_t block_offset = inode_offset / g_extfs_info.block_size;
    uint32_t byte_offset = inode_offset % g_extfs_info.block_size;

    /* 转换为小端序 */
    memcpy(&raw_inode, inode, sizeof(ext_inode_t));

    /* 读取原块 */
    extfs_read_block(inode_table_block + block_offset, g_extfs_inode_buf);

    /* 写入 inode 数据 */
    memcpy(g_extfs_inode_buf + byte_offset, &raw_inode, g_extfs_info.inode_size);

    /* 写回块 */
    extfs_write_block(inode_table_block + block_offset, g_extfs_inode_buf);

    return true;
}

static bool extfs_read_inode_raw(uint32_t inode_num, ext_inode_t *inode)
{
    uint32_t group;
    uint32_t index_in_group;
    uint32_t inode_table_block;
    uint32_t inode_offset;

    if (inode_num == 0) {
        return false;
    }

    group = (inode_num - 1) / g_extfs_info.inodes_per_group;
    index_in_group = (inode_num - 1) % g_extfs_info.inodes_per_group;

    inode_table_block = extfs_get_inode_table_block(group);
    if (inode_table_block == 0) {
        return false;
    }

    inode_offset = index_in_group * g_extfs_info.inode_size;
    uint32_t block_offset = inode_offset / g_extfs_info.block_size;
    uint32_t byte_offset = inode_offset % g_extfs_info.block_size;

    extfs_read_block(inode_table_block + block_offset, g_extfs_inode_buf);
    memcpy(inode, g_extfs_inode_buf + byte_offset, sizeof(ext_inode_t));

    /* 转换为小端序 */
    inode->mode = read_le16((uint8_t *) &inode->mode);
    inode->uid = read_le16((uint8_t *) &inode->uid);
    inode->size = read_le32((uint8_t *) &inode->size);
    inode->atime = read_le32((uint8_t *) &inode->atime);
    inode->ctime = read_le32((uint8_t *) &inode->ctime);
    inode->mtime = read_le32((uint8_t *) &inode->mtime);
    inode->dtime = read_le32((uint8_t *) &inode->dtime);
    inode->gid = read_le16((uint8_t *) &inode->gid);
    inode->links_count = read_le16((uint8_t *) &inode->links_count);
    inode->blocks = read_le32((uint8_t *) &inode->blocks);
    inode->flags = read_le32((uint8_t *) &inode->flags);
    inode->generation = read_le32((uint8_t *) &inode->generation);
    inode->file_acl = read_le32((uint8_t *) &inode->file_acl);
    inode->dir_acl = read_le32((uint8_t *) &inode->dir_acl);
    inode->faddr = read_le32((uint8_t *) &inode->faddr);

    for (int i = 0; i < 15; i++) {
        inode->block[i] = read_le32((uint8_t *) &inode->block[i]);
    }

    return true;
}

static bool extfs_inode_is_directory(const ext_inode_t *inode)
{
    return (inode->mode & EXT_INODE_MODE_FMT) == EXT_INODE_MODE_DIR;
}

static bool extfs_inode_is_file(const ext_inode_t *inode)
{
    return (inode->mode & EXT_INODE_MODE_FMT) == EXT_INODE_MODE_REG;
}

static bool extfs_inode_is_symlink(const ext_inode_t *inode)
{
    return (inode->mode & EXT_INODE_MODE_FMT) == EXT_INODE_MODE_SYM;
}

static uint32_t extfs_inode_file_size(const ext_inode_t *inode)
{
    return inode->size;
}

static uint32_t extfs_get_block_at(const ext_inode_t *inode, uint32_t block_index)
{
    uint32_t blocks_per_indirect = g_extfs_info.block_size / 4;

    /* 直接块 */
    if (block_index < 12) {
        return inode->block[block_index];
    }

    block_index -= 12;

    /* 间接块 */
    if (block_index < blocks_per_indirect) {
        uint32_t indirect_block = inode->block[12];
        if (indirect_block == 0) {
            return 0;
        }
        extfs_read_block(indirect_block, g_extfs_block);
        return read_le32(g_extfs_block + block_index * 4);
    }

    block_index -= blocks_per_indirect;

    /* 双重间接块 */
    if (block_index < blocks_per_indirect * blocks_per_indirect) {
        uint32_t double_indirect = inode->block[13];
        if (double_indirect == 0) {
            return 0;
        }
        uint32_t first_level = block_index / blocks_per_indirect;
        uint32_t second_level = block_index % blocks_per_indirect;

        extfs_read_block(double_indirect, g_extfs_block);
        uint32_t second_block = read_le32(g_extfs_block + first_level * 4);
        if (second_block == 0) {
            return 0;
        }

        extfs_read_block(second_block, g_extfs_block);
        return read_le32(g_extfs_block + second_level * 4);
    }

    block_index -= blocks_per_indirect * blocks_per_indirect;

    /* 三重间接块 */
    uint32_t triple_indirect = inode->block[14];
    if (triple_indirect == 0) {
        return 0;
    }

    uint32_t first = block_index / (blocks_per_indirect * blocks_per_indirect);
    uint32_t remainder = block_index % (blocks_per_indirect * blocks_per_indirect);
    uint32_t second = remainder / blocks_per_indirect;
    uint32_t third = remainder % blocks_per_indirect;

    extfs_read_block(triple_indirect, g_extfs_block);
    uint32_t first_block = read_le32(g_extfs_block + first * 4);
    if (first_block == 0) {
        return 0;
    }

    extfs_read_block(first_block, g_extfs_block);
    uint32_t second_block = read_le32(g_extfs_block + second * 4);
    if (second_block == 0) {
        return 0;
    }

    extfs_read_block(second_block, g_extfs_block);
    return read_le32(g_extfs_block + third * 4);
}

static bool extfs_read_symlink_target(const ext_inode_t *inode, char *target, uint32_t target_size)
{
    uint32_t size = inode->size;

    if (size == 0 || size >= target_size) {
        return false;
    }

    /* 快速符号链接：目标路径存储在 block 数组中 */
    if (size <= 60) {
        memcpy(target, inode->block, size);
        target[size] = '\0';
        return true;
    }

    /* 慢速符号链接：存储在数据块中 */
    uint32_t block = inode->block[0];
    if (block == 0) {
        return false;
    }

    extfs_read_block(block, g_extfs_block);
    memcpy(target, g_extfs_block, size);
    target[size] = '\0';

    return true;
}

static bool extfs_find_in_dir(uint32_t dir_inode, const char *name,
                              uint32_t *out_inode, uint8_t *out_type)
{
    ext_inode_t inode;
    uint32_t file_size;
    uint32_t offset = 0;

    if (!extfs_read_inode_raw(dir_inode, &inode)) {
        return false;
    }

    if (!extfs_inode_is_directory(&inode)) {
        return false;
    }

    file_size = extfs_inode_file_size(&inode);
    uint32_t name_len = (uint32_t) strlen(name);

    while (offset < file_size) {
        uint32_t block_index = offset / g_extfs_info.block_size;
        uint32_t block_offset = offset % g_extfs_info.block_size;
        uint32_t block = extfs_get_block_at(&inode, block_index);

        if (block == 0) {
            return false;
        }

        extfs_read_block(block, g_extfs_block);

        ext_dir_entry_t *entry = (ext_dir_entry_t *) (g_extfs_block + block_offset);

        if (entry->rec_len == 0) {
            break;
        }

        if (entry->name_len == name_len &&
            memcmp(entry->name, name, name_len) == 0) {
            if (out_inode != NULL) {
                *out_inode = read_le32((uint8_t *) &entry->inode);
            }
            if (out_type != NULL) {
                *out_type = entry->file_type;
            }
            return true;
        }

        offset += entry->rec_len;
    }

    return false;
}

static bool extfs_resolve_path_internal(const char *path, uint32_t *out_inode, bool follow_symlinks)
{
    uint32_t current_inode = EXT_ROOT_INODE;
    const char *cursor = path;
    char component[256];
    char symlink_target[256];

    if (!g_extfs_info.present || path == NULL || out_inode == NULL) {
        return false;
    }

    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        *out_inode = current_inode;
        return true;
    }

    while (*cursor != '\0') {
        while (*cursor == '/') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        uint32_t i = 0;
        while (*cursor != '\0' && *cursor != '/' && i < sizeof(component) - 1) {
            component[i++] = *cursor++;
        }
        component[i] = '\0';

        /* 处理 "." 和 ".." */
        if (strcmp(component, ".") == 0) {
            continue;
        }
        if (strcmp(component, "..") == 0) {
            /* 根目录的 .. 还是根目录 */
            if (current_inode == EXT_ROOT_INODE) {
                continue;
            }
            /* 查找父目录 */
            uint32_t parent_inode;
            if (!extfs_find_in_dir(current_inode, "..", &parent_inode, NULL)) {
                return false;
            }
            current_inode = parent_inode;
            continue;
        }

        uint32_t next_inode;
        uint8_t file_type;
        if (!extfs_find_in_dir(current_inode, component, &next_inode, &file_type)) {
            return false;
        }

        /* 处理符号链接 */
        if (follow_symlinks && file_type == EXT_DIR_SYMLINK_TYPE) {
            if (g_symlink_count >= EXTFS_MAX_SYMLINK) {
                return false;  /* 符号链接循环 */
            }

            ext_inode_t sym_inode;
            if (!extfs_read_inode_raw(next_inode, &sym_inode)) {
                return false;
            }

            if (!extfs_read_symlink_target(&sym_inode, symlink_target, sizeof(symlink_target))) {
                return false;
            }

            g_symlink_count++;

            /* 递归解析符号链接 */
            uint32_t resolved_inode;
            const char *rest = cursor;

            if (symlink_target[0] == '/') {
                /* 绝对路径符号链接 */
                current_inode = EXT_ROOT_INODE;
            }

            /* 拼接剩余路径 */
            char full_path[512];
            uint32_t sym_len = strlen(symlink_target);
            uint32_t rest_len = strlen(rest);

            if (rest[0] != '\0') {
                if (sym_len + 1 + rest_len >= sizeof(full_path)) {
                    g_symlink_count--;
                    return false;
                }
                memcpy(full_path, symlink_target, sym_len);
                full_path[sym_len] = '/';
                memcpy(full_path + sym_len + 1, rest, rest_len);
                full_path[sym_len + 1 + rest_len] = '\0';
            } else {
                if (sym_len >= sizeof(full_path)) {
                    g_symlink_count--;
                    return false;
                }
                memcpy(full_path, symlink_target, sym_len);
                full_path[sym_len] = '\0';
            }

            if (!extfs_resolve_path_internal(full_path, &resolved_inode, true)) {
                g_symlink_count--;
                return false;
            }

            g_symlink_count--;
            *out_inode = resolved_inode;
            return true;
        }

        current_inode = next_inode;
    }

    *out_inode = current_inode;
    return true;
}

static bool extfs_resolve_path(const char *path, uint32_t *out_inode)
{
    g_symlink_count = 0;
    return extfs_resolve_path_internal(path, out_inode, true);
}

bool extfs_init(void)
{
    uint8_t sector[512];
    uint8_t super[1024];

    memset(&g_extfs_info, 0, sizeof(g_extfs_info));
    strcpy(g_extfs_info.status, "extfs: not found");

    if (extfs_read_super(0, super) && extfs_parse_super(0, super)) {
        return true;
    }
    ata_read_sector(0, sector);
    if (read_le16(sector + 510) == 0xAA55) {
        for (uint8_t i = 0; i < 4; i++) {
            uint8_t *entry = sector + 446 + (uint32_t) i * 16;
            uint32_t lba = read_le32(entry + 8);

            if (!ext_partition_type(entry[4]) || lba == 0) {
                continue;
            }
            if (extfs_read_super(lba, super) && extfs_parse_super(lba, super)) {
                return true;
            }
        }
    }
    strcpy(g_extfs_info.status, "extfs: no volume detected");
    return false;
}

uint16_t extfs_root_entry_count(void)
{
    char buffer[4096];
    uint16_t count = 0;

    if (!extfs_list_root(buffer, sizeof(buffer))) {
        return 0;
    }
    for (uint32_t i = 0; buffer[i] != '\0'; i++) {
        if (buffer[i] == '\n') {
            count++;
        }
    }
    return count;
}

bool extfs_exists(const char *path)
{
    uint32_t inode;
    return extfs_resolve_path(path, &inode);
}

bool extfs_is_dir(const char *path)
{
    uint32_t inode_num;
    ext_inode_t inode;

    if (!extfs_resolve_path(path, &inode_num)) {
        return false;
    }
    if (!extfs_read_inode_raw(inode_num, &inode)) {
        return false;
    }
    return extfs_inode_is_directory(&inode);
}

bool extfs_is_symlink(const char *path)
{
    uint32_t inode_num;
    ext_inode_t inode;

    if (!extfs_resolve_path(path, &inode_num)) {
        return false;
    }
    if (!extfs_read_inode_raw(inode_num, &inode)) {
        return false;
    }
    return extfs_inode_is_symlink(&inode);
}

int32_t extfs_file_size(const char *path)
{
    uint32_t inode_num;
    ext_inode_t inode;

    if (!extfs_resolve_path(path, &inode_num)) {
        return -1;
    }
    if (!extfs_read_inode_raw(inode_num, &inode)) {
        return -1;
    }
    if (extfs_inode_is_directory(&inode)) {
        return -1;
    }
    return (int32_t) extfs_inode_file_size(&inode);
}

int32_t extfs_read_symlink(const char *path, char *target, uint32_t target_size)
{
    uint32_t inode_num;
    ext_inode_t inode;

    if (path == NULL || target == NULL || target_size == 0) {
        return -1;
    }

    /* 不跟随符号链接地查找 */
    g_symlink_count = 0;
    if (!extfs_resolve_path_internal(path, &inode_num, false)) {
        return -1;
    }

    if (!extfs_read_inode_raw(inode_num, &inode)) {
        return -1;
    }

    if (!extfs_inode_is_symlink(&inode)) {
        return -1;
    }

    if (!extfs_read_symlink_target(&inode, target, target_size)) {
        return -1;
    }

    return (int32_t) strlen(target);
}

int32_t extfs_read_file_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size)
{
    uint32_t inode_num;
    ext_inode_t inode;
    uint32_t file_size;
    uint8_t *dst = (uint8_t *) buffer;
    uint32_t bytes_left;
    uint32_t bytes_read = 0;

    if (buffer == NULL || !extfs_resolve_path(path, &inode_num)) {
        return -1;
    }
    if (!extfs_read_inode_raw(inode_num, &inode)) {
        return -1;
    }
    if (extfs_inode_is_directory(&inode)) {
        return -1;
    }

    file_size = extfs_inode_file_size(&inode);
    if (offset >= file_size || buffer_size == 0) {
        return 0;
    }

    bytes_left = file_size - offset;
    if (bytes_left > buffer_size) {
        bytes_left = buffer_size;
    }

    while (bytes_left > 0) {
        uint32_t block_index = offset / g_extfs_info.block_size;
        uint32_t block_offset = offset % g_extfs_info.block_size;
        uint32_t chunk = g_extfs_info.block_size - block_offset;
        uint32_t block = extfs_get_block_at(&inode, block_index);

        if (chunk > bytes_left) {
            chunk = bytes_left;
        }

        if (block == 0) {
            memset(dst, 0, chunk);
        } else {
            extfs_read_block(block, g_extfs_block);
            memcpy(dst, g_extfs_block + block_offset, chunk);
        }

        dst += chunk;
        offset += chunk;
        bytes_read += chunk;
        bytes_left -= chunk;
    }

    return (int32_t) bytes_read;
}

int32_t extfs_read_file(const char *path, void *buffer, uint32_t buffer_size)
{
    int32_t size = extfs_file_size(path);
    if (size < 0) {
        return -1;
    }
    if ((uint32_t) size > buffer_size) {
        return -2;
    }
    return extfs_read_file_at(path, 0, buffer, buffer_size);
}

/* 获取当前时间（简单实现，返回固定值） */
static uint32_t extfs_get_current_time(void)
{
    /* 简单返回一个时间戳，实际应该从 RTC 读取 */
    return 0x60000000;
}

/* 截断文件 */
static bool extfs_truncate_file(ext_inode_t *inode, uint32_t new_size)
{
    uint32_t old_blocks = (inode->size + g_extfs_info.block_size - 1) / g_extfs_info.block_size;
    uint32_t new_blocks = (new_size + g_extfs_info.block_size - 1) / g_extfs_info.block_size;

    if (new_size >= inode->size) {
        return true;
    }

    /* 释放多余的块 */
    for (uint32_t i = new_blocks; i < old_blocks; i++) {
        uint32_t block = extfs_get_block_at(inode, i);
        if (block != 0) {
            extfs_free_block(block);
        }
    }

    /* 更新 inode */
    inode->size = new_size;
    inode->mtime = extfs_get_current_time();
    inode->ctime = extfs_get_current_time();

    return true;
}

/* 分配文件块 */
static int32_t extfs_alloc_file_block(ext_inode_t *inode, uint32_t block_index)
{
    uint32_t blocks_per_indirect = g_extfs_info.block_size / 4;

    /* 直接块 */
    if (block_index < 12) {
        if (inode->block[block_index] != 0) {
            return (int32_t) inode->block[block_index];
        }
        int32_t new_block = extfs_alloc_block(0);
        if (new_block < 0) {
            return -1;
        }
        inode->block[block_index] = (uint32_t) new_block;
        return new_block;
    }

    block_index -= 12;

    /* 间接块 */
    if (block_index < blocks_per_indirect) {
        if (inode->block[12] == 0) {
            int32_t new_block = extfs_alloc_block(0);
            if (new_block < 0) {
                return -1;
            }
            inode->block[12] = (uint32_t) new_block;
            /* 清零间接块 */
            memset(g_extfs_block, 0, g_extfs_info.block_size);
            extfs_write_block(new_block, g_extfs_block);
        }

        extfs_read_block(inode->block[12], g_extfs_block);
        uint32_t *indirect = (uint32_t *) g_extfs_block;

        if (indirect[block_index] != 0) {
            return (int32_t) indirect[block_index];
        }

        int32_t new_block = extfs_alloc_block(0);
        if (new_block < 0) {
            return -1;
        }
        indirect[block_index] = (uint32_t) new_block;
        extfs_write_block(inode->block[12], g_extfs_block);
        return new_block;
    }

    /* 双重间接块和三重间接块暂时不支持 */
    return -1;
}

/* 写入文件数据 */
static int32_t extfs_write_inode_data(ext_inode_t *inode, uint32_t offset,
                                       const void *buffer, uint32_t size)
{
    const uint8_t *src = (const uint8_t *) buffer;
    uint32_t bytes_left = size;
    uint32_t bytes_written = 0;

    while (bytes_left > 0) {
        uint32_t block_index = offset / g_extfs_info.block_size;
        uint32_t block_offset = offset % g_extfs_info.block_size;
        uint32_t chunk = g_extfs_info.block_size - block_offset;

        if (chunk > bytes_left) {
            chunk = bytes_left;
        }

        int32_t block = extfs_alloc_file_block(inode, block_index);
        if (block < 0) {
            return -1;
        }

        /* 读取原块（如果不是整块写入） */
        if (block_offset != 0 || chunk != g_extfs_info.block_size) {
            extfs_read_block((uint32_t) block, g_extfs_block);
        }

        /* 写入数据 */
        memcpy(g_extfs_block + block_offset, src, chunk);
        extfs_write_block((uint32_t) block, g_extfs_block);

        src += chunk;
        offset += chunk;
        bytes_written += chunk;
        bytes_left -= chunk;

        /* 更新文件大小 */
        if (offset > inode->size) {
            inode->size = offset;
        }
    }

    inode->mtime = extfs_get_current_time();
    inode->ctime = extfs_get_current_time();

    return (int32_t) bytes_written;
}

/* 添加目录条目 */
static bool extfs_add_dir_entry(uint32_t dir_inode_num, const char *name,
                                 uint32_t inode_num, uint8_t file_type)
{
    ext_inode_t dir_inode;
    uint32_t name_len = (uint32_t) strlen(name);
    uint32_t entry_size;

    if (!extfs_read_inode_raw(dir_inode_num, &dir_inode)) {
        return false;
    }

    if (!extfs_inode_is_directory(&dir_inode)) {
        return false;
    }

    /* 计算条目大小（4 字节对齐） */
    entry_size = 8 + name_len;  /* inode(4) + rec_len(2) + name_len(1) + file_type(1) + name */
    entry_size = (entry_size + 3) & ~3;  /* 4 字节对齐 */

    /* 查找可以插入的位置 */
    uint32_t offset = 0;
    uint32_t dir_size = dir_inode.size;

    while (offset < dir_size) {
        uint32_t block_index = offset / g_extfs_info.block_size;
        uint32_t block_offset = offset % g_extfs_info.block_size;
        uint32_t block = extfs_get_block_at(&dir_inode, block_index);

        if (block == 0) {
            break;
        }

        extfs_read_block(block, g_extfs_block);

        ext_dir_entry_t *entry = (ext_dir_entry_t *) (g_extfs_block + block_offset);

        if (entry->rec_len == 0) {
            break;
        }

        /* 检查是否有足够的空间在这个条目中插入 */
        uint32_t actual_size = 8 + entry->name_len;
        actual_size = (actual_size + 3) & ~3;

        if (entry->rec_len >= actual_size + entry_size) {
            /* 可以拆分这个条目 */
            uint16_t old_rec_len = entry->rec_len;
            entry->rec_len = (uint16_t) actual_size;

            ext_dir_entry_t *new_entry = (ext_dir_entry_t *) ((uint8_t *) entry + actual_size);
            new_entry->inode = inode_num;
            new_entry->rec_len = (uint16_t) (old_rec_len - actual_size);
            new_entry->name_len = (uint8_t) name_len;
            new_entry->file_type = file_type;
            memcpy(new_entry->name, name, name_len);

            extfs_write_block(block, g_extfs_block);

            /* 更新目录 inode 的 mtime */
            dir_inode.mtime = extfs_get_current_time();
            extfs_write_inode_raw(dir_inode_num, &dir_inode);

            return true;
        }

        offset += entry->rec_len;
    }

    /* 需要扩展目录 */
    uint32_t new_block_index = dir_size / g_extfs_info.block_size;
    int32_t new_block = extfs_alloc_file_block(&dir_inode, new_block_index);
    if (new_block < 0) {
        return false;
    }

    /* 清零新块 */
    memset(g_extfs_block, 0, g_extfs_info.block_size);

    /* 在新块中添加条目 */
    ext_dir_entry_t *new_entry = (ext_dir_entry_t *) g_extfs_block;
    new_entry->inode = inode_num;
    new_entry->rec_len = (uint16_t) g_extfs_info.block_size;
    new_entry->name_len = (uint8_t) name_len;
    new_entry->file_type = file_type;
    memcpy(new_entry->name, name, name_len);

    extfs_write_block((uint32_t) new_block, g_extfs_block);

    /* 更新目录大小和 mtime */
    dir_inode.size = (new_block_index + 1) * g_extfs_info.block_size;
    dir_inode.mtime = extfs_get_current_time();
    extfs_write_inode_raw(dir_inode_num, &dir_inode);

    return true;
}

/* 删除目录条目 */
static bool extfs_remove_dir_entry(uint32_t dir_inode_num, const char *name)
{
    ext_inode_t dir_inode;
    uint32_t name_len = (uint32_t) strlen(name);

    if (!extfs_read_inode_raw(dir_inode_num, &dir_inode)) {
        return false;
    }

    if (!extfs_inode_is_directory(&dir_inode)) {
        return false;
    }

    uint32_t offset = 0;
    uint32_t dir_size = dir_inode.size;
    uint32_t prev_offset = 0;

    while (offset < dir_size) {
        uint32_t block_index = offset / g_extfs_info.block_size;
        uint32_t block_offset = offset % g_extfs_info.block_size;
        uint32_t block = extfs_get_block_at(&dir_inode, block_index);

        if (block == 0) {
            break;
        }

        extfs_read_block(block, g_extfs_block);

        ext_dir_entry_t *entry = (ext_dir_entry_t *) (g_extfs_block + block_offset);

        if (entry->rec_len == 0) {
            break;
        }

        if (entry->name_len == name_len &&
            memcmp(entry->name, name, name_len) == 0) {
            /* 找到条目，删除它 */
            if (prev_offset == offset) {
                /* 第一个条目，将 name_len 设为 0 */
                entry->name_len = 0;
                entry->inode = 0;
            } else {
                /* 合并到前一个条目 */
                uint32_t prev_block_index = prev_offset / g_extfs_info.block_size;
                uint32_t prev_block_offset = prev_offset % g_extfs_info.block_size;
                uint32_t prev_block = extfs_get_block_at(&dir_inode, prev_block_index);

                if (prev_block == block) {
                    /* 同一块 */
                    ext_dir_entry_t *prev_entry = (ext_dir_entry_t *) (g_extfs_block + prev_block_offset);
                    prev_entry->rec_len += entry->rec_len;
                } else {
                    /* 不同块，简单处理：将 name_len 设为 0 */
                    entry->name_len = 0;
                    entry->inode = 0;
                }
            }

            extfs_write_block(block, g_extfs_block);

            /* 更新目录 inode 的 mtime */
            dir_inode.mtime = extfs_get_current_time();
            extfs_write_inode_raw(dir_inode_num, &dir_inode);

            return true;
        }

        prev_offset = offset;
        offset += entry->rec_len;
    }

    return false;
}

/* 释放文件的所有块 */
static void extfs_free_file_blocks(ext_inode_t *inode)
{
    uint32_t blocks = (inode->size + g_extfs_info.block_size - 1) / g_extfs_info.block_size;
    uint32_t blocks_per_indirect = g_extfs_info.block_size / 4;

    /* 直接块 */
    for (uint32_t i = 0; i < 12 && i < blocks; i++) {
        if (inode->block[i] != 0) {
            extfs_free_block(inode->block[i]);
            inode->block[i] = 0;
        }
    }

    /* 间接块 */
    if (blocks > 12 && inode->block[12] != 0) {
        extfs_read_block(inode->block[12], g_extfs_block);
        uint32_t *indirect = (uint32_t *) g_extfs_block;

        uint32_t indirect_blocks = blocks - 12;
        if (indirect_blocks > blocks_per_indirect) {
            indirect_blocks = blocks_per_indirect;
        }

        for (uint32_t i = 0; i < indirect_blocks; i++) {
            if (indirect[i] != 0) {
                extfs_free_block(indirect[i]);
            }
        }

        extfs_free_block(inode->block[12]);
        inode->block[12] = 0;
    }

    /* 双重间接块和三重间接块暂时不处理 */
}

int32_t extfs_write_file(const char *path, const void *buffer, uint32_t size)
{
    uint32_t inode_num;
    ext_inode_t inode;
    int32_t result;
    bool is_new = false;

    if (path == NULL || buffer == NULL) {
        return -1;
    }

    /* 检查文件是否存在 */
    if (!extfs_resolve_path(path, &inode_num)) {
        /* 文件不存在，创建新文件 */
        /* 分离目录和文件名 */
        char dir_path[256];
        const char *filename = strrchr(path, '/');

        if (filename == NULL) {
            return -1;
        }

        filename++;  /* 跳过 '/' */

        uint32_t dir_len = (uint32_t) (filename - path - 1);
        if (dir_len >= sizeof(dir_path)) {
            return -1;
        }

        memcpy(dir_path, path, dir_len);
        dir_path[dir_len] = '\0';

        if (dir_len == 0) {
            strcpy(dir_path, "/");
        }

        /* 查找目录 inode */
        uint32_t dir_inode;
        if (!extfs_resolve_path(dir_path, &dir_inode)) {
            return -1;
        }

        /* 分配新 inode */
        int32_t new_inode = extfs_alloc_inode(0);
        if (new_inode < 0) {
            return -1;
        }

        inode_num = (uint32_t) new_inode;
        is_new = true;

        /* 初始化 inode */
        memset(&inode, 0, sizeof(inode));
        inode.mode = EXT_INODE_MODE_REG | 0644;
        inode.uid = 0;
        inode.gid = 0;
        inode.size = 0;
        inode.links_count = 1;
        inode.blocks = 0;
        inode.flags = 0;
        inode.generation = 0;
        inode.atime = extfs_get_current_time();
        inode.ctime = extfs_get_current_time();
        inode.mtime = extfs_get_current_time();
        inode.dtime = 0;

        /* 添加目录条目 */
        if (!extfs_add_dir_entry(dir_inode, filename, inode_num, EXT_DIR_FILE_TYPE)) {
            extfs_free_inode(inode_num);
            return -1;
        }
    } else {
        /* 文件已存在，读取 inode */
        if (!extfs_read_inode_raw(inode_num, &inode)) {
            return -1;
        }

        if (extfs_inode_is_directory(&inode)) {
            return -1;
        }

        /* 截断文件（覆盖写入） */
        extfs_truncate_file(&inode, 0);
    }

    /* 写入数据 */
    result = extfs_write_inode_data(&inode, 0, buffer, size);
    if (result < 0) {
        if (is_new) {
            /* 失败时清理 */
            extfs_free_file_blocks(&inode);
            extfs_free_inode(inode_num);
        }
        return -1;
    }

    /* 写回 inode */
    extfs_write_inode_raw(inode_num, &inode);

    return result;
}

bool extfs_delete(const char *path)
{
    uint32_t inode_num;
    ext_inode_t inode;

    if (path == NULL) {
        return false;
    }

    /* 不跟随符号链接地查找 */
    g_symlink_count = 0;
    if (!extfs_resolve_path_internal(path, &inode_num, false)) {
        return false;
    }

    if (!extfs_read_inode_raw(inode_num, &inode)) {
        return false;
    }

    if (extfs_inode_is_directory(&inode)) {
        return false;  /* 不能用 delete 删除目录 */
    }

    /* 分离目录和文件名 */
    char dir_path[256];
    const char *filename = strrchr(path, '/');

    if (filename == NULL) {
        return false;
    }

    filename++;
    uint32_t dir_len = (uint32_t) (filename - path - 1);
    if (dir_len >= sizeof(dir_path)) {
        return false;
    }

    memcpy(dir_path, path, dir_len);
    dir_path[dir_len] = '\0';

    if (dir_len == 0) {
        strcpy(dir_path, "/");
    }

    /* 查找目录 inode */
    uint32_t dir_inode;
    if (!extfs_resolve_path(dir_path, &dir_inode)) {
        return false;
    }

    /* 删除目录条目 */
    if (!extfs_remove_dir_entry(dir_inode, filename)) {
        return false;
    }

    /* 减少链接计数 */
    inode.links_count--;

    if (inode.links_count == 0) {
        /* 释放文件块 */
        extfs_free_file_blocks(&inode);

        /* 释放 inode */
        extfs_free_inode(inode_num);
    } else {
        /* 更新 inode */
        inode.ctime = extfs_get_current_time();
        extfs_write_inode_raw(inode_num, &inode);
    }

    return true;
}

bool extfs_mkdir(const char *path)
{
    uint32_t inode_num;
    ext_inode_t inode;

    if (path == NULL) {
        return false;
    }

    /* 检查目录是否已存在 */
    if (extfs_exists(path)) {
        return false;
    }

    /* 分离父目录和目录名 */
    char dir_path[256];
    const char *dirname = strrchr(path, '/');

    if (dirname == NULL) {
        return false;
    }

    dirname++;
    uint32_t dir_len = (uint32_t) (dirname - path - 1);
    if (dir_len >= sizeof(dir_path)) {
        return false;
    }

    memcpy(dir_path, path, dir_len);
    dir_path[dir_len] = '\0';

    if (dir_len == 0) {
        strcpy(dir_path, "/");
    }

    /* 查找父目录 inode */
    uint32_t parent_inode;
    if (!extfs_resolve_path(dir_path, &parent_inode)) {
        return false;
    }

    /* 分配新 inode */
    int32_t new_inode = extfs_alloc_inode(0);
    if (new_inode < 0) {
        return false;
    }

    inode_num = (uint32_t) new_inode;

    /* 初始化目录 inode */
    memset(&inode, 0, sizeof(inode));
    inode.mode = EXT_INODE_MODE_DIR | 0755;
    inode.uid = 0;
    inode.gid = 0;
    inode.size = 0;
    inode.links_count = 2;  /* . 和 父目录 */
    inode.blocks = 0;
    inode.flags = 0;
    inode.generation = 0;
    inode.atime = extfs_get_current_time();
    inode.ctime = extfs_get_current_time();
    inode.mtime = extfs_get_current_time();
    inode.dtime = 0;

    /* 分配第一个目录块 */
    int32_t first_block = extfs_alloc_file_block(&inode, 0);
    if (first_block < 0) {
        extfs_free_inode(inode_num);
        return false;
    }

    /* 初始化目录块，添加 . 和 .. */
    memset(g_extfs_block, 0, g_extfs_info.block_size);

    /* . 条目 */
    ext_dir_entry_t *dot_entry = (ext_dir_entry_t *) g_extfs_block;
    dot_entry->inode = inode_num;
    dot_entry->rec_len = 12;  /* 4 + 2 + 1 + 1 + 1 + 3 对齐 */
    dot_entry->name_len = 1;
    dot_entry->file_type = EXT_DIR_DIR_TYPE;
    dot_entry->name[0] = '.';

    /* .. 条目 */
    ext_dir_entry_t *dotdot_entry = (ext_dir_entry_t *) (g_extfs_block + 12);
    dotdot_entry->inode = parent_inode;
    dotdot_entry->rec_len = (uint16_t) (g_extfs_info.block_size - 12);
    dotdot_entry->name_len = 2;
    dotdot_entry->file_type = EXT_DIR_DIR_TYPE;
    dotdot_entry->name[0] = '.';
    dotdot_entry->name[1] = '.';

    extfs_write_block((uint32_t) first_block, g_extfs_block);

    /* 更新目录大小 */
    inode.size = g_extfs_info.block_size;

    /* 写回 inode */
    extfs_write_inode_raw(inode_num, &inode);

    /* 在父目录中添加条目 */
    if (!extfs_add_dir_entry(parent_inode, dirname, inode_num, EXT_DIR_DIR_TYPE)) {
        extfs_free_file_blocks(&inode);
        extfs_free_inode(inode_num);
        return false;
    }

    /* 更新父目录的链接计数 */
    ext_inode_t parent_inode_data;
    if (extfs_read_inode_raw(parent_inode, &parent_inode_data)) {
        parent_inode_data.links_count++;
        extfs_write_inode_raw(parent_inode, &parent_inode_data);
    }

    return true;
}

bool extfs_rmdir(const char *path)
{
    uint32_t inode_num;
    ext_inode_t inode;

    if (path == NULL) {
        return false;
    }

    /* 不跟随符号链接地查找 */
    g_symlink_count = 0;
    if (!extfs_resolve_path_internal(path, &inode_num, false)) {
        return false;
    }

    if (!extfs_read_inode_raw(inode_num, &inode)) {
        return false;
    }

    if (!extfs_inode_is_directory(&inode)) {
        return false;
    }

    /* 检查目录是否为空（除了 . 和 ..） */
    char buffer[4096];
    if (extfs_list_dir(path, buffer, sizeof(buffer))) {
        /* 如果 buffer 非空，说明目录有内容 */
        if (buffer[0] != '\0') {
            return false;  /* 目录非空 */
        }
    }

    /* 分离父目录和目录名 */
    char dir_path[256];
    const char *dirname = strrchr(path, '/');

    if (dirname == NULL) {
        return false;
    }

    dirname++;
    uint32_t dir_len = (uint32_t) (dirname - path - 1);
    if (dir_len >= sizeof(dir_path)) {
        return false;
    }

    memcpy(dir_path, path, dir_len);
    dir_path[dir_len] = '\0';

    if (dir_len == 0) {
        strcpy(dir_path, "/");
    }

    /* 查找父目录 inode */
    uint32_t parent_inode;
    if (!extfs_resolve_path(dir_path, &parent_inode)) {
        return false;
    }

    /* 从父目录中删除条目 */
    if (!extfs_remove_dir_entry(parent_inode, dirname)) {
        return false;
    }

    /* 释放目录块 */
    extfs_free_file_blocks(&inode);

    /* 释放 inode */
    extfs_free_inode(inode_num);

    /* 更新父目录的链接计数 */
    ext_inode_t parent_inode_data;
    if (extfs_read_inode_raw(parent_inode, &parent_inode_data)) {
        if (parent_inode_data.links_count > 2) {
            parent_inode_data.links_count--;
        }
        extfs_write_inode_raw(parent_inode, &parent_inode_data);
    }

    return true;
}

bool extfs_list_dir(const char *path, char *buffer, uint32_t buffer_size)
{
    uint32_t dir_inode;
    ext_inode_t inode;
    uint32_t file_size;
    uint32_t offset = 0;
    uint32_t used = 0;

    if (buffer == NULL || buffer_size == 0) {
        return false;
    }

    if (!extfs_resolve_path(path, &dir_inode)) {
        return false;
    }
    if (!extfs_read_inode_raw(dir_inode, &inode)) {
        return false;
    }
    if (!extfs_inode_is_directory(&inode)) {
        return false;
    }

    file_size = extfs_inode_file_size(&inode);
    buffer[0] = '\0';

    while (offset < file_size) {
        uint32_t block_index = offset / g_extfs_info.block_size;
        uint32_t block_offset = offset % g_extfs_info.block_size;
        uint32_t block = extfs_get_block_at(&inode, block_index);

        if (block == 0) {
            break;
        }

        extfs_read_block(block, g_extfs_block);

        ext_dir_entry_t *entry = (ext_dir_entry_t *) (g_extfs_block + block_offset);

        if (entry->rec_len == 0) {
            break;
        }

        if (entry->name_len > 0) {
            if ((entry->name_len == 1 && entry->name[0] == '.') ||
                (entry->name_len == 2 && entry->name[0] == '.' && entry->name[1] == '.')) {
                offset += entry->rec_len;
                continue;
            }

            uint32_t name_len = entry->name_len;
            if (used + name_len + 4 >= buffer_size) {
                return false;
            }

            memcpy(buffer + used, entry->name, name_len);
            used += name_len;

            if (entry->file_type == EXT_DIR_DIR_TYPE) {
                buffer[used++] = '/';
            } else if (entry->file_type == EXT_DIR_SYMLINK_TYPE) {
                buffer[used++] = '@';
            }

            buffer[used++] = '\n';
            buffer[used] = '\0';
        }

        offset += entry->rec_len;
    }

    return true;
}

bool extfs_list_root(char *buffer, uint32_t buffer_size)
{
    return extfs_list_dir("/", buffer, buffer_size);
}

const extfs_info_t *extfs_info(void)
{
    return &g_extfs_info;
}

const char *extfs_status(void)
{
    return g_extfs_info.status;
}
