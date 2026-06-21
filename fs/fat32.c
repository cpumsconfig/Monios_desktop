#include "common.h"
#include "fat32.h"

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

#define FAT32_ATTR_DIRECTORY  0x10
#define FAT32_ATTR_VOLUME_ID  0x08
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
static const char *g_fat32_read_cache_path;
static uint32_t g_fat32_read_cache_start_cluster;
static uint32_t g_fat32_read_cache_cluster_index;
static uint32_t g_fat32_read_cache_cluster;
static bool g_fat32_fat_cache_valid;
static uint32_t g_fat32_fat_cache_lba;
static uint8_t g_fat32_fat_cache[512];

static void fat32_set_entry_cluster(fat32_dir_entry_t *entry, uint32_t cluster);
static uint32_t fat32_entry_cluster(const fat32_dir_entry_t *entry);

static void fat32_clear_read_cache(void)
{
    g_fat32_read_cache_path = NULL;
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
    return type == 0x0B || type == 0x0C;
}

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

static void ata_read_sectors(uint32_t lba, uint8_t count, void *buffer)
{
    uint16_t *dst = (uint16_t *) buffer;

    if (count == 0) {
        return;
    }
    ata_wait_not_busy();
    outb(ATA_DRIVE_PORT, (uint8_t) (0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT_PORT, count);
    outb(ATA_LBA_LOW_PORT, (uint8_t) (lba & 0xFF));
    outb(ATA_LBA_MID_PORT, (uint8_t) ((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH_PORT, (uint8_t) ((lba >> 16) & 0xFF));
    outb(ATA_COMMAND_PORT, ATA_CMD_READ_SECTORS);

    for (uint8_t sector = 0; sector < count; sector++) {
        ata_wait_data_ready();
        for (uint32_t i = 0; i < 256; i++) {
            *dst++ = inw(ATA_DATA_PORT);
        }
    }
}

static void ata_write_sector(uint32_t lba, const void *buffer)
{
    const uint16_t *src = (const uint16_t *) buffer;

    ata_wait_not_busy();
    outb(ATA_DRIVE_PORT, (uint8_t) (0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT_PORT, 1);
    outb(ATA_LBA_LOW_PORT, (uint8_t) (lba & 0xFF));
    outb(ATA_LBA_MID_PORT, (uint8_t) ((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH_PORT, (uint8_t) ((lba >> 16) & 0xFF));
    outb(ATA_COMMAND_PORT, ATA_CMD_WRITE_SECTORS);

    ata_wait_data_ready();
    for (uint32_t i = 0; i < 256; i++) {
        outw(ATA_DATA_PORT, src[i]);
    }
    ata_wait_not_busy();
}

static bool fat32_is_end_cluster(uint32_t cluster)
{
    cluster &= FAT32_CLUSTER_MASK;
    return cluster >= FAT32_CLUSTER_END && cluster != FAT32_CLUSTER_BAD;
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
    uint32_t fat_offset = cluster * 4;
    uint32_t sector_lba = g_fat_lba + (fat_offset / g_bpb.bytes_per_sector);
    uint32_t sector_offset = fat_offset % g_bpb.bytes_per_sector;

    if (!g_fat32_fat_cache_valid || g_fat32_fat_cache_lba != sector_lba) {
        ata_read_sector(sector_lba, g_fat32_fat_cache);
        g_fat32_fat_cache_lba = sector_lba;
        g_fat32_fat_cache_valid = true;
    }
    return (*(uint32_t *) (g_fat32_fat_cache + sector_offset)) & FAT32_CLUSTER_MASK;
}

static void fat32_set_fat_entry(uint32_t cluster, uint32_t value)
{
    uint8_t sector[512];
    uint32_t fat_offset = cluster * 4;
    uint32_t sector_index = fat_offset / g_bpb.bytes_per_sector;
    uint32_t sector_offset = fat_offset % g_bpb.bytes_per_sector;

    for (uint8_t fat = 0; fat < g_bpb.fat_count; fat++) {
        uint32_t sector_lba = g_fat_lba + fat * g_bpb.fat_size_32 + sector_index;
        uint32_t current;
        ata_read_sector(sector_lba, sector);
        current = *(uint32_t *) (sector + sector_offset);
        *(uint32_t *) (sector + sector_offset) = (current & ~FAT32_CLUSTER_MASK) | (value & FAT32_CLUSTER_MASK);
        ata_write_sector(sector_lba, sector);
        if (fat == 0 && g_fat32_fat_cache_valid && g_fat32_fat_cache_lba == sector_lba) {
            memcpy(g_fat32_fat_cache, sector, sizeof(g_fat32_fat_cache));
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

static bool fat32_dir_is_root(const fat32_dir_ref_t *dir)
{
    return dir->is_root;
}

static uint32_t fat32_dir_sector_count(const fat32_dir_ref_t *dir)
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
            ata_write_sector(fat32_cluster_to_lba(cluster) + remaining, buffer);
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
    uint8_t sector[512];
    char target[11];
    uint32_t sector_cluster;

    fat32_format_component(target, name);
    for (uint32_t sector_index = 0; ; sector_index++) {
        if (!fat32_read_dir_sector(dir, sector_index, sector, &sector_cluster)) {
            return false;
        }
        for (uint32_t i = 0; i < 512 / sizeof(fat32_dir_entry_t); i++) {
            fat32_dir_entry_t *entry = ((fat32_dir_entry_t *) sector) + i;
            if (entry->name[0] == 0x00) {
                return false;
            }
            if (entry->name[0] == 0xE5 || (entry->attr & FAT32_ATTR_VOLUME_ID)) {
                continue;
            }
            if (memcmp(entry->name, target, 11) == 0) {
                slot_out->valid = true;
                slot_out->is_root = dir->is_root;
                slot_out->sector_lba = fat32_cluster_to_lba(sector_cluster) + (sector_index % g_bpb.sectors_per_cluster);
                slot_out->byte_offset = (uint16_t) (i * sizeof(fat32_dir_entry_t));
                slot_out->entry = *entry;
                return true;
            }
        }
    }
}

static bool fat32_find_free_slot(fat32_dir_ref_t *dir, fat32_dir_slot_t *slot_out)
{
    uint8_t sector[512];
    uint32_t sector_cluster;

    for (uint32_t sector_index = 0; ; sector_index++) {
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
}

static bool fat32_write_slot(const fat32_dir_ref_t *dir, const fat32_dir_slot_t *slot)
{
    uint8_t sector[512];
    uint32_t sector_lba = slot->sector_lba;
    (void) dir;

    ata_read_sector(sector_lba, sector);
    memcpy(sector + slot->byte_offset, &slot->entry, sizeof(fat32_dir_entry_t));
    ata_write_sector(sector_lba, sector);
    return true;
}

static bool fat32_split_path_component(const char **path_ptr, char component[13])
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

    while (*path != '\0' && *path != '/' && i < 12) {
        component[i++] = *path++;
    }
    component[i] = '\0';
    while (*path == '/') {
        path++;
    }
    *path_ptr = path;
    return true;
}

static bool fat32_resolve_parent(const char *path, fat32_dir_ref_t *parent_out, char final_component[13])
{
    fat32_dir_ref_t dir;
    const char *cursor = path;
    char component[13];
    char next_component[13];
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
            strcpy(final_component, component);
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
        strcpy(component, next_component);
        cursor = saved;
    }
}

static bool fat32_resolve_path(const char *path, fat32_dir_ref_t *parent_out, fat32_dir_slot_t *slot_out, char final_component[13])
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

    ata_read_sector(0, sector);
    memcpy(&g_bpb, sector, sizeof(g_bpb));
    g_volume_lba = 0;

    if (!fat32_bpb_is_valid(&g_bpb)) {
        if (read_le16(sector + 510) != 0xAA55) {
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
        g_fat32_ready = false;
        return false;
    }

    data_sectors = fat32_total_sectors() - data_start;
    g_cluster_count = data_sectors / g_bpb.sectors_per_cluster;
    fat32_clear_read_cache();
    fat32_clear_fat_cache();
    g_fat32_ready = true;
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
    char final_component[13];

    if (!g_fat32_ready) {
        return false;
    }
    return fat32_resolve_path(path, &parent, &slot, final_component) && slot.valid;
}

bool fat32_is_dir(const char *path)
{
    fat32_dir_ref_t parent;
    fat32_dir_slot_t slot;
    char final_component[13];

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

int32_t fat32_file_size(const char *path)
{
    fat32_dir_ref_t parent;
    fat32_dir_slot_t slot;
    char final_component[13];

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
    char final_component[13];
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
    char final_component[13];
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

    if (g_fat32_read_cache_path == path &&
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

    g_fat32_read_cache_path = path;
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
    char final_component[13];
    uint32_t first_cluster = 0;
    uint32_t previous_cluster = 0;
    uint32_t remaining = size;
    const uint8_t *src = (const uint8_t *) buffer;

    if (!g_fat32_ready || !fat32_resolve_parent(path, &parent, final_component) || final_component[0] == '\0') {
        return -1;
    }
    fat32_clear_read_cache();

    if (!fat32_find_entry_in_dir(&parent, final_component, &slot)) {
        if (!fat32_find_free_slot(&parent, &slot)) {
            return -2;
        }
        memset(&slot.entry, 0, sizeof(slot.entry));
        fat32_format_component((char *) slot.entry.name, final_component);
    } else if ((slot.entry.attr & FAT32_ATTR_DIRECTORY) != 0) {
        return -3;
    } else if (fat32_entry_cluster(&slot.entry) >= 2) {
        fat32_free_chain(fat32_entry_cluster(&slot.entry));
    }

    while (remaining > 0) {
        uint32_t cluster = fat32_allocate_cluster();
        uint8_t sector[512];

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

        for (uint8_t sector_index = 0; sector_index < g_bpb.sectors_per_cluster; sector_index++) {
            uint32_t chunk = remaining > 512 ? 512 : remaining;
            memset(sector, 0, sizeof(sector));
            if (chunk > 0) {
                memcpy(sector, src, chunk);
                src += chunk;
                remaining -= chunk;
            }
            ata_write_sector(fat32_cluster_to_lba(cluster) + sector_index, sector);
            if (remaining == 0 && sector_index + 1 >= g_bpb.sectors_per_cluster) {
                break;
            }
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
    char final_component[13];

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
    char final_component[13];
    uint32_t new_cluster;
    uint32_t parent_cluster;

    if (!g_fat32_ready || !fat32_resolve_parent(path, &parent, final_component) || final_component[0] == '\0') {
        return false;
    }
    if (fat32_find_entry_in_dir(&parent, final_component, &slot)) {
        return false;
    }
    if (!fat32_find_free_slot(&parent, &slot)) {
        return false;
    }

    new_cluster = fat32_allocate_cluster();
    if (new_cluster == 0) {
        return false;
    }
    fat32_zero_cluster(new_cluster);

    memset(&slot.entry, 0, sizeof(slot.entry));
    fat32_format_component((char *) slot.entry.name, final_component);
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
    char final_component[13];
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
            if (entry->name[0] == 0xE5) {
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

bool fat32_list_dir(const char *path, char *buffer, uint32_t buffer_size)
{
    fat32_dir_ref_t dir;
    fat32_dir_ref_t parent;
    fat32_dir_slot_t slot;
    char final_component[13];
    uint8_t sector[512];
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
    for (uint32_t sector_index = 0; fat32_read_dir_sector(&dir, sector_index, sector, NULL); sector_index++) {
        for (uint32_t i = 0; i < 512 / sizeof(fat32_dir_entry_t); i++) {
            fat32_dir_entry_t *entry = ((fat32_dir_entry_t *) sector) + i;
            char name[13];
            uint32_t j = 0;

            if (entry->name[0] == 0x00) {
                return true;
            }
            if (entry->name[0] == 0xE5 || (entry->attr & FAT32_ATTR_VOLUME_ID)) {
                continue;
            }
            if (entry->name[0] == '.' && (entry->name[1] == ' ' || entry->name[1] == '.')) {
                continue;
            }
            fat32_decode_name(name, entry->name);
            while (name[j] != '\0') {
                if (used + 2 >= buffer_size) {
                    return false;
                }
                buffer[used++] = name[j++];
            }
            if (entry->attr & FAT32_ATTR_DIRECTORY) {
                if (used + 2 >= buffer_size) {
                    return false;
                }
                buffer[used++] = '/';
            }
            buffer[used++] = '\n';
            buffer[used] = '\0';
        }
    }
    return true;
}

bool fat32_list_root(char *buffer, uint32_t buffer_size)
{
    return fat32_list_dir("/", buffer, buffer_size);
}

