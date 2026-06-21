#include "common.h"
#include "ntfs.h"

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

static ntfs_info_t g_ntfs_info;

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

static uint64_t read_le64(const uint8_t *data)
{
    return (uint64_t) read_le32(data) | ((uint64_t) read_le32(data + 4) << 32);
}

static uint32_t ntfs_unit_size_from_clusters(int8_t value, uint32_t cluster_size)
{
    if (value < 0) {
        uint8_t shift = (uint8_t) -value;

        if (shift < 31) {
            return 1u << shift;
        }
        return 0;
    }
    return (uint32_t) value * cluster_size;
}

static bool ntfs_partition_type(uint8_t type)
{
    return type == 0x07;
}

static bool ntfs_parse_boot(uint32_t volume_lba, const uint8_t sector[512])
{
    uint16_t bytes_per_sector = read_le16(sector + 11);
    uint8_t sectors_per_cluster = sector[13];
    uint8_t mft_sector[512];
    uint64_t mft0_lba64;

    if (memcmp(sector + 3, "NTFS    ", 8) != 0 ||
        read_le16(sector + 510) != 0xAA55 ||
        bytes_per_sector != 512 ||
        sectors_per_cluster == 0) {
        return false;
    }

    memset(&g_ntfs_info, 0, sizeof(g_ntfs_info));
    g_ntfs_info.present = true;
    g_ntfs_info.read_only = true;
    g_ntfs_info.volume_lba = volume_lba;
    g_ntfs_info.bytes_per_sector = bytes_per_sector;
    g_ntfs_info.sectors_per_cluster = sectors_per_cluster;
    g_ntfs_info.total_sectors = read_le64(sector + 40);
    g_ntfs_info.mft_lcn = read_le64(sector + 48);
    g_ntfs_info.mftmirr_lcn = read_le64(sector + 56);
    g_ntfs_info.serial_low = read_le32(sector + 72);
    g_ntfs_info.serial_high = read_le32(sector + 76);
    g_ntfs_info.cluster_size = (uint32_t) bytes_per_sector * sectors_per_cluster;
    g_ntfs_info.mft_record_size = ntfs_unit_size_from_clusters((int8_t) sector[64], g_ntfs_info.cluster_size);
    g_ntfs_info.index_record_size = ntfs_unit_size_from_clusters((int8_t) sector[68], g_ntfs_info.cluster_size);
    mft0_lba64 = (uint64_t) volume_lba + g_ntfs_info.mft_lcn * sectors_per_cluster;
    if (mft0_lba64 <= 0x0FFFFFFFULL) {
        g_ntfs_info.mft0_lba = (uint32_t) mft0_lba64;
        ata_read_sector(g_ntfs_info.mft0_lba, mft_sector);
        g_ntfs_info.mft0_readable = memcmp(mft_sector, "FILE", 4) == 0;
    }
    strcpy(g_ntfs_info.status, g_ntfs_info.mft0_readable ? "ntfs: boot/mft metadata ready" : "ntfs: boot metadata ready");
    return true;
}

bool ntfs_init(void)
{
    uint8_t sector[512];

    memset(&g_ntfs_info, 0, sizeof(g_ntfs_info));
    strcpy(g_ntfs_info.status, "ntfs: not found");

    ata_read_sector(0, sector);
    if (ntfs_parse_boot(0, sector)) {
        return true;
    }
    if (read_le16(sector + 510) == 0xAA55) {
        for (uint8_t i = 0; i < 4; i++) {
            uint8_t *entry = sector + 446 + (uint32_t) i * 16;
            uint32_t lba = read_le32(entry + 8);

            if (!ntfs_partition_type(entry[4]) || lba == 0) {
                continue;
            }
            ata_read_sector(lba, sector);
            if (ntfs_parse_boot(lba, sector)) {
                return true;
            }
        }
    }
    strcpy(g_ntfs_info.status, "ntfs: no volume detected");
    return false;
}

uint16_t ntfs_root_entry_count(void)
{
    return 0;
}

bool ntfs_exists(const char *path)
{
    return g_ntfs_info.present && path != NULL && path[0] == '/' && path[1] == '\0';
}

bool ntfs_is_dir(const char *path)
{
    return ntfs_exists(path);
}

int32_t ntfs_file_size(const char *path)
{
    (void) path;
    return -1;
}

int32_t ntfs_read_file(const char *path, void *buffer, uint32_t buffer_size)
{
    (void) path;
    (void) buffer;
    (void) buffer_size;
    return -1;
}

int32_t ntfs_read_file_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size)
{
    (void) path;
    (void) offset;
    (void) buffer;
    (void) buffer_size;
    return -1;
}

int32_t ntfs_write_file(const char *path, const void *buffer, uint32_t size)
{
    (void) path;
    (void) buffer;
    (void) size;
    return -1;
}

bool ntfs_delete(const char *path)
{
    (void) path;
    return false;
}

bool ntfs_mkdir(const char *path)
{
    (void) path;
    return false;
}

bool ntfs_rmdir(const char *path)
{
    (void) path;
    return false;
}

bool ntfs_list_dir(const char *path, char *buffer, uint32_t buffer_size)
{
    if (!g_ntfs_info.present || path == NULL || buffer == NULL || buffer_size == 0 || !ntfs_is_dir(path)) {
        return false;
    }
    buffer[0] = '\0';
    return true;
}

bool ntfs_list_root(char *buffer, uint32_t buffer_size)
{
    return ntfs_list_dir("/", buffer, buffer_size);
}

const ntfs_info_t *ntfs_info(void)
{
    return &g_ntfs_info;
}

const char *ntfs_status(void)
{
    return g_ntfs_info.status;
}
