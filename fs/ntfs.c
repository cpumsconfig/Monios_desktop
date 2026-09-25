#include "common.h"
#include "file.h"
#include "blockdev.h"
#include "kernel.h"
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
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_STATUS_BSY        0x80
#define ATA_STATUS_DRQ        0x08
#define ATA_WAIT_LIMIT        1000000U

#define NTFS_ATTR_STANDARD_INFORMATION 0x10
#define NTFS_ATTR_ATTRIBUTE_LIST       0x20
#define NTFS_ATTR_FILE_NAME            0x30
#define NTFS_ATTR_OBJECT_ID            0x40
#define NTFS_ATTR_SECURITY_DESCRIPTOR  0x50
#define NTFS_ATTR_VOLUME_NAME          0x60
#define NTFS_ATTR_VOLUME_INFORMATION   0x70
#define NTFS_ATTR_DATA                 0x80
#define NTFS_ATTR_INDEX_ROOT           0x90
#define NTFS_ATTR_INDEX_ALLOCATION     0xA0
#define NTFS_ATTR_BITMAP               0xB0
#define NTFS_ATTR_REPARSE_POINT        0xC0
#define NTFS_ATTR_EA_INFORMATION       0xD0
#define NTFS_ATTR_EA                   0xE0
#define NTFS_ATTR_LOGGED_UTILITY_STREAM 0x100

#define NTFS_MFT_RECORD_FLAG_IN_USE    0x0001
#define NTFS_MFT_RECORD_FLAG_DIRECTORY 0x0002

#define NTFS_INDEX_ENTRY_FLAG_END      0x0001
#define NTFS_INDEX_ENTRY_FLAG_LAST     0x0002

#define NTFS_MAX_PATH                  256
#define NTFS_MAX_NAME_LEN              255

typedef struct {
    uint32_t type;
    uint32_t length;
    uint8_t non_resident;
    uint8_t name_length;
    uint16_t name_offset;
    uint16_t flags;
    uint16_t attribute_id;
    union {
        struct {
            uint32_t value_length;
            uint16_t value_offset;
            uint8_t indexed;
            uint8_t padding;
        } resident;
        struct {
            uint64_t starting_vcn;
            uint64_t last_vcn;
            uint16_t mapping_pairs_offset;
            uint16_t compression_unit;
            uint32_t padding;
            uint64_t allocated_size;
            uint64_t data_size;
            uint64_t initialized_size;
        } non_resident;
    } data;
} __attribute__((packed)) ntfs_attr_header_t;

typedef struct {
    uint8_t magic[4];
    uint16_t update_sequence_offset;
    uint16_t update_sequence_size;
    uint64_t log_file_sequence_number;
    uint16_t sequence_number;
    uint16_t hard_link_count;
    uint16_t first_attribute_offset;
    uint16_t flags;
    uint32_t bytes_in_use;
    uint32_t bytes_allocated;
    uint64_t base_record_reference;
    uint16_t next_attribute_id;
    uint16_t padding;
    uint32_t mft_record_number;
} __attribute__((packed)) ntfs_mft_record_header_t;

typedef struct {
    uint64_t file_reference;
    uint16_t length;
    uint16_t name_length;
    uint8_t flags;
    uint8_t padding[3];
    uint64_t parent_directory;
    uint64_t creation_time;
    uint64_t last_data_change_time;
    uint64_t last_mft_change_time;
    uint64_t last_access_time;
    uint64_t allocated_size;
    uint64_t data_size;
    uint32_t file_attributes;
    uint32_t reparse_point_tag;
    uint8_t name[1];
} __attribute__((packed)) ntfs_file_name_attr_t;

typedef struct {
    uint64_t index_entry_length;
    uint16_t key_length;
    uint16_t flags;
    uint64_t file_reference;
    uint8_t key[1];
} __attribute__((packed)) ntfs_index_entry_t;

typedef struct {
    uint32_t type;
    uint32_t collation_rule;
    uint32_t index_entry_size;
    uint8_t clusters_per_index_record;
    uint8_t padding[3];
    uint8_t index_header[1];
} __attribute__((packed)) ntfs_index_root_t;

typedef struct {
    uint32_t entries_offset;
    uint32_t total_entries_size;
    uint32_t allocated_entries_size;
    uint8_t flags;
    uint8_t padding[3];
} __attribute__((packed)) ntfs_index_header_t;

static ntfs_info_t g_ntfs_info;
static uint8_t g_mft_buffer[4096];
static uint8_t g_index_buffer[65536];

static bool ntfs_compare_name(const char *name, const uint16_t *utf16_name, uint32_t name_len);

/* 本卷所在的块设备序号，由 ntfs_init() 从 file_blockdev_hint() 取。
 * >= 0 时所有扇区 I/O 走 drivers/storage/blockdev.c 的块设备抽象；
 * < 0（未知）时回退到下面的 legacy ATA PIO。
 *
 * 为什么必须走块设备层：这些驱动过去直接读写 0x1F0 端口，在没有 legacy IDE
 * 控制器的机器上（AHCI/NVMe-only，含 QEMU -machine q35）端口读回全 0，
 * 文件系统完全挂不上。 */
static int32_t g_ntfs_blockdev = -1;

/* 写失败只报一次，避免坏设备上刷屏。 */
static bool g_ntfs_write_warned;

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
    if (g_ntfs_blockdev >= 0) {
        if (!blockdev_raw_read_sectors(g_ntfs_blockdev, lba, 1, buffer)) {
            memset(buffer, 0, 512);
        }
        return;
    }

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

static void ata_read_sectors(uint32_t lba, uint8_t count, void *buffer)
{
    if (g_ntfs_blockdev >= 0) {
        if (!blockdev_raw_read_sectors(g_ntfs_blockdev, lba, count, buffer)) {
            memset(buffer, 0, (uint32_t) count * 512u);
        }
        return;
    }

    uint16_t *dst = (uint16_t *) buffer;

    if (count == 0) {
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
    if (g_ntfs_blockdev >= 0) {
        if (!blockdev_raw_write_sectors(g_ntfs_blockdev, lba, 1, buffer) && !g_ntfs_write_warned) {
            g_ntfs_write_warned = true;
            log_write("fs: warning - block device rejected write (volume read-only)");
        }
        return;
    }

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
    (void) ata_wait_not_busy();
}

static void ata_write_sectors(uint32_t lba, uint8_t count, const void *buffer)
{
    if (g_ntfs_blockdev >= 0) {
        if (!blockdev_raw_write_sectors(g_ntfs_blockdev, lba, count, buffer) && !g_ntfs_write_warned) {
            g_ntfs_write_warned = true;
            log_write("fs: warning - block device rejected write (volume read-only)");
        }
        return;
    }

    const uint16_t *src = (const uint16_t *) buffer;

    if (count == 0 || !ata_wait_not_busy()) {
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

static uint64_t ntfs_cluster_to_lba(uint64_t cluster)
{
    return (uint64_t) g_ntfs_info.volume_lba + cluster * g_ntfs_info.sectors_per_cluster;
}

static bool ntfs_read_mft_record(uint64_t mft_record_number, uint8_t *buffer)
{
    uint64_t mft_lba;
    uint32_t record_size;
    uint32_t sectors_per_record;

    if (!g_ntfs_info.present) {
        return false;
    }

    record_size = g_ntfs_info.mft_record_size;
    if (record_size == 0 || record_size > 4096) {
        return false;
    }

    sectors_per_record = record_size / 512;
    if (sectors_per_record == 0) {
        sectors_per_record = 1;
    }

    mft_lba = ntfs_cluster_to_lba(g_ntfs_info.mft_lcn) + mft_record_number * sectors_per_record;
    if (mft_lba > 0x0FFFFFFFULL) {
        return false;
    }

    ata_read_sectors((uint32_t) mft_lba, (uint8_t) sectors_per_record, buffer);

    if (memcmp(buffer, "FILE", 4) != 0) {
        return false;
    }

    return true;
}

/* 把已修改的 MFT 记录写回磁盘（与 ntfs_read_mft_record 对称）。 */
static bool ntfs_write_mft_record(uint64_t mft_record_number, const uint8_t *buffer)
{
    uint64_t mft_lba;
    uint32_t record_size;
    uint32_t sectors_per_record;

    if (!g_ntfs_info.present) {
        return false;
    }

    record_size = g_ntfs_info.mft_record_size;
    if (record_size == 0 || record_size > 4096) {
        return false;
    }

    sectors_per_record = record_size / 512;
    if (sectors_per_record == 0) {
        sectors_per_record = 1;
    }

    mft_lba = ntfs_cluster_to_lba(g_ntfs_info.mft_lcn) + mft_record_number * sectors_per_record;
    if (mft_lba > 0x0FFFFFFFULL) {
        return false;
    }

    /* 一次最多写 8 个扇区（PIO count 寄存器为 8 位，0 表示 256）。 */
    while (sectors_per_record > 0) {
        uint8_t chunk = sectors_per_record > 8 ? 8u : (uint8_t) sectors_per_record;
        ata_write_sectors((uint32_t) mft_lba, chunk, buffer);
        mft_lba += chunk;
        buffer += chunk * 512u;
        sectors_per_record -= chunk;
    }
    return true;
}

static ntfs_attr_header_t *ntfs_find_attribute(uint8_t *mft_record, uint32_t attr_type, uint8_t *name, uint32_t name_len)
{
    ntfs_mft_record_header_t *header = (ntfs_mft_record_header_t *) mft_record;
    uint32_t offset = header->first_attribute_offset;
    uint32_t record_size = header->bytes_in_use;

    if (mft_record == NULL ||
        record_size == 0 ||
        record_size > g_ntfs_info.mft_record_size ||
        offset < sizeof(ntfs_mft_record_header_t)) {
        return NULL;
    }

    while (offset + sizeof(ntfs_attr_header_t) <= record_size) {
        ntfs_attr_header_t *attr = (ntfs_attr_header_t *) (mft_record + offset);

        if (attr->type == 0xFFFFFFFF) {
            break;
        }
        if (attr->length < sizeof(ntfs_attr_header_t) ||
            attr->length > record_size - offset) {
            break;
        }

        if (attr->type == attr_type) {
            if (name == NULL || name_len == 0) {
                if (attr->name_length == 0) {
                    return attr;
                }
            } else if (attr->name_length == name_len &&
                       attr->name_offset >= sizeof(ntfs_attr_header_t) &&
                       attr->name_offset + name_len * 2 <= attr->length) {
                uint8_t *attr_name = mft_record + offset + attr->name_offset;
                if (memcmp(attr_name, name, name_len * 2) == 0) {
                    return attr;
                }
            }
        }

        if (attr->length == 0) {
            return NULL;
        }
        offset += attr->length;
    }

    return NULL;
}

static bool ntfs_get_attribute_data(uint8_t *mft_record, ntfs_attr_header_t *attr, uint64_t vcn, uint8_t **data_out, uint32_t *size_out)
{
    uint32_t attr_offset;
    uint32_t record_size;

    (void)vcn;  /* resident-only; non-resident path handled by ntfs_read_non_resident_data */
    if (mft_record == NULL || attr == NULL || data_out == NULL || size_out == NULL) {
        return false;
    }
    record_size = g_ntfs_info.mft_record_size;
    attr_offset = (uint32_t) ((uint8_t *) attr - mft_record);
    if (record_size == 0 ||
        record_size > sizeof(g_mft_buffer) ||
        attr_offset >= record_size ||
        attr->length < sizeof(ntfs_attr_header_t) ||
        attr_offset + attr->length > record_size) {
        return false;
    }

    if (!attr->non_resident) {
        uint32_t value_offset = attr->data.resident.value_offset;
        uint32_t value_length = attr->data.resident.value_length;
        uint32_t offset;

        if (value_offset < sizeof(ntfs_attr_header_t) ||
            value_offset > attr->length ||
            value_length > attr->length - value_offset) {
            return false;
        }
        offset = attr_offset + value_offset;
        if (offset + value_length > record_size) {
            return false;
        }
        *data_out = mft_record + offset;
        *size_out = value_length;
        return true;
    }

    return false;
}

static int64_t ntfs_decode_run_length(const uint8_t **data_ptr, uint8_t length_bytes)
{
    const uint8_t *data = *data_ptr;
    int64_t length = 0;

    for (uint8_t i = 0; i < length_bytes; i++) {
        length |= (int64_t) data[i] << (i * 8);
    }

    if (length_bytes > 0 && (data[length_bytes - 1] & 0x80)) {
        for (uint8_t i = length_bytes; i < 8; i++) {
            length |= (int64_t) 0xFF << (i * 8);
        }
    }

    *data_ptr = data + length_bytes;
    return length;
}

__attribute__((unused)) static uint64_t ntfs_decode_run_cluster(const uint8_t **data_ptr, uint8_t cluster_bytes)
{
    const uint8_t *data = *data_ptr;
    uint64_t cluster = 0;

    for (uint8_t i = 0; i < cluster_bytes; i++) {
        cluster |= (uint64_t) data[i] << (i * 8);
    }

    if (cluster_bytes > 0 && (data[cluster_bytes - 1] & 0x80)) {
        for (uint8_t i = cluster_bytes; i < 8; i++) {
            cluster |= (uint64_t) 0xFF << (i * 8);
        }
    }

    *data_ptr = data + cluster_bytes;
    return cluster;
}

static bool ntfs_read_non_resident_data(uint8_t *mft_record, ntfs_attr_header_t *attr, uint64_t offset, uint8_t *buffer, uint32_t size)
{
    (void)mft_record;  /* operates from attr mapping pairs only */
    const uint8_t *run_ptr;
    uint64_t current_vcn = 0;
    uint64_t current_cluster = 0;
    uint64_t run_length = 0;
    uint64_t data_size;
    uint64_t cluster_size;
    uint64_t start_cluster;
    uint64_t start_offset_in_cluster;
    uint64_t bytes_left;
    uint8_t *dst = buffer;

    if (attr == NULL || !attr->non_resident) {
        return false;
    }

    data_size = attr->data.non_resident.data_size;
    cluster_size = g_ntfs_info.cluster_size;

    if (offset >= data_size) {
        return false;
    }

    bytes_left = data_size - offset;
    if (bytes_left > size) {
        bytes_left = size;
    }

    run_ptr = (uint8_t *) attr + attr->data.non_resident.mapping_pairs_offset;
    start_cluster = offset / cluster_size;
    start_offset_in_cluster = offset % cluster_size;

    while (current_vcn < start_cluster + (bytes_left + cluster_size - 1) / cluster_size) {
        uint8_t header;
        uint8_t length_bytes;
        uint8_t cluster_bytes;
        int64_t run_cluster_delta;

        if (*run_ptr == 0) {
            break;
        }

        header = *run_ptr++;
        length_bytes = header & 0x0F;
        cluster_bytes = (header >> 4) & 0x0F;

        if (length_bytes == 0 || length_bytes > 8) {
            break;
        }

        run_length = (uint64_t) ntfs_decode_run_length(&run_ptr, length_bytes);

        if (cluster_bytes > 0) {
            run_cluster_delta = ntfs_decode_run_length(&run_ptr, cluster_bytes);
            current_cluster += (uint64_t) run_cluster_delta;
        }

        if (current_vcn + run_length > start_cluster) {
            uint64_t run_start_vcn = current_vcn;
            uint64_t run_end_vcn = current_vcn + run_length;
            uint64_t read_start_vcn;
            uint64_t read_end_vcn;
            uint64_t read_clusters;
            uint64_t read_offset;

            read_start_vcn = start_cluster > run_start_vcn ? start_cluster : run_start_vcn;
            read_end_vcn = start_cluster + (bytes_left + cluster_size - 1) / cluster_size;
            if (read_end_vcn > run_end_vcn) {
                read_end_vcn = run_end_vcn;
            }

            if (read_end_vcn > read_start_vcn) {
                uint64_t cluster_offset = read_start_vcn - run_start_vcn;
                uint64_t lba = ntfs_cluster_to_lba(current_cluster + cluster_offset);

                read_clusters = read_end_vcn - read_start_vcn;
                read_offset = 0;

                if (read_start_vcn == start_cluster && start_offset_in_cluster > 0) {
                    read_offset = start_offset_in_cluster;
                }

                for (uint64_t c = 0; c < read_clusters && bytes_left > 0; c++) {
                    uint64_t cluster_lba = lba + c * g_ntfs_info.sectors_per_cluster;
                    uint8_t sector_buffer[512];
                    uint32_t s = 0;
                    uint32_t first_byte = 0;

                    if (c == 0 && read_offset > 0) {
                        s = (uint32_t) (read_offset / 512u);
                        first_byte = (uint32_t) (read_offset % 512u);
                    }

                    for (; s < g_ntfs_info.sectors_per_cluster && bytes_left > 0; ) {
                        if (first_byte == 0 && bytes_left >= 512u) {
                            uint32_t whole_sectors = (uint32_t) (bytes_left / 512u);

                            if (whole_sectors > g_ntfs_info.sectors_per_cluster - s) {
                                whole_sectors = g_ntfs_info.sectors_per_cluster - s;
                            }
                            if (whole_sectors > 255u) {
                                whole_sectors = 255u;
                            }
                            if (whole_sectors > 1u) {
                                if (cluster_lba + s + whole_sectors - 1u > 0x0FFFFFFFULL) {
                                    return false;
                                }
                                ata_read_sectors((uint32_t) (cluster_lba + s), (uint8_t) whole_sectors, dst);
                                dst += whole_sectors * 512u;
                                bytes_left -= whole_sectors * 512u;
                                s += whole_sectors;
                                continue;
                            }
                        }

                        uint32_t chunk = 512;
                        if (first_byte > 0) {
                            chunk = 512u - first_byte;
                        }
                        if (chunk > bytes_left) {
                            chunk = (uint32_t) bytes_left;
                        }

                        if (cluster_lba + s > 0x0FFFFFFFULL) {
                            return false;
                        }
                        ata_read_sector((uint32_t) (cluster_lba + s), sector_buffer);
                        memcpy(dst, sector_buffer + first_byte, chunk);
                        dst += chunk;
                        bytes_left -= chunk;
                        first_byte = 0;
                        s++;
                    }
                }
            }
        }

        current_vcn += run_length;
    }

    return true;
}

/* 在非驻留属性的 runlist 中，查找给定 VCN（簇号，从 0 开始）对应的 LCN。 */
static bool ntfs_runlist_lookup_lcn(ntfs_attr_header_t *attr, uint64_t vcn, uint64_t *lcn_out)
{
    const uint8_t *run_ptr;
    uint64_t current_vcn = 0;
    int64_t current_cluster = 0;

    if (attr == NULL || !attr->non_resident) {
        return false;
    }

    run_ptr = (uint8_t *) attr + attr->data.non_resident.mapping_pairs_offset;
    while (*run_ptr != 0) {
        uint8_t header = *run_ptr++;
        uint8_t length_bytes = header & 0x0F;
        uint8_t cluster_bytes = (header >> 4) & 0x0F;
        uint64_t run_length;

        if (length_bytes == 0 || length_bytes > 8) {
            break;
        }
        run_length = (uint64_t) ntfs_decode_run_length(&run_ptr, length_bytes);
        if (cluster_bytes > 8) {
            break;
        }
        if (cluster_bytes > 0) {
            current_cluster += ntfs_decode_run_length(&run_ptr, cluster_bytes);
        }

        if (vcn < current_vcn + run_length) {
            if (current_cluster < 0) {
                return false;
            }
            *lcn_out = (uint64_t) current_cluster + (vcn - current_vcn);
            return true;
        }
        current_vcn += run_length;
    }
    return false;
}

/*
 * 把 buffer 中 size 字节写入非驻留属性，从文件字节偏移 offset 开始。
 * 要求 offset+size 不超过属性已分配的数据大小（不做扩容）。
 * 仅覆盖已有 runlist 对应的簇，不分配新簇。
 */
static bool ntfs_write_non_resident_data(ntfs_attr_header_t *attr, uint64_t offset,
                                         const uint8_t *buffer, uint32_t size)
{
    uint64_t data_size;
    uint64_t cluster_size;
    uint64_t start_cluster;
    uint64_t start_offset_in_cluster;
    uint64_t bytes_left;
    const uint8_t *src = buffer;

    if (attr == NULL || !attr->non_resident) {
        return false;
    }

    data_size = attr->data.non_resident.data_size;
    cluster_size = g_ntfs_info.cluster_size;
    if (cluster_size == 0) {
        return false;
    }

    if (offset + size > data_size) {
        return false;
    }

    bytes_left = size;
    start_cluster = offset / cluster_size;
    start_offset_in_cluster = offset % cluster_size;

    while (bytes_left > 0) {
        uint64_t lcn;
        uint64_t chunk;

        if (!ntfs_runlist_lookup_lcn(attr, start_cluster, &lcn)) {
            return false;
        }

        chunk = cluster_size - start_offset_in_cluster;
        if (chunk > bytes_left) {
            chunk = bytes_left;
        }

        /* 按扇区写入，处理跨扇区/簇内偏移。 */
        {
            uint64_t cluster_lba = ntfs_cluster_to_lba(lcn);
            uint32_t sec = (uint32_t) (start_offset_in_cluster / 512u);
            uint32_t first_byte = (uint32_t) (start_offset_in_cluster % 512u);
            uint64_t remaining = chunk;

            while (remaining > 0) {
                uint32_t this_chunk;

                if (first_byte == 0 && remaining >= 512u) {
                    uint32_t whole = (uint32_t) (remaining / 512u);
                    uint32_t avail = g_ntfs_info.sectors_per_cluster - sec;
                    if (whole > avail) whole = avail;
                    if (whole > 255u) whole = 255u;
                    if (whole >= 1u) {
                        ata_write_sectors((uint32_t) (cluster_lba + sec), (uint8_t) whole, src);
                        uint32_t done = whole * 512u;
                        src += done;
                        remaining -= done;
                        sec += whole;
                        if (remaining == 0) break;
                    }
                }

                this_chunk = 512u - first_byte;
                if (this_chunk > remaining) this_chunk = (uint32_t) remaining;
                {
                    uint8_t sector_buffer[512];
                    uint32_t lba = (uint32_t) (cluster_lba + sec);
                    ata_read_sector(lba, sector_buffer);
                    memcpy(sector_buffer + first_byte, src, this_chunk);
                    ata_write_sector(lba, sector_buffer);
                }
                src += this_chunk;
                remaining -= this_chunk;
                first_byte = 0;
                sec++;
            }
        }

        bytes_left -= chunk;
        start_cluster++;
        start_offset_in_cluster = 0;
    }

    return true;
}

/* 在 $Bitmap（MFT 记录 6）中设置/清除某个簇的已分配位。
 * used=true 标记为已分配，false 标记为空闲。
 */
static bool ntfs_bitmap_set_cluster(uint64_t cluster, bool used)
{
    uint8_t mft_record[4096];
    ntfs_attr_header_t *attr;
    uint64_t byte_offset;
    uint64_t cluster_in_bitmap;
    uint32_t byte_in_cluster;
    uint64_t lcn;
    uint8_t cluster_buf[4096];
    uint32_t cluster_size;

    if (!ntfs_read_mft_record(6, mft_record)) {
        return false;
    }
    attr = ntfs_find_attribute(mft_record, NTFS_ATTR_BITMAP, NULL, 0);
    if (attr == NULL) {
        return false;
    }

    cluster_size = g_ntfs_info.cluster_size;
    if (cluster_size == 0 || cluster_size > sizeof(cluster_buf)) {
        return false;
    }

    byte_offset = cluster / 8u;

    if (!attr->non_resident) {
        /* 驻留位图（极小卷）：直接改内存副本并写回整个 MFT 记录。 */
        uint8_t *data = NULL;
        uint32_t data_size = 0;
        if (!ntfs_get_attribute_data(mft_record, attr, 0, &data, &data_size)) {
            return false;
        }
        if (byte_offset >= data_size) {
            return false;
        }
        if (used) {
            data[byte_offset] |= (uint8_t) (1u << (cluster % 8u));
        } else {
            data[byte_offset] &= (uint8_t) ~(1u << (cluster % 8u));
        }
        return ntfs_write_mft_record(6, mft_record);
    }

    cluster_in_bitmap = byte_offset / cluster_size;
    byte_in_cluster = (uint32_t) (byte_offset % cluster_size);

    if (!ntfs_runlist_lookup_lcn(attr, cluster_in_bitmap, &lcn)) {
        return false;
    }

    /* 读取位图簇，改位，写回。 */
    {
        uint64_t lba = ntfs_cluster_to_lba(lcn);
        uint32_t sec;
        for (sec = 0; sec < g_ntfs_info.sectors_per_cluster; sec++) {
            ata_read_sector((uint32_t) (lba + sec), cluster_buf + sec * 512u);
        }
    }

    if (used) {
        cluster_buf[byte_in_cluster] |= (uint8_t) (1u << (cluster % 8u));
    } else {
        cluster_buf[byte_in_cluster] &= (uint8_t) ~(1u << (cluster % 8u));
    }

    {
        uint64_t lba = ntfs_cluster_to_lba(lcn);
        uint32_t sec;
        for (sec = 0; sec < g_ntfs_info.sectors_per_cluster; sec++) {
            ata_write_sector((uint32_t) (lba + sec), cluster_buf + sec * 512u);
        }
    }

    if (used && g_ntfs_info.free_clusters > 0) g_ntfs_info.free_clusters--;
    if (!used) g_ntfs_info.free_clusters++;
    return true;
}

/* 释放一个非驻留属性占用的所有簇（在 $Bitmap 中标记为空闲）。 */
static void ntfs_release_non_resident_clusters(ntfs_attr_header_t *attr)
{
    const uint8_t *run_ptr;
    uint64_t current_vcn = 0;
    int64_t current_cluster = 0;

    if (attr == NULL || !attr->non_resident) {
        return;
    }

    run_ptr = (uint8_t *) attr + attr->data.non_resident.mapping_pairs_offset;
    while (*run_ptr != 0) {
        uint8_t header = *run_ptr++;
        uint8_t length_bytes = header & 0x0F;
        uint8_t cluster_bytes = (header >> 4) & 0x0F;
        uint64_t run_length;
        uint64_t i;

        if (length_bytes == 0 || length_bytes > 8) break;
        run_length = (uint64_t) ntfs_decode_run_length(&run_ptr, length_bytes);
        if (cluster_bytes > 8) break;
        if (cluster_bytes > 0) {
            current_cluster += ntfs_decode_run_length(&run_ptr, cluster_bytes);
        }
        if (current_cluster < 0) break;

        for (i = 0; i < run_length; i++) {
            ntfs_bitmap_set_cluster((uint64_t) current_cluster + i, false);
        }
        current_vcn += run_length;
    }
}

/* UTF-8 路径名转 UTF-16LE（NTFS 文件名），返回 UTF-16 字符数。 */
static uint32_t ntfs_utf8_to_utf16(const char *utf8, uint16_t *utf16, uint32_t max_chars)
{
    uint32_t i = 0;
    uint32_t j = 0;

    while (utf8[i] != '\0' && j + 1 < max_chars) {
        uint8_t c = (uint8_t) utf8[i++];
        if (c < 0x80) {
            utf16[j++] = c;
        } else if ((c & 0xE0) == 0xC0 && utf8[i] != '\0') {
            uint16_t b1 = (uint16_t) utf8[i++] & 0x3F;
            uint16_t ch = ((uint16_t) (c & 0x1F) << 6) | b1;
            utf16[j++] = ch;
        } else if ((c & 0xF0) == 0xE0 && utf8[i] != '\0' && utf8[i + 1] != '\0') {
            uint16_t b1 = (uint16_t) utf8[i++] & 0x3F;
            uint16_t b2 = (uint16_t) utf8[i++] & 0x3F;
            uint16_t ch = ((uint16_t) (c & 0x0F) << 12) | (b1 << 6) | b2;
            utf16[j++] = ch;
        } else {
            utf16[j++] = c;
        }
    }
    return j;
}

/* 在父目录的 $INDEX_ROOT 中追加一个目录项。
 * parent_mft: 父目录 MFT 记录号；child_ref: 子项 MFT 参考（低48位为记录号）；
 * name: UTF-8 文件名；is_dir: 是否目录。
 * 返回 true 表示成功写入父目录 MFT 记录。
 */
static bool ntfs_index_add_entry(uint64_t parent_mft, uint64_t child_ref,
                                 const char *name, bool is_dir)
{
    uint8_t mft_record[4096];
    ntfs_attr_header_t *attr;
    uint8_t *index_data;
    uint32_t index_size;
    ntfs_index_header_t *ih;
    uint32_t entries_offset;
    uint32_t used;
    uint16_t utf16_name[NTFS_MAX_NAME_LEN];
    uint32_t name_chars;
    uint32_t name_bytes;
    uint32_t fn_size;
    uint32_t entry_size;
    uint8_t *entry_ptr;
    ntfs_index_entry_t *entry;
    ntfs_file_name_attr_t *fn;

    if (!ntfs_read_mft_record(parent_mft, mft_record)) {
        return false;
    }
    attr = ntfs_find_attribute(mft_record, NTFS_ATTR_INDEX_ROOT, NULL, 0);
    if (attr == NULL || attr->non_resident) {
        return false;  /* 索引已扩展到 INDEX_ALLOCATION，简化实现不支持 */
    }
    if (!ntfs_get_attribute_data(mft_record, attr, 0, &index_data, &index_size)) {
        return false;
    }
    if (index_size < 16) {
        return false;
    }

    /* index_data 布局：4字节类型 + 4字节collation + 4字节entry_size + 1字节clusters + 3字节pad
     * 然后是 ntfs_index_header_t（16字节），最后是索引项。
     * 与读取代码一致：index_data+16 指向 ntfs_index_header_t。 */
    ih = (ntfs_index_header_t *) (index_data + 16);
    entries_offset = ih->entries_offset;
    used = ih->total_entries_size;

    name_chars = ntfs_utf8_to_utf16(name, utf16_name, NTFS_MAX_NAME_LEN);
    name_bytes = name_chars * 2u;
    fn_size = 0x42u + name_bytes;  /* ntfs_file_name_attr_t 固定部分 = 0x42 */
    entry_size = 0x10u + fn_size;  /* index entry 头 16 字节 + FILE_NAME 内容 */
    entry_size = (entry_size + 7u) & ~7u;

    if (used + entry_size > index_size) {
        return false;  /* 索引满，不支持拆分到 INDEX_ALLOCATION */
    }

    entry_ptr = index_data + used;
    entry = (ntfs_index_entry_t *) entry_ptr;
    entry->file_reference = child_ref;
    entry->index_entry_length = entry_size;
    entry->key_length = fn_size;
    entry->flags = NTFS_INDEX_ENTRY_FLAG_LAST;  /* 新的最后一项 */

    /* 把原来的最后一项的 LAST 标志清掉 */
    if (used > entries_offset) {
        /* 沿 entries_offset 走到 used 之前的一项 */
        uint32_t off = entries_offset;
        while (off + 8 <= used) {
            ntfs_index_entry_t *e = (ntfs_index_entry_t *) (index_data + off);
            uint32_t elen = (uint32_t) e->index_entry_length;
            if (elen == 0 || off + elen > used) break;
            if (off + elen == used) {
                e->flags &= (uint16_t) ~NTFS_INDEX_ENTRY_FLAG_LAST;
                break;
            }
            off += elen;
        }
    }

    fn = (ntfs_file_name_attr_t *) entry->key;
    memset(fn, 0, fn_size);
    fn->file_reference = child_ref;
    fn->parent_directory = parent_mft;
    fn->name_length = (uint8_t) name_chars;
    fn->file_attributes = is_dir ? 0x10 : 0x20;
    memcpy(fn->name, utf16_name, name_bytes);

    ih->total_entries_size = used + entry_size;
    if (ih->allocated_entries_size < ih->total_entries_size) {
        ih->allocated_entries_size = ih->total_entries_size;
    }

    return ntfs_write_mft_record(parent_mft, mft_record);
}

/* 从父目录的 $INDEX_ROOT 中移除指定文件名的目录项。 */
static bool ntfs_index_remove_entry(uint64_t parent_mft, const char *name)
{
    uint8_t mft_record[4096];
    ntfs_attr_header_t *attr;
    uint8_t *index_data;
    uint32_t index_size;
    ntfs_index_header_t *ih;
    uint32_t entries_offset;
    uint32_t entries_end;
    uint32_t offset;
    bool found = false;

    if (!ntfs_read_mft_record(parent_mft, mft_record)) {
        return false;
    }
    attr = ntfs_find_attribute(mft_record, NTFS_ATTR_INDEX_ROOT, NULL, 0);
    if (attr == NULL || attr->non_resident) {
        return false;
    }
    if (!ntfs_get_attribute_data(mft_record, attr, 0, &index_data, &index_size)) {
        return false;
    }
    if (index_size < 16) {
        return false;
    }
    ih = (ntfs_index_header_t *) (index_data + 16);
    entries_offset = ih->entries_offset;
    entries_end = ih->total_entries_size;

    offset = entries_offset;
    while (offset + sizeof(ntfs_index_entry_t) <= entries_end) {
        ntfs_index_entry_t *entry = (ntfs_index_entry_t *) (index_data + offset);
        uint32_t entry_len = (uint32_t) entry->index_entry_length;
        ntfs_file_name_attr_t *fn;
        uint32_t key_offset;
        uint32_t name_offset;

        if (entry_len == 0 || offset + entry_len > entries_end) break;

        if ((entry->flags & NTFS_INDEX_ENTRY_FLAG_END) == 0) {
            key_offset = (uint32_t) ((uint8_t *) entry->key - (uint8_t *) entry);
            if (key_offset < entry_len) {
                fn = (ntfs_file_name_attr_t *) entry->key;
                name_offset = (uint32_t) ((uint8_t *) fn->name - (uint8_t *) fn);
                if (name_offset + fn->name_length * 2 <= entry->key_length) {
                    if (ntfs_compare_name(name, (const uint16_t *) fn->name, fn->name_length)) {
                        found = true;
                        /* 把该项标记为 END（未使用），其后项前移。
                         * 简化处理：把后面的项整体向前搬移 entry_len。 */
                        uint32_t tail = entries_end - (offset + entry_len);
                        if (tail > 0) {
                            memmove(index_data + offset,
                                    index_data + offset + entry_len,
                                    tail);
                        }
                        ih->total_entries_size -= entry_len;
                        entries_end = ih->total_entries_size;
                        /* 重新扫描 */
                        offset = entries_offset;
                        continue;
                    }
                }
            }
        }

        if (entry->flags & NTFS_INDEX_ENTRY_FLAG_LAST) break;
        offset += entry_len;
    }

    if (!found) {
        return false;
    }

    return ntfs_write_mft_record(parent_mft, mft_record);
}

/* 扫描 MFT，找到一个未使用的记录号（>= 16，避开系统记录 0-15）。 */
static bool ntfs_find_free_mft_record(uint64_t *record_out)
{
    uint8_t mft_record[4096];
    uint64_t max_records = g_ntfs_info.total_clusters; /* 上限保护 */
    uint64_t i;

    if (max_records > 1024 * 1024) {
        max_records = 1024 * 1024;
    }

    for (i = 16; i < max_records; i++) {
        if (ntfs_read_mft_record(i, mft_record)) {
            ntfs_mft_record_header_t *h = (ntfs_mft_record_header_t *) mft_record;
            if ((h->flags & NTFS_MFT_RECORD_FLAG_IN_USE) == 0) {
                *record_out = i;
                return true;
            }
        } else {
            /* 读不到记录说明记录槽为空/损坏，可重用 */
            *record_out = i;
            return true;
        }
    }
    return false;
}

/* 分块读取 $Bitmap 时使用的块大小（复用已有的 64KB 索引缓冲区） */
#define NTFS_BITMAP_CHUNK_SIZE  sizeof(g_index_buffer)

/*
 * 手动 8 位 popcount（Harvard 法）。
 * freestanding 环境没有 libgcc，不能用 __builtin_popcount（会外呼 __popcountdi2）。
 */
static uint8_t popcount8(uint8_t b)
{
    b = (uint8_t) (b - ((b >> 1) & 0x55));
    b = (uint8_t) ((b & 0x33) + ((b >> 2) & 0x33));
    return (uint8_t) (((b + (b >> 4)) & 0x0F));
}

/*
 * 统计一段位图字节中 0 bit（空闲簇）的数量。
 * 假定每个字节的 8 个 bit 全部有效；最后一个字节的 padding 由调用方修正。
 */
static uint64_t ntfs_count_free_clusters(const uint8_t *bitmap, uint32_t bytes)
{
    uint64_t free_bits = 0;

    for (uint32_t i = 0; i < bytes; i++) {
        /* 1=已分配，0=空闲；该字节中已分配位数为 popcount，空闲位为 8-popcount */
        free_bits += (uint64_t) (8 - popcount8(bitmap[i]));
    }
    return free_bits;
}

/*
 * 读取 $Bitmap（MFT 记录 6，属性类型 0xB0）并统计空闲簇数。
 *
 * $Bitmap 通常是非驻留属性，且可能很大（数十 MB），无法整体读入内存，
 * 因此这里复用 g_index_buffer，通过 ntfs_read_non_resident_data 按字节偏移
 * 分块（每块最多 64KB）读取，边读边统计，读完一块再读下一块。
 *
 * 驻留属性则直接通过 ntfs_get_attribute_data 取指针一次性统计。
 * 任何失败都返回 false，调用方应保证 free_clusters 为 0，且不影响卷挂载。
 */
static bool ntfs_read_bitmap(void)
{
    uint8_t mft_record[4096];
    ntfs_attr_header_t *attr;
    uint64_t total_clusters;
    uint64_t needed_bytes;
    uint64_t free_clusters = 0;
    uint8_t tail_byte = 0;

    if (!ntfs_read_mft_record(6, mft_record)) {
        return false;
    }

    attr = ntfs_find_attribute(mft_record, NTFS_ATTR_BITMAP, NULL, 0);
    if (attr == NULL) {
        return false;
    }

    total_clusters = g_ntfs_info.total_clusters;
    if (total_clusters == 0) {
        g_ntfs_info.free_clusters = 0;
        return true;
    }
    /* 每个 bit 对应一个簇，所需位图字节数 = 总簇数向上取整到 8 */
    needed_bytes = (total_clusters + 7) / 8;

    if (!attr->non_resident) {
        /* 驻留属性：直接拿到位图数据指针 */
        uint8_t *data = NULL;
        uint32_t data_size = 0;
        uint32_t count_bytes;

        if (!ntfs_get_attribute_data(mft_record, attr, 0, &data, &data_size)) {
            return false;
        }
        count_bytes = data_size;
        if ((uint64_t) count_bytes > needed_bytes) {
            count_bytes = (uint32_t) needed_bytes;
        }
        if (count_bytes == 0) {
            return false;
        }
        free_clusters = ntfs_count_free_clusters(data, count_bytes);
        tail_byte = data[count_bytes - 1];
    } else {
        /* 非驻留属性：分块读取整个位图 */
        uint64_t offset = 0;

        while (offset < needed_bytes) {
            uint32_t chunk = NTFS_BITMAP_CHUNK_SIZE;
            uint64_t remaining = needed_bytes - offset;

            if ((uint64_t) chunk > remaining) {
                chunk = (uint32_t) remaining;
            }
            /* ntfs_read_non_resident_data 内部解码 data runs，按字节 offset 读取 */
            if (!ntfs_read_non_resident_data(mft_record, attr, offset, g_index_buffer, chunk)) {
                return false;
            }
            free_clusters += ntfs_count_free_clusters(g_index_buffer, chunk);
            tail_byte = g_index_buffer[chunk - 1];
            offset += chunk;
        }
    }

    /* 修正最后一个字节的 padding bit：
     * 总簇数不是 8 的倍数时，最后一个字节的高位不属于任何簇，应视为已分配。
     * ntfs_count_free_clusters 把这些位也按 8 个有效位统计了，需扣除其中的 0 位。 */
    {
        uint32_t leftover = (uint32_t) (total_clusters % 8);
        if (leftover != 0) {
            uint8_t pad_mask = (uint8_t) ~((1u << leftover) - 1u);
            uint8_t pad_bits = (uint8_t) (tail_byte & pad_mask);

            free_clusters -= (uint64_t) ((8 - leftover) - popcount8(pad_bits));
        }
    }

    g_ntfs_info.free_clusters = free_clusters;
    return true;
}

static uint64_t ntfs_get_file_size(uint8_t *mft_record)
{
    ntfs_attr_header_t *attr;
    ntfs_file_name_attr_t *file_name;

    attr = ntfs_find_attribute(mft_record, NTFS_ATTR_FILE_NAME, NULL, 0);
    if (attr == NULL) {
        return 0;
    }

    if (!attr->non_resident) {
        uint32_t offset = (uint32_t) ((uint8_t *) attr - mft_record) + attr->data.resident.value_offset;
        file_name = (ntfs_file_name_attr_t *) (mft_record + offset);
        return file_name->data_size;
    }

    attr = ntfs_find_attribute(mft_record, NTFS_ATTR_DATA, NULL, 0);
    if (attr == NULL) {
        return 0;
    }

    if (attr->non_resident) {
        return attr->data.non_resident.data_size;
    }

    return attr->data.resident.value_length;
}

static bool ntfs_is_directory(uint8_t *mft_record)
{
    ntfs_mft_record_header_t *header = (ntfs_mft_record_header_t *) mft_record;
    return (header->flags & NTFS_MFT_RECORD_FLAG_DIRECTORY) != 0;
}

static void ntfs_utf16_to_utf8(const uint16_t *utf16, uint32_t len, char *utf8, uint32_t utf8_size)
{
    uint32_t i = 0;
    uint32_t j = 0;

    while (i < len && j + 1 < utf8_size) {
        uint16_t ch = utf16[i++];

        if (ch < 0x80) {
            utf8[j++] = (char) ch;
        } else if (ch < 0x800) {
            if (j + 2 >= utf8_size) break;
            utf8[j++] = (char) (0xC0 | (ch >> 6));
            utf8[j++] = (char) (0x80 | (ch & 0x3F));
        } else {
            if (j + 3 >= utf8_size) break;
            utf8[j++] = (char) (0xE0 | (ch >> 12));
            utf8[j++] = (char) (0x80 | ((ch >> 6) & 0x3F));
            utf8[j++] = (char) (0x80 | (ch & 0x3F));
        }
    }

    utf8[j] = '\0';
}

static bool ntfs_compare_name(const char *name, const uint16_t *utf16_name, uint32_t name_len)
{
    char utf8_name[NTFS_MAX_NAME_LEN + 1];
    uint32_t i;

    ntfs_utf16_to_utf8(utf16_name, name_len, utf8_name, sizeof(utf8_name));

    for (i = 0; name[i] != '\0' && utf8_name[i] != '\0'; i++) {
        char a = name[i];
        char b = utf8_name[i];

        if (a >= 'A' && a <= 'Z') a = (char) (a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char) (b - 'A' + 'a');

        if (a != b) {
            return false;
        }
    }

    return name[i] == '\0' && utf8_name[i] == '\0';
}

static bool ntfs_index_bounds(const uint8_t *index_data, uint32_t index_size,
                              uint32_t *entries_offset_out, uint32_t *entries_end_out)
{
    ntfs_index_header_t *index_header;
    uint32_t entries_offset;
    uint32_t entries_end;

    if (index_data == NULL || index_size < sizeof(ntfs_index_header_t) ||
        entries_offset_out == NULL || entries_end_out == NULL) {
        return false;
    }

    index_header = (ntfs_index_header_t *) index_data;
    entries_offset = index_header->entries_offset;
    entries_end = index_header->total_entries_size;

    if (entries_offset < sizeof(ntfs_index_header_t) ||
        entries_offset > entries_end ||
        entries_end > index_size) {
        return false;
    }

    *entries_offset_out = entries_offset;
    *entries_end_out = entries_end;
    return true;
}

static bool ntfs_index_entry_len(const ntfs_index_entry_t *entry,
                                 uint32_t available, uint32_t *entry_len_out)
{
    uint64_t len;

    if (entry == NULL || entry_len_out == NULL) {
        return false;
    }
    len = entry->index_entry_length;
    if (len == 0 || len > available || len > 0xFFFFFFFFu ||
        (uint32_t) len < sizeof(ntfs_index_entry_t)) {
        return false;
    }
    *entry_len_out = (uint32_t) len;
    return true;
}

static bool ntfs_index_entry_file_name(ntfs_index_entry_t *entry,
                                       uint32_t entry_len,
                                       ntfs_file_name_attr_t **file_name_out)
{
    uint32_t key_offset;
    uint32_t name_offset;
    ntfs_file_name_attr_t *file_name;

    if (entry == NULL || file_name_out == NULL) {
        return false;
    }
    key_offset = (uint32_t) ((uint8_t *) entry->key - (uint8_t *) entry);
    if (key_offset >= entry_len || entry->key_length > entry_len - key_offset) {
        return false;
    }
    file_name = (ntfs_file_name_attr_t *) entry->key;
    name_offset = (uint32_t) ((uint8_t *) file_name->name - (uint8_t *) file_name);
    if (name_offset > entry->key_length ||
        (uint32_t) file_name->name_length > (entry->key_length - name_offset) / 2u) {
        return false;
    }
    *file_name_out = file_name;
    return true;
}

static bool ntfs_find_in_index(const uint8_t *index_data, uint32_t index_size, const char *name, uint64_t *mft_ref_out)
{
    uint32_t entries_offset;
    uint32_t entries_end;
    uint32_t offset;

    if (!ntfs_index_bounds(index_data, index_size, &entries_offset, &entries_end)) {
        return false;
    }

    offset = entries_offset;
    while (offset + sizeof(ntfs_index_entry_t) <= entries_end) {
        ntfs_index_entry_t *entry = (ntfs_index_entry_t *) (index_data + offset);
        uint32_t entry_len;

        if (!ntfs_index_entry_len(entry, entries_end - offset, &entry_len)) {
            break;
        }

        if ((entry->flags & NTFS_INDEX_ENTRY_FLAG_END) == 0) {
            ntfs_file_name_attr_t *file_name;

            if (!ntfs_index_entry_file_name(entry, entry_len, &file_name)) {
                return false;
            }
            if (ntfs_compare_name(name, (const uint16_t *) file_name->name, file_name->name_length)) {
                *mft_ref_out = entry->file_reference & 0xFFFFFFFFFFFF;
                return true;
            }
        }

        if (entry->flags & NTFS_INDEX_ENTRY_FLAG_LAST) {
            break;
        }

        offset += entry_len;
    }

    return false;
}

static bool ntfs_find_file_in_dir(uint64_t dir_mft_ref, const char *name, uint64_t *mft_ref_out)
{
    uint8_t mft_record[4096];
    ntfs_attr_header_t *attr;
    uint8_t *index_data;
    uint32_t index_size;

    if (!ntfs_read_mft_record(dir_mft_ref, mft_record)) {
        return false;
    }

    attr = ntfs_find_attribute(mft_record, NTFS_ATTR_INDEX_ROOT, NULL, 0);
    if (attr == NULL) {
        return false;
    }

    if (!ntfs_get_attribute_data(mft_record, attr, 0, &index_data, &index_size)) {
        return false;
    }
    if (index_size < 16) {
        return false;
    }

    if (ntfs_find_in_index(index_data + 16, index_size - 16, name, mft_ref_out)) {
        return true;
    }

    attr = ntfs_find_attribute(mft_record, NTFS_ATTR_INDEX_ALLOCATION, NULL, 0);
    if (attr != NULL && attr->non_resident) {
        uint64_t data_size = attr->data.non_resident.data_size;
        uint32_t index_record_size = g_ntfs_info.index_record_size;

        if (index_record_size == 0) {
            index_record_size = g_ntfs_info.cluster_size;
        }

        for (uint64_t vcn = 0; vcn * index_record_size < data_size; vcn++) {
            if (index_record_size > sizeof(g_index_buffer)) {
                break;
            }

            if (!ntfs_read_non_resident_data(mft_record, attr, vcn * index_record_size, g_index_buffer, index_record_size)) {
                break;
            }

            if (memcmp(g_index_buffer, "INDX", 4) == 0) {
                uint32_t offset = 24;

                if (index_record_size > offset &&
                    ntfs_find_in_index(g_index_buffer + offset, index_record_size - offset, name, mft_ref_out)) {
                    return true;
                }
            }
        }
    }

    return false;
}

static bool ntfs_resolve_path(const char *path, uint64_t *mft_ref_out)
{
    uint64_t current_mft = 5;
    const char *cursor = path;
    char component[NTFS_MAX_NAME_LEN + 1];
    uint32_t comp_len;

    if (path == NULL || path[0] != '/') {
        return false;
    }

    cursor++;

    while (*cursor != '\0') {
        comp_len = 0;
        while (*cursor != '/' && *cursor != '\0' && comp_len < NTFS_MAX_NAME_LEN) {
            component[comp_len++] = *cursor++;
        }
        component[comp_len] = '\0';

        if (comp_len == 0) {
            if (*cursor == '/') {
                cursor++;
            }
            continue;
        }

        if (strcmp(component, ".") == 0) {
            if (*cursor == '/') {
                cursor++;
            }
            continue;
        }

        if (strcmp(component, "..") == 0) {
            uint64_t parent_mft;
            if (!ntfs_find_file_in_dir(current_mft, "..", &parent_mft)) {
                return false;
            }
            current_mft = parent_mft;
            if (*cursor == '/') {
                cursor++;
            }
            continue;
        }

        if (!ntfs_find_file_in_dir(current_mft, component, &current_mft)) {
            return false;
        }

        if (*cursor == '/') {
            cursor++;
        }
    }

    *mft_ref_out = current_mft;
    return true;
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
    /* 总簇数 = 总扇区数 / 每簇扇区数 */
    g_ntfs_info.total_clusters = g_ntfs_info.total_sectors / sectors_per_cluster;
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
    int32_t mount_hint = file_mount_partition_hint();
    g_ntfs_blockdev = file_blockdev_hint();
    g_ntfs_write_warned = false;

    memset(&g_ntfs_info, 0, sizeof(g_ntfs_info));
    strcpy(g_ntfs_info.status, "ntfs: not found");

    if (mount_hint >= 0) {
        ata_read_sector((uint32_t) mount_hint, sector);
        if (ntfs_parse_boot((uint32_t) mount_hint, sector)) {
            ntfs_read_bitmap();  /* 失败时内部已置 free_clusters=0，不影响挂载 */
            return true;
        }
        strcpy(g_ntfs_info.status, "ntfs: no volume detected");
        return false;
    }

    ata_read_sector(0, sector);
    if (ntfs_parse_boot(0, sector)) {
        ntfs_read_bitmap();  /* 失败时内部已置 free_clusters=0，不影响挂载 */
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
                ntfs_read_bitmap();  /* 失败时内部已置 free_clusters=0，不影响挂载 */
                return true;
            }
        }
    }
    strcpy(g_ntfs_info.status, "ntfs: no volume detected");
    return false;
}

uint16_t ntfs_root_entry_count(void)
{
    char buffer[4096];
    uint16_t count = 0;

    if (!ntfs_list_root(buffer, sizeof(buffer))) {
        return 0;
    }

    for (uint32_t i = 0; buffer[i] != '\0'; i++) {
        if (buffer[i] == '\n') {
            count++;
        }
    }
    return count;
}

bool ntfs_exists(const char *path)
{
    uint64_t mft_ref;

    if (!g_ntfs_info.present || path == NULL) {
        return false;
    }

    if (path[0] == '/' && path[1] == '\0') {
        return true;
    }

    return ntfs_resolve_path(path, &mft_ref);
}

bool ntfs_is_dir(const char *path)
{
    uint64_t mft_ref;
    uint8_t mft_record[4096];

    if (!g_ntfs_info.present || path == NULL) {
        return false;
    }

    if (path[0] == '/' && path[1] == '\0') {
        return true;
    }

    if (!ntfs_resolve_path(path, &mft_ref)) {
        return false;
    }

    if (!ntfs_read_mft_record(mft_ref, mft_record)) {
        return false;
    }

    return ntfs_is_directory(mft_record);
}

int32_t ntfs_file_size(const char *path)
{
    uint64_t mft_ref;
    uint8_t mft_record[4096];
    uint64_t size;

    if (!g_ntfs_info.present || path == NULL) {
        return -1;
    }

    if (!ntfs_resolve_path(path, &mft_ref)) {
        return -1;
    }

    if (!ntfs_read_mft_record(mft_ref, mft_record)) {
        return -1;
    }

    if (ntfs_is_directory(mft_record)) {
        return -1;
    }

    size = ntfs_get_file_size(mft_record);
    if (size > 0x7FFFFFFF) {
        return -1;
    }

    return (int32_t) size;
}

int32_t ntfs_read_file(const char *path, void *buffer, uint32_t buffer_size)
{
    return ntfs_read_file_at(path, 0, buffer, buffer_size);
}

int32_t ntfs_read_file_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size)
{
    uint64_t mft_ref;
    uint8_t mft_record[4096];
    ntfs_attr_header_t *attr;
    uint64_t file_size;
    uint64_t bytes_to_read;

    if (!g_ntfs_info.present || path == NULL || buffer == NULL) {
        return -1;
    }

    if (!ntfs_resolve_path(path, &mft_ref)) {
        return -1;
    }

    if (!ntfs_read_mft_record(mft_ref, mft_record)) {
        return -1;
    }

    if (ntfs_is_directory(mft_record)) {
        return -3;
    }

    file_size = ntfs_get_file_size(mft_record);
    if (offset >= file_size || buffer_size == 0) {
        return 0;
    }

    bytes_to_read = file_size - offset;
    if (bytes_to_read > buffer_size) {
        bytes_to_read = buffer_size;
    }

    attr = ntfs_find_attribute(mft_record, NTFS_ATTR_DATA, NULL, 0);
    if (attr == NULL) {
        return -1;
    }

    if (!attr->non_resident) {
        uint8_t *data;
        uint32_t data_size;

        if (!ntfs_get_attribute_data(mft_record, attr, 0, &data, &data_size)) {
            return -1;
        }

        if (offset >= data_size) {
            return 0;
        }

        bytes_to_read = data_size - offset;
        if (bytes_to_read > buffer_size) {
            bytes_to_read = buffer_size;
        }

        memcpy(buffer, data + offset, (uint32_t) bytes_to_read);
        return (int32_t) bytes_to_read;
    }

    if (!ntfs_read_non_resident_data(mft_record, attr, offset, (uint8_t *) buffer, (uint32_t) bytes_to_read)) {
        return -1;
    }

    return (int32_t) bytes_to_read;
}

/* 更新 $FILE_NAME 属性中的 data_size/allocated_size（用于文件大小变更后）。 */
static void ntfs_update_fname_sizes(uint8_t *mft_record, uint64_t data_size, uint64_t alloc_size)
{
    ntfs_attr_header_t *attr;
    ntfs_file_name_attr_t *fn;
    uint32_t offset;

    attr = ntfs_find_attribute(mft_record, NTFS_ATTR_FILE_NAME, NULL, 0);
    if (attr == NULL || attr->non_resident) {
        return;
    }
    offset = (uint32_t) ((uint8_t *) attr - mft_record) + attr->data.resident.value_offset;
    fn = (ntfs_file_name_attr_t *) (mft_record + offset);
    fn->data_size = data_size;
    fn->allocated_size = alloc_size;
}

int32_t ntfs_write_file(const char *path, const void *buffer, uint32_t size)
{
    uint64_t mft_ref;
    uint8_t mft_record[4096];
    ntfs_attr_header_t *attr;
    bool exists;

    if (!g_ntfs_info.present || path == NULL || buffer == NULL) {
        return -1;
    }

    exists = ntfs_resolve_path(path, &mft_ref);

    if (exists) {
        /* 覆盖已有文件：不扩容，仅在已分配空间内写入。 */
        if (!ntfs_read_mft_record(mft_ref, mft_record)) {
            return -1;
        }
        if (ntfs_is_directory(mft_record)) {
            return -1;
        }

        attr = ntfs_find_attribute(mft_record, NTFS_ATTR_DATA, NULL, 0);
        if (attr == NULL) {
            return -1;
        }

        if (!attr->non_resident) {
            uint8_t *data;
            uint32_t data_size;
            uint32_t cap = attr->data.resident.value_length;

            if (!ntfs_get_attribute_data(mft_record, attr, 0, &data, &data_size)) {
                return -1;
            }
            /* 驻留数据：只能在已分配的 value_length 内覆盖。 */
            if (size > cap) {
                return -1;
            }
            if (size > 0) {
                memcpy(data, buffer, size);
            }
            attr->data.resident.value_length = size;
            ntfs_update_fname_sizes(mft_record, size, size);
            if (!ntfs_write_mft_record(mft_ref, mft_record)) {
                return -1;
            }
            return (int32_t) size;
        }

        /* 非驻留数据：在已分配大小内覆盖。 */
        {
            uint64_t alloc = attr->data.non_resident.data_size;
            if (size > alloc) {
                return -1;  /* 不支持扩容 */
            }
            if (size > 0) {
                if (!ntfs_write_non_resident_data(attr, 0, (const uint8_t *) buffer, size)) {
                    return -1;
                }
            }
            attr->data.non_resident.data_size = size;
            attr->data.non_resident.initialized_size = size;
            ntfs_update_fname_sizes(mft_record, size, alloc);
            if (!ntfs_write_mft_record(mft_ref, mft_record)) {
                return -1;
            }
            return (int32_t) size;
        }
    }

    /* 文件不存在：尝试创建一个小型驻留数据文件。
     * 限制：仅支持小文件（数据 + 属性头需放进 MFT 记录），
     * 父目录索引必须仍在 $INDEX_ROOT 内（未扩展到 INDEX_ALLOCATION）。 */
    {
        const char *filename;
        char dir_path[NTFS_MAX_PATH + 1];
        uint32_t dir_len;
        uint64_t parent_mft;
        uint64_t new_ref;
        uint16_t utf16_name[NTFS_MAX_NAME_LEN];
        uint32_t name_chars;
        uint32_t name_bytes;
        uint32_t fn_size;
        uint8_t rec[4096];
        ntfs_mft_record_header_t *h;
        ntfs_attr_header_t *a;
        uint32_t off;
        uint8_t *p;
        ntfs_file_name_attr_t *fn;
        uint32_t data_cap;

        filename = path;
        while (*filename != '\0') filename++;
        while (filename > path && *filename != '/') filename--;
        if (*filename != '/') {
            return -1;
        }
        filename++;
        if (*filename == '\0') {
            return -1;
        }

        dir_len = (uint32_t) (filename - path - 1);
        if (dir_len >= sizeof(dir_path)) {
            return -1;
        }
        memcpy(dir_path, path, dir_len);
        dir_path[dir_len] = '\0';
        if (dir_len == 0) {
            strcpy(dir_path, "/");
        }

        if (!ntfs_resolve_path(dir_path, &parent_mft)) {
            return -1;
        }

        /* 数据大小上限：MFT 记录大小减去各种属性开销。
         * 保守取 cluster_size 与 record_size/2 的较小值。 */
        data_cap = g_ntfs_info.mft_record_size;
        if (data_cap > 2048) data_cap = 2048;
        if (size > data_cap - 256) {
            return -1;  /* 文件太大，驻放不下 */
        }

        if (!ntfs_find_free_mft_record(&new_ref)) {
            return -1;
        }

        name_chars = ntfs_utf8_to_utf16(filename, utf16_name, NTFS_MAX_NAME_LEN);
        name_bytes = name_chars * 2u;
        fn_size = 0x42u + name_bytes;

        memset(rec, 0, sizeof(rec));
        memcpy(rec, "FILE", 4);
        h = (ntfs_mft_record_header_t *) rec;
        h->update_sequence_offset = 0x2A;
        h->update_sequence_size = 0;
        h->sequence_number = 1;
        h->hard_link_count = 1;
        h->first_attribute_offset = 0x38;
        h->flags = NTFS_MFT_RECORD_FLAG_IN_USE;
        h->next_attribute_id = 0;
        h->mft_record_number = (uint32_t) new_ref;

        off = 0x38;

        /* $STANDARD_INFORMATION (0x10), resident */
        a = (ntfs_attr_header_t *) (rec + off);
        a->type = NTFS_ATTR_STANDARD_INFORMATION;
        a->length = 0x70;
        a->non_resident = 0;
        a->name_length = 0;
        a->name_offset = 0;
        a->flags = 0;
        a->attribute_id = 0;
        a->data.resident.value_length = 0x30;
        a->data.resident.value_offset = 0x40;
        a->data.resident.indexed = 0;
        a->data.resident.padding = 0;
        off += 0x70;

        /* $FILE_NAME (0x30), resident */
        a = (ntfs_attr_header_t *) (rec + off);
        a->type = NTFS_ATTR_FILE_NAME;
        a->length = (uint16_t) (((0x40 + fn_size) + 7u) & ~7u);
        a->non_resident = 0;
        a->name_length = 0;
        a->name_offset = 0;
        a->flags = 0;
        a->attribute_id = 0;
        a->data.resident.value_length = fn_size;
        a->data.resident.value_offset = 0x40;
        a->data.resident.indexed = 1;
        a->data.resident.padding = 0;
        p = rec + off + 0x40;
        fn = (ntfs_file_name_attr_t *) p;
        memset(fn, 0, fn_size);
        fn->file_reference = new_ref;
        fn->parent_directory = parent_mft;
        fn->name_length = (uint8_t) name_chars;
        fn->file_attributes = 0x20;  /* archive */
        fn->data_size = size;
        fn->allocated_size = size;
        memcpy(fn->name, utf16_name, name_bytes);
        off += a->length;

        /* $DATA (0x80), resident */
        a = (ntfs_attr_header_t *) (rec + off);
        a->type = NTFS_ATTR_DATA;
        a->length = (uint16_t) (((0x40 + size) + 7u) & ~7u);
        if ((a->length & 7u) != 0) {
            a->length = (uint16_t) ((a->length + 7u) & ~7u);
        }
        a->non_resident = 0;
        a->name_length = 0;
        a->name_offset = 0;
        a->flags = 0;
        a->attribute_id = 0;
        a->data.resident.value_length = size;
        a->data.resident.value_offset = 0x40;
        a->data.resident.indexed = 0;
        a->data.resident.padding = 0;
        if (size > 0) {
            memcpy(rec + off + 0x40, buffer, size);
        }
        off += a->length;

        /* 结束标记 */
        a = (ntfs_attr_header_t *) (rec + off);
        a->type = 0xFFFFFFFF;
        a->length = 0;
        off += 0x10;

        h->bytes_in_use = off;
        h->bytes_allocated = g_ntfs_info.mft_record_size;

        if (h->bytes_in_use > g_ntfs_info.mft_record_size) {
            return -1;
        }

        if (!ntfs_write_mft_record(new_ref, rec)) {
            return -1;
        }

        if (!ntfs_index_add_entry(parent_mft, new_ref, filename, false)) {
            /* 索引添加失败：保留 MFT 记录但不挂载到目录。 */
            return -1;
        }

        return (int32_t) size;
    }
}

bool ntfs_delete(const char *path)
{
    uint64_t mft_ref;
    uint8_t mft_record[4096];
    ntfs_attr_header_t *data_attr;
    const char *filename;
    char dir_path[NTFS_MAX_PATH + 1];
    uint32_t dir_len;
    uint64_t parent_mft;

    if (!g_ntfs_info.present || path == NULL) {
        return false;
    }
    if (path[0] == '/' && path[1] == '\0') {
        return false;  /* 不能删根 */
    }

    if (!ntfs_resolve_path(path, &mft_ref)) {
        return false;
    }
    if (!ntfs_read_mft_record(mft_ref, mft_record)) {
        return false;
    }
    if (ntfs_is_directory(mft_record)) {
        return false;  /* 用 rmdir 删目录 */
    }

    /* 分离父目录和文件名 */
    filename = path;
    while (*filename != '\0') filename++;
    while (filename > path && *filename != '/') filename--;
    if (*filename != '/') return false;
    filename++;
    if (*filename == '\0') return false;

    dir_len = (uint32_t) (filename - path - 1);
    if (dir_len >= sizeof(dir_path)) return false;
    memcpy(dir_path, path, dir_len);
    dir_path[dir_len] = '\0';
    if (dir_len == 0) strcpy(dir_path, "/");

    if (!ntfs_resolve_path(dir_path, &parent_mft)) {
        return false;
    }

    /* 释放 $DATA 占用的簇 */
    data_attr = ntfs_find_attribute(mft_record, NTFS_ATTR_DATA, NULL, 0);
    if (data_attr != NULL && data_attr->non_resident) {
        ntfs_release_non_resident_clusters(data_attr);
    }

    /* 从父目录索引中移除 */
    ntfs_index_remove_entry(parent_mft, filename);

    /* 标记 MFT 记录为未使用 */
    {
        ntfs_mft_record_header_t *h = (ntfs_mft_record_header_t *) mft_record;
        h->flags &= (uint16_t) ~NTFS_MFT_RECORD_FLAG_IN_USE;
        h->hard_link_count = 0;
        if (!ntfs_write_mft_record(mft_ref, mft_record)) {
            return false;
        }
    }

    return true;
}

bool ntfs_mkdir(const char *path)
{
    /* 简化实现：创建一个目录 MFT 记录，仅含 $INDEX_ROOT（空目录，只有隐式 END 项）。
     * 不创建 "." 和 ".." 索引项（Windows 不要求，部分实现可后续解析）。 */
    const char *dirname;
    char dir_path[NTFS_MAX_PATH + 1];
    uint32_t dir_len;
    uint64_t parent_mft;
    uint64_t new_ref;
    uint16_t utf16_name[NTFS_MAX_NAME_LEN];
    uint32_t name_chars;
    uint32_t name_bytes;
    uint32_t fn_size;
    uint8_t rec[4096];
    ntfs_mft_record_header_t *h;
    ntfs_attr_header_t *a;
    uint32_t off;
    ntfs_file_name_attr_t *fn;

    if (!g_ntfs_info.present || path == NULL) {
        return false;
    }
    if (path[0] == '/' && path[1] == '\0') {
        return false;
    }

    if (ntfs_exists(path)) {
        return false;  /* 已存在 */
    }

    dirname = path;
    while (*dirname != '\0') dirname++;
    while (dirname > path && *dirname != '/') dirname--;
    if (*dirname != '/') return false;
    dirname++;
    if (*dirname == '\0') return false;

    dir_len = (uint32_t) (dirname - path - 1);
    if (dir_len >= sizeof(dir_path)) return false;
    memcpy(dir_path, path, dir_len);
    dir_path[dir_len] = '\0';
    if (dir_len == 0) strcpy(dir_path, "/");

    if (!ntfs_resolve_path(dir_path, &parent_mft)) {
        return false;
    }

    if (!ntfs_find_free_mft_record(&new_ref)) {
        return false;
    }

    name_chars = ntfs_utf8_to_utf16(dirname, utf16_name, NTFS_MAX_NAME_LEN);
    name_bytes = name_chars * 2u;
    fn_size = 0x42u + name_bytes;

    memset(rec, 0, sizeof(rec));
    memcpy(rec, "FILE", 4);
    h = (ntfs_mft_record_header_t *) rec;
    h->update_sequence_offset = 0x2A;
    h->update_sequence_size = 0;
    h->sequence_number = 1;
    h->hard_link_count = 1;
    h->first_attribute_offset = 0x38;
    h->flags = NTFS_MFT_RECORD_FLAG_IN_USE | NTFS_MFT_RECORD_FLAG_DIRECTORY;
    h->next_attribute_id = 0;
    h->mft_record_number = (uint32_t) new_ref;

    off = 0x38;

    /* $STANDARD_INFORMATION */
    a = (ntfs_attr_header_t *) (rec + off);
    a->type = NTFS_ATTR_STANDARD_INFORMATION;
    a->length = 0x70;
    a->non_resident = 0;
    a->data.resident.value_length = 0x30;
    a->data.resident.value_offset = 0x40;
    off += 0x70;

    /* $FILE_NAME */
    a = (ntfs_attr_header_t *) (rec + off);
    a->type = NTFS_ATTR_FILE_NAME;
    a->length = (uint16_t) (((0x40 + fn_size) + 7u) & ~7u);
    a->non_resident = 0;
    a->data.resident.value_length = fn_size;
    a->data.resident.value_offset = 0x40;
    a->data.resident.indexed = 1;
    fn = (ntfs_file_name_attr_t *) (rec + off + 0x40);
    memset(fn, 0, fn_size);
    fn->file_reference = new_ref;
    fn->parent_directory = parent_mft;
    fn->name_length = (uint8_t) name_chars;
    fn->file_attributes = 0x10;  /* directory */
    memcpy(fn->name, utf16_name, name_bytes);
    off += a->length;

    /* $INDEX_ROOT (空目录索引) */
    {
        ntfs_index_root_t *ir;
        ntfs_index_header_t *ih;
        uint8_t *end_entry;

        a = (ntfs_attr_header_t *) (rec + off);
        a->type = NTFS_ATTR_INDEX_ROOT;
        a->non_resident = 0;
        a->name_length = 0;
        a->name_offset = 0;
        a->flags = 0;
        a->data.resident.value_offset = 0x40;
        a->data.resident.indexed = 0;
        a->data.resident.padding = 0;

        ir = (ntfs_index_root_t *) (rec + off + 0x40);
        memset(ir, 0, 0x100);
        ir->type = NTFS_ATTR_FILE_NAME;
        ir->collation_rule = 0x01;
        ir->index_entry_size = 0xD8;
        ir->clusters_per_index_record = 1;

        ih = (ntfs_index_header_t *) ir->index_header;
        ih->entries_offset = 0x10;
        ih->total_entries_size = 0x10;
        ih->allocated_entries_size = 0x100;
        ih->flags = 0;

        /* 末尾 END 项 */
        end_entry = (uint8_t *) ih + 0x10;
        memset(end_entry, 0, 8);
        ((ntfs_index_entry_t *) end_entry)->flags = NTFS_INDEX_ENTRY_FLAG_END | NTFS_INDEX_ENTRY_FLAG_LAST;
        ((ntfs_index_entry_t *) end_entry)->index_entry_length = 8;

        a->data.resident.value_length = 0x100;
        a->length = (uint16_t) (0x40 + 0x100);
        off += a->length;
    }

    /* 结束标记 */
    a = (ntfs_attr_header_t *) (rec + off);
    a->type = 0xFFFFFFFF;
    a->length = 0;
    off += 0x10;

    h->bytes_in_use = off;
    h->bytes_allocated = g_ntfs_info.mft_record_size;

    if (h->bytes_in_use > g_ntfs_info.mft_record_size) {
        return false;
    }

    if (!ntfs_write_mft_record(new_ref, rec)) {
        return false;
    }

    if (!ntfs_index_add_entry(parent_mft, new_ref, dirname, true)) {
        return false;
    }

    return true;
}

bool ntfs_rmdir(const char *path)
{
    uint64_t mft_ref;
    uint8_t mft_record[4096];
    ntfs_attr_header_t *attr;
    uint8_t *index_data;
    uint32_t index_size;
    ntfs_index_header_t *ih;
    uint32_t offset;
    bool has_children = false;
    const char *dirname;
    char dir_path[NTFS_MAX_PATH + 1];
    uint32_t dir_len;
    uint64_t parent_mft;

    if (!g_ntfs_info.present || path == NULL) {
        return false;
    }
    if (path[0] == '/' && path[1] == '\0') {
        return false;
    }

    if (!ntfs_resolve_path(path, &mft_ref)) {
        return false;
    }
    if (!ntfs_read_mft_record(mft_ref, mft_record)) {
        return false;
    }
    if (!ntfs_is_directory(mft_record)) {
        return false;
    }

    /* 检查目录是否为空 */
    attr = ntfs_find_attribute(mft_record, NTFS_ATTR_INDEX_ROOT, NULL, 0);
    if (attr == NULL || attr->non_resident) {
        return false;
    }
    if (!ntfs_get_attribute_data(mft_record, attr, 0, &index_data, &index_size)) {
        return false;
    }
    if (index_size < 16) {
        return false;
    }
    ih = (ntfs_index_header_t *) (index_data + 16);
    offset = ih->entries_offset;
    while (offset + sizeof(ntfs_index_entry_t) <= ih->total_entries_size) {
        ntfs_index_entry_t *entry = (ntfs_index_entry_t *) (index_data + offset);
        uint32_t elen = (uint32_t) entry->index_entry_length;
        if (elen == 0) break;
        if ((entry->flags & NTFS_INDEX_ENTRY_FLAG_END) == 0) {
            has_children = true;
            break;
        }
        offset += elen;
    }
    if (has_children) {
        return false;  /* 目录非空 */
    }

    /* 分离父目录和目录名 */
    dirname = path;
    while (*dirname != '\0') dirname++;
    while (dirname > path && *dirname != '/') dirname--;
    if (*dirname != '/') return false;
    dirname++;
    if (*dirname == '\0') return false;

    dir_len = (uint32_t) (dirname - path - 1);
    if (dir_len >= sizeof(dir_path)) return false;
    memcpy(dir_path, path, dir_len);
    dir_path[dir_len] = '\0';
    if (dir_len == 0) strcpy(dir_path, "/");

    if (!ntfs_resolve_path(dir_path, &parent_mft)) {
        return false;
    }

    ntfs_index_remove_entry(parent_mft, dirname);

    /* 标记 MFT 记录为未使用 */
    {
        ntfs_mft_record_header_t *h = (ntfs_mft_record_header_t *) mft_record;
        h->flags &= (uint16_t) ~NTFS_MFT_RECORD_FLAG_IN_USE;
        h->hard_link_count = 0;
        return ntfs_write_mft_record(mft_ref, mft_record);
    }
}

static bool ntfs_list_index_entries(const uint8_t *index_data, uint32_t index_size, char *buffer, uint32_t buffer_size, uint32_t *used_out)
{
    uint32_t entries_offset;
    uint32_t entries_end;
    uint32_t offset;
    uint32_t used = *used_out;

    if (!ntfs_index_bounds(index_data, index_size, &entries_offset, &entries_end)) {
        return false;
    }

    offset = entries_offset;
    while (offset + sizeof(ntfs_index_entry_t) <= entries_end) {
        ntfs_index_entry_t *entry = (ntfs_index_entry_t *) (index_data + offset);
        uint32_t entry_len;

        if (!ntfs_index_entry_len(entry, entries_end - offset, &entry_len)) {
            break;
        }

        if ((entry->flags & NTFS_INDEX_ENTRY_FLAG_END) == 0) {
            ntfs_file_name_attr_t *file_name;
            char name[NTFS_MAX_NAME_LEN + 1];
            uint32_t name_len;

            if (!ntfs_index_entry_file_name(entry, entry_len, &file_name)) {
                return false;
            }
            ntfs_utf16_to_utf8((const uint16_t *) file_name->name, file_name->name_length, name, sizeof(name));
            name_len = (uint32_t) strlen(name);

            if (name_len == 0 || (name[0] == '$' && name[1] != '\0')) {
                offset += entry_len;
                continue;
            }

            if (used + name_len + 2 >= buffer_size) {
                *used_out = used;
                return false;
            }

            memcpy(buffer + used, name, name_len);
            used += name_len;

            if (file_name->file_attributes & 0x10) {
                buffer[used++] = '/';
            }

            buffer[used++] = '\n';
            buffer[used] = '\0';
        }

        if (entry->flags & NTFS_INDEX_ENTRY_FLAG_LAST) {
            break;
        }

        offset += entry_len;
    }

    *used_out = used;
    return true;
}

bool ntfs_list_dir(const char *path, char *buffer, uint32_t buffer_size)
{
    uint64_t mft_ref;
    uint8_t mft_record[4096];
    ntfs_attr_header_t *attr;
    uint8_t *index_data;
    uint32_t index_size;
    uint32_t used = 0;

    if (!g_ntfs_info.present || path == NULL || buffer == NULL || buffer_size == 0) {
        return false;
    }

    if (path[0] == '/' && path[1] == '\0') {
        mft_ref = 5;
    } else {
        if (!ntfs_resolve_path(path, &mft_ref)) {
            return false;
        }
    }

    if (!ntfs_read_mft_record(mft_ref, mft_record)) {
        return false;
    }

    if (!ntfs_is_directory(mft_record)) {
        return false;
    }

    buffer[0] = '\0';

    attr = ntfs_find_attribute(mft_record, NTFS_ATTR_INDEX_ROOT, NULL, 0);
    if (attr == NULL) {
        return false;
    }

    if (!ntfs_get_attribute_data(mft_record, attr, 0, &index_data, &index_size)) {
        return false;
    }
    if (index_size < 16) {
        return false;
    }

    if (!ntfs_list_index_entries(index_data + 16, index_size - 16, buffer, buffer_size, &used)) {
        return false;
    }

    attr = ntfs_find_attribute(mft_record, NTFS_ATTR_INDEX_ALLOCATION, NULL, 0);
    if (attr != NULL && attr->non_resident) {
        uint64_t data_size = attr->data.non_resident.data_size;
        uint32_t index_record_size = g_ntfs_info.index_record_size;

        if (index_record_size == 0) {
            index_record_size = g_ntfs_info.cluster_size;
        }

        for (uint64_t vcn = 0; vcn * index_record_size < data_size; vcn++) {
            if (index_record_size > sizeof(g_index_buffer)) {
                break;
            }

            if (!ntfs_read_non_resident_data(mft_record, attr, vcn * index_record_size, g_index_buffer, index_record_size)) {
                break;
            }

            if (memcmp(g_index_buffer, "INDX", 4) == 0) {
                uint32_t offset = 24;

                if (index_record_size <= offset ||
                    !ntfs_list_index_entries(g_index_buffer + offset,
                                             index_record_size - offset,
                                             buffer,
                                             buffer_size,
                                             &used)) {
                    return false;
                }
            }
        }
    }

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

uint64_t ntfs_free_bytes(void)
{
    /* 空闲字节数 = 空闲簇数 * 每簇字节数 */
    return g_ntfs_info.free_clusters * g_ntfs_info.cluster_size;
}

bool ntfs_stat(const char *path, ntfs_stat_t *stat_out)
{
    uint64_t mft_ref;
    uint8_t mft_record[4096];
    ntfs_attr_header_t *attr;
    ntfs_file_name_attr_t *fn;
    uint32_t value_offset;

    if (!g_ntfs_info.present || path == NULL || stat_out == NULL) {
        return false;
    }
    memset(stat_out, 0, sizeof(*stat_out));

    if (!ntfs_resolve_path(path, &mft_ref)) {
        return false;
    }
    if (!ntfs_read_mft_record(mft_ref, mft_record)) {
        return false;
    }

    stat_out->file_size = ntfs_get_file_size(mft_record);
    stat_out->is_dir = ntfs_is_directory(mft_record);

    /* 从 $FILE_NAME 属性提取时间戳与文件属性。 */
    attr = ntfs_find_attribute(mft_record, NTFS_ATTR_FILE_NAME, NULL, 0);
    if (attr != NULL && !attr->non_resident) {
        uint32_t fa;

        value_offset = (uint32_t) ((uint8_t *) attr - mft_record)
                       + attr->data.resident.value_offset;
        fn = (ntfs_file_name_attr_t *) (mft_record + value_offset);

        stat_out->create_time = fn->creation_time;
        stat_out->modify_time = fn->last_data_change_time;
        stat_out->access_time = fn->last_access_time;

        /* NTFS FA_* 位与 FAT 属性位兼容：只读/隐藏/系统/目录/归档。 */
        fa = fn->file_attributes;
        if (fa & 0x0001u) stat_out->attr |= 0x01u;
        if (fa & 0x0002u) stat_out->attr |= 0x02u;
        if (fa & 0x0004u) stat_out->attr |= 0x04u;
        if (fa & 0x0010u) stat_out->attr |= 0x10u;
        if (fa & 0x0020u) stat_out->attr |= 0x20u;
    }

    if (stat_out->is_dir) {
        stat_out->attr |= 0x10u;
    }

    return true;
}

bool ntfs_rename(const char *oldpath, const char *newpath)
{
    uint64_t old_ref;
    uint8_t mft_record[4096];
    bool is_dir;
    const char *old_name;
    const char *new_name;
    char old_parent_path[NTFS_MAX_PATH + 1];
    char new_parent_path[NTFS_MAX_PATH + 1];
    uint32_t split_len;
    uint64_t old_parent_mft;
    uint64_t new_parent_mft;
    uint64_t found_ref;
    uint16_t utf16_name[NTFS_MAX_NAME_LEN];
    uint32_t name_chars;
    uint32_t name_bytes;
    ntfs_attr_header_t *attr;
    ntfs_file_name_attr_t *fn;
    uint32_t value_offset;

    if (!g_ntfs_info.present || oldpath == NULL || newpath == NULL) {
        return false;
    }
    if (oldpath[0] != '/' || newpath[0] != '/') {
        return false;
    }
    if (oldpath[1] == '\0') {
        return false;  /* 不能重命名根目录 */
    }

    /* 解析源文件。 */
    if (!ntfs_resolve_path(oldpath, &old_ref)) {
        return false;
    }
    if (!ntfs_read_mft_record(old_ref, mft_record)) {
        return false;
    }
    is_dir = ntfs_is_directory(mft_record);

    /* 拆分 oldpath：取最后一个 '/' 之后为旧文件名，之前为父目录路径。 */
    old_name = oldpath;
    while (*old_name != '\0') old_name++;
    while (old_name > oldpath && *old_name != '/') old_name--;
    if (*old_name != '/') {
        return false;
    }
    old_name++;
    if (*old_name == '\0') {
        return false;
    }
    split_len = (uint32_t) (old_name - oldpath - 1);
    if (split_len > sizeof(old_parent_path) - 1) {
        return false;
    }
    memcpy(old_parent_path, oldpath, split_len);
    old_parent_path[split_len] = '\0';
    if (split_len == 0) {
        strcpy(old_parent_path, "/");
    }

    /* 拆分 newpath。 */
    new_name = newpath;
    while (*new_name != '\0') new_name++;
    while (new_name > newpath && *new_name != '/') new_name--;
    if (*new_name != '/') {
        return false;
    }
    new_name++;
    if (*new_name == '\0') {
        return false;
    }
    split_len = (uint32_t) (new_name - newpath - 1);
    if (split_len > sizeof(new_parent_path) - 1) {
        return false;
    }
    memcpy(new_parent_path, newpath, split_len);
    new_parent_path[split_len] = '\0';
    if (split_len == 0) {
        strcpy(new_parent_path, "/");
    }

    /* 解析新旧父目录。 */
    if (!ntfs_resolve_path(old_parent_path, &old_parent_mft)) {
        return false;
    }
    if (!ntfs_resolve_path(new_parent_path, &new_parent_mft)) {
        return false;
    }

    /* 同父同名直接成功。 */
    if (old_parent_mft == new_parent_mft && strcmp(old_name, new_name) == 0) {
        return true;
    }

    /* 目标名在新父目录中已存在则失败。 */
    if (ntfs_find_file_in_dir(new_parent_mft, new_name, &found_ref)) {
        return false;
    }

    /* 从旧父目录移除索引项。 */
    if (!ntfs_index_remove_entry(old_parent_mft, old_name)) {
        return false;
    }

    /* 在新父目录添加索引项；失败则回滚旧索引。 */
    if (!ntfs_index_add_entry(new_parent_mft, old_ref, new_name, is_dir)) {
        ntfs_index_add_entry(old_parent_mft, old_ref, old_name, is_dir);
        return false;
    }

    /* 更新文件自身 MFT 记录中 $FILE_NAME 的父目录引用与文件名。 */
    name_chars = ntfs_utf8_to_utf16(new_name, utf16_name, NTFS_MAX_NAME_LEN);
    name_bytes = name_chars * 2u;

    attr = ntfs_find_attribute(mft_record, NTFS_ATTR_FILE_NAME, NULL, 0);
    if (attr == NULL || attr->non_resident) {
        return false;
    }
    /* 驻留属性不支持扩容：新文件名所需空间不得超过已有 value_length。
     * （创建时 value_length = 0x42 + 旧名 utf16 字节，与写入路径一致。） */
    if (attr->data.resident.value_length < 0x42u + name_bytes) {
        return false;
    }

    value_offset = (uint32_t) ((uint8_t *) attr - mft_record)
                   + attr->data.resident.value_offset;
    fn = (ntfs_file_name_attr_t *) (mft_record + value_offset);
    fn->parent_directory = new_parent_mft;
    fn->name_length = (uint8_t) name_chars;
    memcpy(fn->name, utf16_name, name_bytes);

    return ntfs_write_mft_record(old_ref, mft_record);
}
