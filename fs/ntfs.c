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

static void ata_read_sectors(uint32_t lba, uint8_t count, void *buffer)
{
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

static ntfs_attr_header_t *ntfs_find_attribute(uint8_t *mft_record, uint32_t attr_type, uint8_t *name, uint32_t name_len)
{
    ntfs_mft_record_header_t *header = (ntfs_mft_record_header_t *) mft_record;
    uint32_t offset = header->first_attribute_offset;
    uint32_t record_size = header->bytes_in_use;

    while (offset < record_size) {
        ntfs_attr_header_t *attr = (ntfs_attr_header_t *) (mft_record + offset);

        if (attr->type == 0xFFFFFFFF) {
            break;
        }

        if (attr->type == attr_type) {
            if (name == NULL || name_len == 0) {
                if (attr->name_length == 0) {
                    return attr;
                }
            } else if (attr->name_length == name_len) {
                uint8_t *attr_name = mft_record + offset + attr->name_offset;
                if (memcmp(attr_name, name, name_len * 2) == 0) {
                    return attr;
                }
            }
        }

        if (attr->length == 0) {
            break;
        }
        offset += attr->length;
    }

    return NULL;
}

static bool ntfs_get_attribute_data(uint8_t *mft_record, ntfs_attr_header_t *attr, uint64_t vcn, uint8_t **data_out, uint32_t *size_out)
{
    if (attr == NULL) {
        return false;
    }

    if (!attr->non_resident) {
        uint32_t offset = (uint32_t) ((uint8_t *) attr - mft_record) + attr->data.resident.value_offset;
        *data_out = mft_record + offset;
        *size_out = attr->data.resident.value_length;
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

static uint64_t ntfs_decode_run_cluster(const uint8_t **data_ptr, uint8_t cluster_bytes)
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

                    for (uint32_t s = 0; s < g_ntfs_info.sectors_per_cluster && bytes_left > 0; s++) {
                        uint64_t sector_offset = (uint64_t) s * 512;
                        if (c == 0 && sector_offset < read_offset) {
                            continue;
                        }

                        uint32_t chunk = 512;
                        if (c == 0 && s == 0 && read_offset > 0) {
                            chunk = (uint32_t) (512 - (read_offset % 512));
                        }
                        if (chunk > bytes_left) {
                            chunk = (uint32_t) bytes_left;
                        }

                        ata_read_sector((uint32_t) (cluster_lba + s), sector_buffer);

                        uint32_t src_offset = 0;
                        if (c == 0 && s == 0 && read_offset > 0) {
                            src_offset = (uint32_t) (read_offset % 512);
                        }

                        memcpy(dst, sector_buffer + src_offset, chunk);
                        dst += chunk;
                        bytes_left -= chunk;
                    }
                }
            }
        }

        current_vcn += run_length;
    }

    return true;
}

static uint64_t ntfs_get_file_size(uint8_t *mft_record)
{
    ntfs_attr_header_t *attr;
    ntfs_file_name_attr_t *file_name;
    uint32_t size;

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

static bool ntfs_find_in_index(const uint8_t *index_data, uint32_t index_size, const char *name, uint64_t *mft_ref_out)
{
    ntfs_index_header_t *index_header;
    uint32_t entries_offset;
    uint32_t entries_size;
    uint32_t offset;

    index_header = (ntfs_index_header_t *) index_data;
    entries_offset = index_header->entries_offset;
    entries_size = index_header->total_entries_size;

    offset = entries_offset;
    while (offset + sizeof(ntfs_index_entry_t) <= entries_size) {
        ntfs_index_entry_t *entry = (ntfs_index_entry_t *) (index_data + offset);

        if (entry->index_entry_length == 0) {
            break;
        }

        if ((entry->flags & NTFS_INDEX_ENTRY_FLAG_END) == 0) {
            ntfs_file_name_attr_t *file_name = (ntfs_file_name_attr_t *) entry->key;

            if (ntfs_compare_name(name, (const uint16_t *) file_name->name, file_name->name_length)) {
                *mft_ref_out = entry->file_reference & 0xFFFFFFFFFFFF;
                return true;
            }
        }

        if (entry->flags & NTFS_INDEX_ENTRY_FLAG_LAST) {
            break;
        }

        offset += entry->index_entry_length;
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
                ntfs_index_header_t *idx_header = (ntfs_index_header_t *) (g_index_buffer + offset);
                uint32_t entries_offset = idx_header->entries_offset;
                uint32_t entries_size = idx_header->total_entries_size;

                if (ntfs_find_in_index(g_index_buffer + offset, entries_size + entries_offset, name, mft_ref_out)) {
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

static bool ntfs_list_index_entries(const uint8_t *index_data, uint32_t index_size, char *buffer, uint32_t buffer_size, uint32_t *used_out)
{
    ntfs_index_header_t *index_header;
    uint32_t entries_offset;
    uint32_t entries_size;
    uint32_t offset;
    uint32_t used = *used_out;

    index_header = (ntfs_index_header_t *) index_data;
    entries_offset = index_header->entries_offset;
    entries_size = index_header->total_entries_size;

    offset = entries_offset;
    while (offset + sizeof(ntfs_index_entry_t) <= entries_size) {
        ntfs_index_entry_t *entry = (ntfs_index_entry_t *) (index_data + offset);

        if (entry->index_entry_length == 0) {
            break;
        }

        if ((entry->flags & NTFS_INDEX_ENTRY_FLAG_END) == 0) {
            ntfs_file_name_attr_t *file_name = (ntfs_file_name_attr_t *) entry->key;
            char name[NTFS_MAX_NAME_LEN + 1];
            uint32_t name_len;

            ntfs_utf16_to_utf8((const uint16_t *) file_name->name, file_name->name_length, name, sizeof(name));
            name_len = (uint32_t) strlen(name);

            if (name_len == 0 || (name[0] == '$' && name[1] != '\0')) {
                offset += entry->index_entry_length;
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

        offset += entry->index_entry_length;
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
                ntfs_index_header_t *idx_header = (ntfs_index_header_t *) (g_index_buffer + offset);
                uint32_t entries_size = idx_header->total_entries_size;

                if (!ntfs_list_index_entries(g_index_buffer + offset, entries_size + idx_header->entries_offset, buffer, buffer_size, &used)) {
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
