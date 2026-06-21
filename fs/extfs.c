#include "common.h"
#include "extfs.h"

#define ATA_DATA_PORT         0x1F0
#define ATA_SECTOR_COUNT_PORT 0x1F2
#define ATA_LBA_LOW_PORT      0x1F3
#define ATA_LBA_MID_PORT      0x1F4
#define ATA_LBA_HIGH_PORT     0x1F5
#define ATA_DRIVE_PORT        0x1F6
#define ATA_COMMAND_PORT      0x1F7
#define ATA_STATUS_PORT       0x1F7

#define ATA_CMD_READ_SECTORS  0x20
#define ATA_STATUS_BSY        0x80
#define ATA_STATUS_DRQ        0x08
#define EXT_SUPER_OFFSET      1024U
#define EXT_SUPER_MAGIC       0xEF53U

static extfs_info_t g_extfs_info;

static void ata_wait_not_busy(void)
{
    while ((inb(ATA_STATUS_PORT) & ATA_STATUS_BSY) != 0) {
    }
}

static void ata_wait_data_ready(void)
{
    ata_wait_not_busy();
    while ((inb(ATA_STATUS_PORT) & ATA_STATUS_DRQ) == 0) {
    }
}

static void ata_read_sector(uint32_t lba, void *buffer)
{
    uint16_t *dst = (uint16_t *) buffer;

    ata_wait_not_busy();
    outb(ATA_DRIVE_PORT, (uint8_t) (0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT_PORT, 1);
    outb(ATA_LBA_LOW_PORT, (uint8_t) (lba & 0xFF));
    outb(ATA_LBA_MID_PORT, (uint8_t) ((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH_PORT, (uint8_t) ((lba >> 16) & 0xFF));
    outb(ATA_COMMAND_PORT, ATA_CMD_READ_SECTORS);

    ata_wait_data_ready();
    for (uint32_t i = 0; i < 256; i++) {
        dst[i] = inw(ATA_DATA_PORT);
    }
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

static bool ext_partition_type(uint8_t type)
{
    return type == 0x83;
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
    strcpy(g_extfs_info.status, "extfs: ext volume detected (metadata only)");
    return true;
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
    return 0;
}

bool extfs_exists(const char *path)
{
    return g_extfs_info.present && path != NULL && path[0] == '/' && path[1] == '\0';
}

bool extfs_is_dir(const char *path)
{
    return extfs_exists(path);
}

int32_t extfs_file_size(const char *path)
{
    (void) path;
    return -1;
}

int32_t extfs_read_file(const char *path, void *buffer, uint32_t buffer_size)
{
    (void) path;
    (void) buffer;
    (void) buffer_size;
    return -1;
}

int32_t extfs_read_file_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size)
{
    (void) path;
    (void) offset;
    (void) buffer;
    (void) buffer_size;
    return -1;
}

int32_t extfs_write_file(const char *path, const void *buffer, uint32_t size)
{
    (void) path;
    (void) buffer;
    (void) size;
    return -1;
}

bool extfs_delete(const char *path)
{
    (void) path;
    return false;
}

bool extfs_mkdir(const char *path)
{
    (void) path;
    return false;
}

bool extfs_rmdir(const char *path)
{
    (void) path;
    return false;
}

bool extfs_list_dir(const char *path, char *buffer, uint32_t buffer_size)
{
    if (!g_extfs_info.present || path == NULL || buffer == NULL || buffer_size == 0 || !extfs_is_dir(path)) {
        return false;
    }
    buffer[0] = '\0';
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
