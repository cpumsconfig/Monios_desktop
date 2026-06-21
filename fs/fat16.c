#include "common.h"
#include "fat16.h"

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

#define FAT16_ATTR_DIRECTORY  0x10
#define FAT16_ATTR_VOLUME_ID  0x08
#define FAT16_CLUSTER_FREE    0x0000
#define FAT16_CLUSTER_END     0xFFF8
#define FAT16_CLUSTER_BAD     0xFFF7

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
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t fs_type[8];
} __attribute__((packed)) fat16_bpb_t;

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
} __attribute__((packed)) fat16_dir_entry_t;

typedef struct {
    bool is_root;
    uint16_t start_cluster;
} fat16_dir_ref_t;

typedef struct {
    bool valid;
    bool is_root;
    uint32_t sector_lba;
    uint16_t byte_offset;
    fat16_dir_entry_t entry;
} fat16_dir_slot_t;

static fat16_bpb_t g_bpb;
static uint32_t g_fat_lba;
static uint32_t g_root_lba;
static uint32_t g_root_sectors;
static uint32_t g_data_lba;
static uint32_t g_cluster_count;
static bool g_fat16_ready;
static const char *g_fat16_read_cache_path;
static uint16_t g_fat16_read_cache_start_cluster;
static uint32_t g_fat16_read_cache_cluster_index;
static uint16_t g_fat16_read_cache_cluster;

static void fat16_clear_read_cache(void)
{
    g_fat16_read_cache_path = NULL;
    g_fat16_read_cache_start_cluster = 0;
    g_fat16_read_cache_cluster_index = 0;
    g_fat16_read_cache_cluster = 0;
}

static uint32_t fat16_total_sectors(void)
{
    return g_bpb.total_sectors_16 != 0 ? g_bpb.total_sectors_16 : g_bpb.total_sectors_32;
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

static void ata_write_sector(uint32_t lba, const void *buffer)
{
    const uint16_t *src = (const uint16_t *) buffer;

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

static bool fat16_is_end_cluster(uint16_t cluster)
{
    return cluster >= FAT16_CLUSTER_END && cluster != FAT16_CLUSTER_BAD;
}

static void fat16_format_component(char out[11], const char *name)
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

static void fat16_decode_name(char out[13], const uint8_t name[11])
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

static uint32_t fat16_cluster_to_lba(uint16_t cluster)
{
    return g_data_lba + (uint32_t) (cluster - 2) * g_bpb.sectors_per_cluster;
}

static uint16_t fat16_get_fat_entry(uint16_t cluster)
{
    uint8_t sector[512];
    uint32_t fat_offset = (uint32_t) cluster * 2;
    uint32_t sector_lba = g_fat_lba + (fat_offset / g_bpb.bytes_per_sector);
    uint32_t sector_offset = fat_offset % g_bpb.bytes_per_sector;

    ata_read_sector(sector_lba, sector);
    return *(uint16_t *) (sector + sector_offset);
}

static void fat16_set_fat_entry(uint16_t cluster, uint16_t value)
{
    uint8_t sector[512];
    uint32_t fat_offset = (uint32_t) cluster * 2;
    uint32_t sector_index = fat_offset / g_bpb.bytes_per_sector;
    uint32_t sector_offset = fat_offset % g_bpb.bytes_per_sector;

    for (uint8_t fat = 0; fat < g_bpb.fat_count; fat++) {
        uint32_t sector_lba = g_fat_lba + fat * g_bpb.fat_size_16 + sector_index;
        ata_read_sector(sector_lba, sector);
        *(uint16_t *) (sector + sector_offset) = value;
        ata_write_sector(sector_lba, sector);
    }
}

static uint16_t fat16_allocate_cluster(void)
{
    for (uint16_t cluster = 2; cluster < g_cluster_count + 2; cluster++) {
        if (fat16_get_fat_entry(cluster) == FAT16_CLUSTER_FREE) {
            fat16_set_fat_entry(cluster, 0xFFFF);
            return cluster;
        }
    }
    return 0;
}

static void fat16_zero_cluster(uint16_t cluster)
{
    uint8_t sector[512];
    memset(sector, 0, sizeof(sector));
    for (uint8_t i = 0; i < g_bpb.sectors_per_cluster; i++) {
        ata_write_sector(fat16_cluster_to_lba(cluster) + i, sector);
    }
}

static void fat16_free_chain(uint16_t cluster)
{
    while (cluster >= 2 && cluster < g_cluster_count + 2) {
        uint16_t next = fat16_get_fat_entry(cluster);
        fat16_set_fat_entry(cluster, FAT16_CLUSTER_FREE);
        if (fat16_is_end_cluster(next) || next == FAT16_CLUSTER_FREE || next == FAT16_CLUSTER_BAD) {
            break;
        }
        cluster = next;
    }
}

static bool fat16_dir_is_root(const fat16_dir_ref_t *dir)
{
    return dir->is_root;
}

static uint32_t fat16_dir_sector_count(const fat16_dir_ref_t *dir)
{
    if (dir->is_root) {
        return g_root_sectors;
    }
    return g_bpb.sectors_per_cluster;
}

static bool fat16_read_dir_sector(const fat16_dir_ref_t *dir, uint32_t sector_index, uint8_t *buffer, uint16_t *cluster_out)
{
    if (dir->is_root) {
        if (sector_index >= g_root_sectors) {
            return false;
        }
        ata_read_sector(g_root_lba + sector_index, buffer);
        if (cluster_out) *cluster_out = 0;
        return true;
    }

    uint16_t cluster = dir->start_cluster;
    uint32_t remaining = sector_index;

    while (cluster >= 2 && cluster < g_cluster_count + 2) {
        if (remaining < g_bpb.sectors_per_cluster) {
            ata_read_sector(fat16_cluster_to_lba(cluster) + remaining, buffer);
            if (cluster_out) *cluster_out = cluster;
            return true;
        }
        remaining -= g_bpb.sectors_per_cluster;
        cluster = fat16_get_fat_entry(cluster);
        if (fat16_is_end_cluster(cluster)) {
            break;
        }
    }
    return false;
}

static bool fat16_write_dir_sector(const fat16_dir_ref_t *dir, uint32_t sector_index, const uint8_t *buffer)
{
    if (dir->is_root) {
        if (sector_index >= g_root_sectors) {
            return false;
        }
        ata_write_sector(g_root_lba + sector_index, buffer);
        return true;
    }

    uint16_t cluster = dir->start_cluster;
    uint32_t remaining = sector_index;

    while (cluster >= 2 && cluster < g_cluster_count + 2) {
        if (remaining < g_bpb.sectors_per_cluster) {
            ata_write_sector(fat16_cluster_to_lba(cluster) + remaining, buffer);
            return true;
        }
        remaining -= g_bpb.sectors_per_cluster;
        cluster = fat16_get_fat_entry(cluster);
        if (fat16_is_end_cluster(cluster)) {
            break;
        }
    }
    return false;
}

static bool fat16_extend_dir(fat16_dir_ref_t *dir)
{
    uint16_t new_cluster;
    uint16_t cluster;

    if (dir->is_root) {
        return false;
    }

    new_cluster = fat16_allocate_cluster();
    if (new_cluster == 0) {
        return false;
    }
    fat16_zero_cluster(new_cluster);

    cluster = dir->start_cluster;
    while (!fat16_is_end_cluster(fat16_get_fat_entry(cluster))) {
        cluster = fat16_get_fat_entry(cluster);
    }
    fat16_set_fat_entry(cluster, new_cluster);
    fat16_set_fat_entry(new_cluster, 0xFFFF);
    return true;
}

static bool fat16_find_entry_in_dir(const fat16_dir_ref_t *dir, const char *name, fat16_dir_slot_t *slot_out)
{
    uint8_t sector[512];
    char target[11];
    uint16_t sector_cluster;

    fat16_format_component(target, name);
    for (uint32_t sector_index = 0; ; sector_index++) {
        if (!fat16_read_dir_sector(dir, sector_index, sector, &sector_cluster)) {
            return false;
        }
        for (uint32_t i = 0; i < 512 / sizeof(fat16_dir_entry_t); i++) {
            fat16_dir_entry_t *entry = ((fat16_dir_entry_t *) sector) + i;
            if (entry->name[0] == 0x00) {
                return false;
            }
            if (entry->name[0] == 0xE5 || (entry->attr & FAT16_ATTR_VOLUME_ID)) {
                continue;
            }
            if (memcmp(entry->name, target, 11) == 0) {
                slot_out->valid = true;
                slot_out->is_root = dir->is_root;
                slot_out->sector_lba = dir->is_root
                    ? (g_root_lba + sector_index)
                    : (fat16_cluster_to_lba(sector_cluster) + (sector_index % g_bpb.sectors_per_cluster));
                slot_out->byte_offset = (uint16_t) (i * sizeof(fat16_dir_entry_t));
                slot_out->entry = *entry;
                return true;
            }
        }
    }
}

static bool fat16_find_free_slot(fat16_dir_ref_t *dir, fat16_dir_slot_t *slot_out)
{
    uint8_t sector[512];
    uint16_t sector_cluster;

    for (uint32_t sector_index = 0; ; sector_index++) {
        if (!fat16_read_dir_sector(dir, sector_index, sector, &sector_cluster)) {
            if (fat16_extend_dir(dir)) {
                continue;
            }
            return false;
        }
        for (uint32_t i = 0; i < 512 / sizeof(fat16_dir_entry_t); i++) {
            fat16_dir_entry_t *entry = ((fat16_dir_entry_t *) sector) + i;
            if (entry->name[0] == 0x00 || entry->name[0] == 0xE5) {
                slot_out->valid = true;
                slot_out->is_root = dir->is_root;
                slot_out->sector_lba = dir->is_root
                    ? (g_root_lba + sector_index)
                    : (fat16_cluster_to_lba(sector_cluster) + (sector_index % g_bpb.sectors_per_cluster));
                slot_out->byte_offset = (uint16_t) (i * sizeof(fat16_dir_entry_t));
                memset(&slot_out->entry, 0, sizeof(slot_out->entry));
                return true;
            }
        }
    }
}

static bool fat16_write_slot(const fat16_dir_ref_t *dir, const fat16_dir_slot_t *slot)
{
    uint8_t sector[512];
    uint32_t sector_lba = slot->sector_lba;
    (void) dir;

    ata_read_sector(sector_lba, sector);
    memcpy(sector + slot->byte_offset, &slot->entry, sizeof(fat16_dir_entry_t));
    ata_write_sector(sector_lba, sector);
    return true;
}

static bool fat16_split_path_component(const char **path_ptr, char component[13])
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

static bool fat16_resolve_parent(const char *path, fat16_dir_ref_t *parent_out, char final_component[13])
{
    fat16_dir_ref_t dir;
    const char *cursor = path;
    char component[13];
    char next_component[13];
    fat16_dir_slot_t slot;

    dir.is_root = true;
    dir.start_cluster = 0;

    if (!fat16_split_path_component(&cursor, component)) {
        final_component[0] = '\0';
        *parent_out = dir;
        return true;
    }

    while (1) {
        const char *saved = cursor;
        if (!fat16_split_path_component(&saved, next_component)) {
            strcpy(final_component, component);
            *parent_out = dir;
            return true;
        }

        if (!fat16_find_entry_in_dir(&dir, component, &slot)) {
            return false;
        }
        if ((slot.entry.attr & FAT16_ATTR_DIRECTORY) == 0) {
            return false;
        }
        dir.is_root = false;
        dir.start_cluster = slot.entry.first_cluster_low;
        strcpy(component, next_component);
        cursor = saved;
    }
}

static bool fat16_resolve_path(const char *path, fat16_dir_ref_t *parent_out, fat16_dir_slot_t *slot_out, char final_component[13])
{
    if (!fat16_resolve_parent(path, parent_out, final_component)) {
        return false;
    }
    if (final_component[0] == '\0') {
        slot_out->valid = false;
        return true;
    }
    return fat16_find_entry_in_dir(parent_out, final_component, slot_out);
}

static void fat16_init_dot_entries(fat16_dir_ref_t *new_dir, uint16_t self_cluster, uint16_t parent_cluster)
{
    uint8_t sector[512];
    fat16_dir_entry_t *entries = (fat16_dir_entry_t *) sector;

    memset(sector, 0, sizeof(sector));
    memset(entries[0].name, ' ', 11);
    entries[0].name[0] = '.';
    entries[0].attr = FAT16_ATTR_DIRECTORY;
    entries[0].first_cluster_low = self_cluster;

    memset(entries[1].name, ' ', 11);
    entries[1].name[0] = '.';
    entries[1].name[1] = '.';
    entries[1].attr = FAT16_ATTR_DIRECTORY;
    entries[1].first_cluster_low = parent_cluster;

    fat16_write_dir_sector(new_dir, 0, sector);
}

bool fat16_init(void)
{
    uint8_t sector[512];
    uint32_t data_sectors;

    ata_read_sector(0, sector);
    memcpy(&g_bpb, sector, sizeof(g_bpb));

    if (g_bpb.bytes_per_sector != 512 || g_bpb.sectors_per_cluster == 0 || g_bpb.fat_size_16 == 0) {
        g_fat16_ready = false;
        return false;
    }

    g_fat_lba = g_bpb.reserved_sector_count;
    g_root_sectors = ((uint32_t) g_bpb.root_entry_count * 32 + (g_bpb.bytes_per_sector - 1)) / g_bpb.bytes_per_sector;
    g_root_lba = g_fat_lba + (uint32_t) g_bpb.fat_count * g_bpb.fat_size_16;
    g_data_lba = g_root_lba + g_root_sectors;
    if (fat16_total_sectors() <= g_data_lba) {
        g_fat16_ready = false;
        return false;
    }

    data_sectors = fat16_total_sectors() - g_data_lba;
    g_cluster_count = data_sectors / g_bpb.sectors_per_cluster;
    fat16_clear_read_cache();
    g_fat16_ready = true;
    return true;
}

uint16_t fat16_root_entry_count(void)
{
    char buffer[4096];
    uint16_t count = 0;

    if (!fat16_list_root(buffer, sizeof(buffer))) {
        return 0;
    }

    for (uint32_t i = 0; buffer[i] != '\0'; i++) {
        if (buffer[i] == '\n') {
            count++;
        }
    }
    return count;
}

bool fat16_exists(const char *path)
{
    fat16_dir_ref_t parent;
    fat16_dir_slot_t slot;
    char final_component[13];

    if (!g_fat16_ready) {
        return false;
    }
    return fat16_resolve_path(path, &parent, &slot, final_component) && slot.valid;
}

bool fat16_is_dir(const char *path)
{
    fat16_dir_ref_t parent;
    fat16_dir_slot_t slot;
    char final_component[13];

    if (!g_fat16_ready) {
        return false;
    }
    if (path == NULL || path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        return true;
    }
    if (!fat16_resolve_path(path, &parent, &slot, final_component) || !slot.valid) {
        return false;
    }
    return (slot.entry.attr & FAT16_ATTR_DIRECTORY) != 0;
}

int32_t fat16_file_size(const char *path)
{
    fat16_dir_ref_t parent;
    fat16_dir_slot_t slot;
    char final_component[13];

    if (!g_fat16_ready || !fat16_resolve_path(path, &parent, &slot, final_component) || !slot.valid) {
        return -1;
    }
    if ((slot.entry.attr & FAT16_ATTR_DIRECTORY) != 0) {
        return -1;
    }
    return (int32_t) slot.entry.file_size;
}

int32_t fat16_read_file(const char *path, void *buffer, uint32_t buffer_size)
{
    fat16_dir_ref_t parent;
    fat16_dir_slot_t slot;
    char final_component[13];
    uint16_t cluster;
    uint32_t bytes_left;
    uint8_t *dst = (uint8_t *) buffer;

    if (!g_fat16_ready || !fat16_resolve_path(path, &parent, &slot, final_component) || !slot.valid) {
        return -1;
    }
    if ((slot.entry.attr & FAT16_ATTR_DIRECTORY) != 0) {
        return -3;
    }

    cluster = slot.entry.first_cluster_low;
    bytes_left = slot.entry.file_size;
    while (cluster >= 2 && cluster < g_cluster_count + 2 && bytes_left > 0) {
        for (uint8_t sector_index = 0; sector_index < g_bpb.sectors_per_cluster && bytes_left > 0; sector_index++) {
            uint8_t sector[512];
            uint32_t chunk = bytes_left > 512 ? 512 : bytes_left;

            if ((uint32_t) (dst - (uint8_t *) buffer) + chunk > buffer_size) {
                return -2;
            }

            ata_read_sector(fat16_cluster_to_lba(cluster) + sector_index, sector);
            memcpy(dst, sector, chunk);
            dst += chunk;
            bytes_left -= chunk;
        }
        cluster = fat16_get_fat_entry(cluster);
        if (fat16_is_end_cluster(cluster)) {
            break;
        }
    }

    return (int32_t) slot.entry.file_size;
}

int32_t fat16_read_file_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size)
{
    fat16_dir_ref_t parent;
    fat16_dir_slot_t slot;
    char final_component[13];
    uint16_t cluster;
    uint16_t first_cluster;
    uint32_t file_size;
    uint32_t cluster_bytes;
    uint32_t target_cluster_index;
    uint32_t walk_clusters;
    uint32_t offset_in_cluster;
    uint32_t bytes_left;
    uint8_t *dst = (uint8_t *) buffer;
    uint32_t bytes_read = 0;

    if (!g_fat16_ready || buffer == NULL || !fat16_resolve_path(path, &parent, &slot, final_component) || !slot.valid) {
        return -1;
    }
    if ((slot.entry.attr & FAT16_ATTR_DIRECTORY) != 0) {
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

    first_cluster = slot.entry.first_cluster_low;
    cluster = first_cluster;
    cluster_bytes = (uint32_t) g_bpb.sectors_per_cluster * 512u;
    target_cluster_index = offset / cluster_bytes;
    offset_in_cluster = offset % cluster_bytes;

    if (g_fat16_read_cache_path == path &&
        g_fat16_read_cache_start_cluster == first_cluster &&
        g_fat16_read_cache_cluster >= 2 &&
        g_fat16_read_cache_cluster < g_cluster_count + 2 &&
        g_fat16_read_cache_cluster_index <= target_cluster_index) {
        cluster = g_fat16_read_cache_cluster;
        walk_clusters = target_cluster_index - g_fat16_read_cache_cluster_index;
    } else {
        walk_clusters = target_cluster_index;
    }

    while (walk_clusters > 0 && cluster >= 2 && cluster < g_cluster_count + 2) {
        cluster = fat16_get_fat_entry(cluster);
        walk_clusters--;
        if (fat16_is_end_cluster(cluster)) {
            return (int32_t) bytes_read;
        }
    }

    g_fat16_read_cache_path = path;
    g_fat16_read_cache_start_cluster = first_cluster;
    g_fat16_read_cache_cluster_index = target_cluster_index;
    g_fat16_read_cache_cluster = cluster;

    while (cluster >= 2 && cluster < g_cluster_count + 2 && bytes_left > 0) {
        for (uint8_t sector_index = 0; sector_index < g_bpb.sectors_per_cluster && bytes_left > 0; sector_index++) {
            uint8_t sector[512];
            uint32_t sector_offset = (uint32_t) sector_index * 512u;
            uint32_t start = 0;
            uint32_t chunk;

            if (offset_in_cluster >= sector_offset + 512u) {
                continue;
            }
            if (offset_in_cluster > sector_offset) {
                start = offset_in_cluster - sector_offset;
            }
            chunk = 512u - start;
            if (chunk > bytes_left) {
                chunk = bytes_left;
            }
            ata_read_sector(fat16_cluster_to_lba(cluster) + sector_index, sector);
            memcpy(dst, sector + start, chunk);
            dst += chunk;
            bytes_read += chunk;
            bytes_left -= chunk;
        }
        offset_in_cluster = 0;
        if (bytes_left == 0) {
            break;
        }
        cluster = fat16_get_fat_entry(cluster);
        if (fat16_is_end_cluster(cluster)) {
            break;
        }
        g_fat16_read_cache_cluster_index++;
        g_fat16_read_cache_cluster = cluster;
    }

    return (int32_t) bytes_read;
}

int32_t fat16_write_file(const char *path, const void *buffer, uint32_t size)
{
    fat16_dir_ref_t parent;
    fat16_dir_slot_t slot;
    char final_component[13];
    uint16_t first_cluster = 0;
    uint16_t previous_cluster = 0;
    uint32_t remaining = size;
    const uint8_t *src = (const uint8_t *) buffer;

    if (!g_fat16_ready || !fat16_resolve_parent(path, &parent, final_component) || final_component[0] == '\0') {
        return -1;
    }
    fat16_clear_read_cache();

    if (!fat16_find_entry_in_dir(&parent, final_component, &slot)) {
        if (!fat16_find_free_slot(&parent, &slot)) {
            return -2;
        }
        memset(&slot.entry, 0, sizeof(slot.entry));
        fat16_format_component((char *) slot.entry.name, final_component);
    } else if ((slot.entry.attr & FAT16_ATTR_DIRECTORY) != 0) {
        return -3;
    } else if (slot.entry.first_cluster_low >= 2) {
        fat16_free_chain(slot.entry.first_cluster_low);
    }

    while (remaining > 0) {
        uint16_t cluster = fat16_allocate_cluster();
        uint8_t sector[512];

        if (cluster == 0) {
            if (first_cluster != 0) {
                fat16_free_chain(first_cluster);
            }
            return -4;
        }
        if (first_cluster == 0) {
            first_cluster = cluster;
        }
        if (previous_cluster != 0) {
            fat16_set_fat_entry(previous_cluster, cluster);
        }
        fat16_set_fat_entry(cluster, 0xFFFF);
        previous_cluster = cluster;

        for (uint8_t sector_index = 0; sector_index < g_bpb.sectors_per_cluster; sector_index++) {
            uint32_t chunk = remaining > 512 ? 512 : remaining;
            memset(sector, 0, sizeof(sector));
            if (chunk > 0) {
                memcpy(sector, src, chunk);
                src += chunk;
                remaining -= chunk;
            }
            ata_write_sector(fat16_cluster_to_lba(cluster) + sector_index, sector);
            if (remaining == 0 && sector_index + 1 >= g_bpb.sectors_per_cluster) {
                break;
            }
        }
    }

    slot.entry.attr = 0;
    slot.entry.first_cluster_low = first_cluster;
    slot.entry.file_size = size;
    if (!fat16_write_slot(&parent, &slot)) {
        return -5;
    }
    return (int32_t) size;
}

bool fat16_delete(const char *path)
{
    fat16_dir_ref_t parent;
    fat16_dir_slot_t slot;
    char final_component[13];

    if (!g_fat16_ready || !fat16_resolve_path(path, &parent, &slot, final_component) || !slot.valid) {
        return false;
    }
    fat16_clear_read_cache();
    if ((slot.entry.attr & FAT16_ATTR_DIRECTORY) != 0) {
        return false;
    }
    if (slot.entry.first_cluster_low >= 2) {
        fat16_free_chain(slot.entry.first_cluster_low);
    }
    slot.entry.name[0] = 0xE5;
    return fat16_write_slot(&parent, &slot);
}

bool fat16_mkdir(const char *path)
{
    fat16_dir_ref_t parent;
    fat16_dir_ref_t new_dir;
    fat16_dir_slot_t slot;
    char final_component[13];
    uint16_t new_cluster;
    uint16_t parent_cluster;

    if (!g_fat16_ready || !fat16_resolve_parent(path, &parent, final_component) || final_component[0] == '\0') {
        return false;
    }
    if (fat16_find_entry_in_dir(&parent, final_component, &slot)) {
        return false;
    }
    if (!fat16_find_free_slot(&parent, &slot)) {
        return false;
    }

    new_cluster = fat16_allocate_cluster();
    if (new_cluster == 0) {
        return false;
    }
    fat16_zero_cluster(new_cluster);

    memset(&slot.entry, 0, sizeof(slot.entry));
    fat16_format_component((char *) slot.entry.name, final_component);
    slot.entry.attr = FAT16_ATTR_DIRECTORY;
    slot.entry.first_cluster_low = new_cluster;
    slot.entry.file_size = 0;
    if (!fat16_write_slot(&parent, &slot)) {
        fat16_free_chain(new_cluster);
        return false;
    }

    new_dir.is_root = false;
    new_dir.start_cluster = new_cluster;
    parent_cluster = parent.is_root ? 0 : parent.start_cluster;
    fat16_init_dot_entries(&new_dir, new_cluster, parent_cluster);
    return true;
}

bool fat16_rmdir(const char *path)
{
    fat16_dir_ref_t parent;
    fat16_dir_slot_t slot;
    char final_component[13];
    fat16_dir_ref_t dir;
    uint8_t sector[512];

    if (!g_fat16_ready || !fat16_resolve_path(path, &parent, &slot, final_component) || !slot.valid) {
        return false;
    }
    if ((slot.entry.attr & FAT16_ATTR_DIRECTORY) == 0) {
        return false;
    }

    dir.is_root = false;
    dir.start_cluster = slot.entry.first_cluster_low;
    for (uint32_t sector_index = 0; fat16_read_dir_sector(&dir, sector_index, sector, NULL); sector_index++) {
        for (uint32_t i = 0; i < 512 / sizeof(fat16_dir_entry_t); i++) {
            fat16_dir_entry_t *entry = ((fat16_dir_entry_t *) sector) + i;
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
    fat16_free_chain(slot.entry.first_cluster_low);
    slot.entry.name[0] = 0xE5;
    return fat16_write_slot(&parent, &slot);
}

bool fat16_list_dir(const char *path, char *buffer, uint32_t buffer_size)
{
    fat16_dir_ref_t dir;
    fat16_dir_ref_t parent;
    fat16_dir_slot_t slot;
    char final_component[13];
    uint8_t sector[512];
    uint32_t used = 0;

    if (!g_fat16_ready || buffer_size == 0) {
        return false;
    }

    if (path == NULL || path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        dir.is_root = true;
        dir.start_cluster = 0;
    } else {
        if (!fat16_resolve_path(path, &parent, &slot, final_component) || !slot.valid) {
            return false;
        }
        if ((slot.entry.attr & FAT16_ATTR_DIRECTORY) == 0) {
            return false;
        }
        dir.is_root = false;
        dir.start_cluster = slot.entry.first_cluster_low;
    }

    buffer[0] = '\0';
    for (uint32_t sector_index = 0; fat16_read_dir_sector(&dir, sector_index, sector, NULL); sector_index++) {
        for (uint32_t i = 0; i < 512 / sizeof(fat16_dir_entry_t); i++) {
            fat16_dir_entry_t *entry = ((fat16_dir_entry_t *) sector) + i;
            char name[13];
            uint32_t j = 0;

            if (entry->name[0] == 0x00) {
                return true;
            }
            if (entry->name[0] == 0xE5 || (entry->attr & FAT16_ATTR_VOLUME_ID)) {
                continue;
            }
            if (entry->name[0] == '.' && (entry->name[1] == ' ' || entry->name[1] == '.')) {
                continue;
            }
            fat16_decode_name(name, entry->name);
            while (name[j] != '\0') {
                if (used + 2 >= buffer_size) {
                    return false;
                }
                buffer[used++] = name[j++];
            }
            if (entry->attr & FAT16_ATTR_DIRECTORY) {
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

bool fat16_list_root(char *buffer, uint32_t buffer_size)
{
    return fat16_list_dir("/", buffer, buffer_size);
}
