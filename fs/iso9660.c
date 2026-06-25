#include "common.h"
#include "cdrom.h"
#include "iso9660.h"
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
#define ATA_STATUS_BSY        0x80
#define ATA_STATUS_DRQ        0x08
#define ATA_WAIT_LIMIT        1000000U

#define ISO9660_BLOCK_SIZE    2048
#define ISO9660_ATA_SECTORS   4
#define ISO9660_PVD_BLOCK     16
#define ISO9660_ATTR_DIR      0x02
#define ISO9660_ATTR_HIDDEN   0x01

typedef struct {
    bool valid;
    uint32_t extent;
    uint32_t size;
    uint8_t flags;
    uint8_t name_len;
    uint8_t record_len;
} iso9660_entry_t;

static iso9660_entry_t g_root;
static iso9660_info_t g_iso9660_info;
static uint8_t g_iso9660_block[ISO9660_BLOCK_SIZE];

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

static void iso9660_read_block(uint32_t block, uint8_t *buffer)
{
    uint32_t ata_lba = block * ISO9660_ATA_SECTORS;

    if (cdrom_is_ready() && cdrom_read_sectors(block, 1, buffer)) {
        return;
    }
    for (uint32_t i = 0; i < ISO9660_ATA_SECTORS; i++) {
        ata_read_sector(ata_lba + i, buffer + i * 512u);
    }
}

static uint32_t iso9660_read32_le(const uint8_t *data)
{
    return ((uint32_t) data[0]) |
           ((uint32_t) data[1] << 8) |
           ((uint32_t) data[2] << 16) |
           ((uint32_t) data[3] << 24);
}

static uint16_t iso9660_read16_le(const uint8_t *data)
{
    return (uint16_t) data[0] | ((uint16_t) data[1] << 8);
}

static void iso9660_record_to_entry(const uint8_t *record, iso9660_entry_t *entry)
{
    entry->valid = record != NULL && record[0] >= 34;
    if (!entry->valid) {
        return;
    }
    entry->record_len = record[0];
    entry->extent = iso9660_read32_le(record + 2);
    entry->size = iso9660_read32_le(record + 10);
    entry->flags = record[25];
    entry->name_len = record[32];
}

static bool iso9660_is_dir_entry(const iso9660_entry_t *entry)
{
    return entry != NULL && entry->valid && (entry->flags & ISO9660_ATTR_DIR) != 0;
}

static bool iso9660_is_hidden_entry(const iso9660_entry_t *entry)
{
    return entry != NULL && entry->valid && (entry->flags & ISO9660_ATTR_HIDDEN) != 0;
}

static bool iso9660_split_path_component(const char **path_ptr, char component[64])
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
    while (*path != '\0' && *path != '/' && i + 1 < 64) {
        component[i++] = *path++;
    }
    component[i] = '\0';
    while (*path == '/') {
        path++;
    }
    *path_ptr = path;
    return true;
}

static char iso9660_upper(char ch)
{
    if (ch >= 'a' && ch <= 'z') {
        return (char) (ch - 'a' + 'A');
    }
    return ch;
}

static uint32_t iso9660_identifier_len(const uint8_t *id, uint8_t id_len)
{
    uint32_t len = id_len;

    for (uint32_t i = 0; i < len; i++) {
        if (id[i] == ';') {
            len = i;
            break;
        }
    }
    while (len > 0 && id[len - 1] == '.') {
        len--;
    }
    return len;
}

static bool iso9660_name_matches(const uint8_t *id, uint8_t id_len, const char *target)
{
    uint32_t name_len;
    uint32_t target_len;

    if (id_len == 1 && (id[0] == 0 || id[0] == 1)) {
        return false;
    }

    name_len = iso9660_identifier_len(id, id_len);
    target_len = (uint32_t) strlen(target);
    while (target_len > 0 && target[target_len - 1] == '.') {
        target_len--;
    }
    if (name_len != target_len) {
        return false;
    }
    for (uint32_t i = 0; i < name_len; i++) {
        if (iso9660_upper((char) id[i]) != iso9660_upper(target[i])) {
            return false;
        }
    }
    return true;
}

static void iso9660_decode_name(char *out, uint32_t out_size, const uint8_t *id, uint8_t id_len)
{
    uint32_t len;
    uint32_t i;

    if (out_size == 0) {
        return;
    }
    if (id_len == 1 && id[0] == 0) {
        strcpy(out, ".");
        return;
    }
    if (id_len == 1 && id[0] == 1) {
        strcpy(out, "..");
        return;
    }

    len = iso9660_identifier_len(id, id_len);
    if (len + 1 > out_size) {
        len = out_size - 1;
    }
    for (i = 0; i < len; i++) {
        out[i] = (char) id[i];
    }
    out[i] = '\0';
}

static bool iso9660_find_in_dir(const iso9660_entry_t *dir, const char *name, iso9660_entry_t *out)
{
    uint32_t offset = 0;

    if (!iso9660_is_dir_entry(dir) || name == NULL || name[0] == '\0') {
        return false;
    }

    while (offset < dir->size) {
        uint32_t block_index = offset / ISO9660_BLOCK_SIZE;
        uint32_t block_offset = offset % ISO9660_BLOCK_SIZE;
        const uint8_t *record;
        uint8_t record_len;
        uint8_t id_len;

        iso9660_read_block(dir->extent + block_index, g_iso9660_block);
        record = g_iso9660_block + block_offset;
        record_len = record[0];
        if (record_len == 0) {
            offset = (block_index + 1) * ISO9660_BLOCK_SIZE;
            continue;
        }
        if (block_offset + record_len > ISO9660_BLOCK_SIZE || record_len < 34) {
            return false;
        }
        id_len = record[32];
        if (33u + id_len <= record_len && iso9660_name_matches(record + 33, id_len, name)) {
            iso9660_record_to_entry(record, out);
            return out->valid;
        }
        offset += record_len;
    }
    return false;
}

static bool iso9660_resolve_path(const char *path, iso9660_entry_t *out)
{
    iso9660_entry_t current;
    const char *cursor = path;
    char component[64];

    if (!g_iso9660_info.ready || path == NULL || out == NULL) {
        return false;
    }
    current = g_root;
    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        *out = current;
        return true;
    }
    while (iso9660_split_path_component(&cursor, component)) {
        iso9660_entry_t next;

        if (!iso9660_find_in_dir(&current, component, &next)) {
            return false;
        }
        current = next;
    }
    *out = current;
    return true;
}

static void iso9660_parse_pvd(const uint8_t *pvd)
{
    /* 卷标 */
    memset(g_iso9660_info.volume_id, 0, sizeof(g_iso9660_info.volume_id));
    memcpy(g_iso9660_info.volume_id, pvd + 40, 32);

    /* 发布者 */
    memset(g_iso9660_info.publisher, 0, sizeof(g_iso9660_info.publisher));
    memcpy(g_iso9660_info.publisher, pvd + 318, 128);

    /* 准备者 */
    memset(g_iso9660_info.preparer, 0, sizeof(g_iso9660_info.preparer));
    memcpy(g_iso9660_info.preparer, pvd + 446, 128);

    /* 应用程序 */
    memset(g_iso9660_info.application, 0, sizeof(g_iso9660_info.application));
    memcpy(g_iso9660_info.application, pvd + 574, 128);

    /* 总块数 */
    g_iso9660_info.total_blocks = iso9660_read32_le(pvd + 80);

    /* 块大小 */
    g_iso9660_info.block_size = iso9660_read16_le(pvd + 128);
}

bool iso9660_init(void)
{
    const uint8_t *root_record;

    memset(&g_iso9660_info, 0, sizeof(g_iso9660_info));
    memset(&g_root, 0, sizeof(g_root));

    iso9660_read_block(ISO9660_PVD_BLOCK, g_iso9660_block);
    if (g_iso9660_block[0] != 1 || memcmp(g_iso9660_block + 1, "CD001", 5) != 0 || g_iso9660_block[6] != 1) {
        g_iso9660_info.present = false;
        g_iso9660_info.ready = false;
        strcpy(g_iso9660_info.status, "iso9660: not found");
        return false;
    }

    g_iso9660_info.present = true;
    iso9660_parse_pvd(g_iso9660_block);

    root_record = g_iso9660_block + 156;
    if (root_record[0] < 34) {
        g_iso9660_info.ready = false;
        strcpy(g_iso9660_info.status, "iso9660: invalid root entry");
        return false;
    }
    iso9660_record_to_entry(root_record, &g_root);
    g_iso9660_info.root_extent = g_root.extent;
    g_iso9660_info.root_size = g_root.size;

    g_iso9660_info.ready = g_root.valid && iso9660_is_dir_entry(&g_root);

    if (g_iso9660_info.ready) {
        strcpy(g_iso9660_info.status, "iso9660: ready");
    } else {
        strcpy(g_iso9660_info.status, "iso9660: init failed");
    }

    return g_iso9660_info.ready;
}

uint16_t iso9660_root_entry_count(void)
{
    char buffer[4096];
    uint16_t count = 0;

    if (!iso9660_list_root(buffer, sizeof(buffer))) {
        return 0;
    }
    for (uint32_t i = 0; buffer[i] != '\0'; i++) {
        if (buffer[i] == '\n') {
            count++;
        }
    }
    return count;
}

bool iso9660_exists(const char *path)
{
    iso9660_entry_t entry;
    return iso9660_resolve_path(path, &entry);
}

bool iso9660_is_dir(const char *path)
{
    iso9660_entry_t entry;
    return iso9660_resolve_path(path, &entry) && iso9660_is_dir_entry(&entry);
}

int32_t iso9660_file_size(const char *path)
{
    iso9660_entry_t entry;

    if (!iso9660_resolve_path(path, &entry) || iso9660_is_dir_entry(&entry)) {
        return -1;
    }
    return (int32_t) entry.size;
}

int32_t iso9660_read_file_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size)
{
    iso9660_entry_t entry;
    uint8_t *dst = (uint8_t *) buffer;
    uint32_t bytes_left;
    uint32_t bytes_read = 0;

    if (buffer == NULL || !iso9660_resolve_path(path, &entry) || iso9660_is_dir_entry(&entry)) {
        return -1;
    }
    if (offset >= entry.size || buffer_size == 0) {
        return 0;
    }

    bytes_left = entry.size - offset;
    if (bytes_left > buffer_size) {
        bytes_left = buffer_size;
    }

    while (bytes_left > 0) {
        uint32_t block_index = offset / ISO9660_BLOCK_SIZE;
        uint32_t block_offset = offset % ISO9660_BLOCK_SIZE;
        uint32_t chunk = ISO9660_BLOCK_SIZE - block_offset;

        if (block_offset == 0 && bytes_left >= ISO9660_BLOCK_SIZE && cdrom_is_ready()) {
            uint32_t block_count = bytes_left / ISO9660_BLOCK_SIZE;
            uint32_t direct_bytes = block_count * ISO9660_BLOCK_SIZE;

            if (cdrom_read_sectors(entry.extent + block_index, block_count, dst)) {
                dst += direct_bytes;
                offset += direct_bytes;
                bytes_read += direct_bytes;
                bytes_left -= direct_bytes;
                continue;
            }
        }
        if (chunk > bytes_left) {
            chunk = bytes_left;
        }
        iso9660_read_block(entry.extent + block_index, g_iso9660_block);
        memcpy(dst, g_iso9660_block + block_offset, chunk);
        dst += chunk;
        offset += chunk;
        bytes_read += chunk;
        bytes_left -= chunk;
    }
    return (int32_t) bytes_read;
}

int32_t iso9660_read_file(const char *path, void *buffer, uint32_t buffer_size)
{
    iso9660_entry_t entry;

    if (!iso9660_resolve_path(path, &entry) || iso9660_is_dir_entry(&entry)) {
        return -1;
    }
    if (entry.size > buffer_size) {
        return -2;
    }
    return iso9660_read_file_at(path, 0, buffer, buffer_size);
}

int32_t iso9660_write_file(const char *path, const void *buffer, uint32_t size)
{
    (void) path;
    (void) buffer;
    (void) size;
    return -1;
}

bool iso9660_delete(const char *path)
{
    (void) path;
    return false;
}

bool iso9660_mkdir(const char *path)
{
    (void) path;
    return false;
}

bool iso9660_rmdir(const char *path)
{
    (void) path;
    return false;
}

bool iso9660_list_dir(const char *path, char *buffer, uint32_t buffer_size)
{
    iso9660_entry_t dir;
    uint32_t used = 0;
    uint32_t offset = 0;

    if (buffer == NULL || buffer_size == 0 || !iso9660_resolve_path(path, &dir) || !iso9660_is_dir_entry(&dir)) {
        return false;
    }
    buffer[0] = '\0';

    while (offset < dir.size) {
        uint32_t block_index = offset / ISO9660_BLOCK_SIZE;
        uint32_t block_offset = offset % ISO9660_BLOCK_SIZE;
        const uint8_t *record;
        uint8_t record_len;
        uint8_t id_len;
        char name[64];
        uint32_t name_len;

        iso9660_read_block(dir.extent + block_index, g_iso9660_block);
        record = g_iso9660_block + block_offset;
        record_len = record[0];
        if (record_len == 0) {
            offset = (block_index + 1) * ISO9660_BLOCK_SIZE;
            continue;
        }
        if (block_offset + record_len > ISO9660_BLOCK_SIZE || record_len < 34) {
            return false;
        }
        id_len = record[32];
        if (id_len == 1 && (record[33] == 0 || record[33] == 1)) {
            offset += record_len;
            continue;
        }

        /* 跳过隐藏文件 */
        if ((record[25] & ISO9660_ATTR_HIDDEN) != 0) {
            offset += record_len;
            continue;
        }

        iso9660_decode_name(name, sizeof(name), record + 33, id_len);
        name_len = (uint32_t) strlen(name);
        if (used + name_len + 3 >= buffer_size) {
            return false;
        }
        strcpy(buffer + used, name);
        used += name_len;
        if ((record[25] & ISO9660_ATTR_DIR) != 0) {
            buffer[used++] = '/';
        }
        buffer[used++] = '\n';
        buffer[used] = '\0';
        offset += record_len;
    }
    return true;
}

bool iso9660_list_root(char *buffer, uint32_t buffer_size)
{
    return iso9660_list_dir("/", buffer, buffer_size);
}

const iso9660_info_t *iso9660_info(void)
{
    return &g_iso9660_info;
}

const char *iso9660_status(void)
{
    return g_iso9660_info.status;
}
