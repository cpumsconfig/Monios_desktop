#include "common.h"
#include "fs_cache.h"

#define FS_CACHE_SLOTS 16U
#define FS_CACHE_BLOCK_SIZE 512U
#define FS_CACHE_PATH_MAX 96U

typedef struct {
    bool valid;
    char path[FS_CACHE_PATH_MAX];
    uint32_t block_start;
    uint32_t data_size;
    uint8_t data[FS_CACHE_BLOCK_SIZE];
    uint32_t age;
} fs_cache_slot_t;

static fs_cache_slot_t g_slots[FS_CACHE_SLOTS];
static fs_cache_info_t g_info;
static uint32_t g_age;

static bool fs_cache_path_equal(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    return strcmp(a, b) == 0;
}

static void fs_cache_copy_path(char *dst, uint32_t size, const char *src)
{
    uint32_t i = 0;

    if (size == 0) {
        return;
    }
    while (src != NULL && src[i] != '\0' && i + 1 < size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void fs_cache_init(void)
{
    memset(g_slots, 0, sizeof(g_slots));
    memset(&g_info, 0, sizeof(g_info));
    g_info.enabled = true;
    g_info.slots = FS_CACHE_SLOTS;
    g_info.block_size = FS_CACHE_BLOCK_SIZE;
    strcpy(g_info.status, "fscache: ready");
    g_age = 1;
}

static fs_cache_slot_t *fs_cache_find(const char *path, uint32_t block_start)
{
    for (uint32_t i = 0; i < FS_CACHE_SLOTS; i++) {
        if (g_slots[i].valid &&
            g_slots[i].block_start == block_start &&
            fs_cache_path_equal(g_slots[i].path, path)) {
            return &g_slots[i];
        }
    }
    return NULL;
}

static fs_cache_slot_t *fs_cache_choose_slot(void)
{
    uint32_t oldest = 0;

    for (uint32_t i = 0; i < FS_CACHE_SLOTS; i++) {
        if (!g_slots[i].valid) {
            return &g_slots[i];
        }
        if (g_slots[i].age < g_slots[oldest].age) {
            oldest = i;
        }
    }
    return &g_slots[oldest];
}

static bool fs_cache_copy_from_slot(fs_cache_slot_t *slot, uint32_t offset, void *buffer, uint32_t size)
{
    uint32_t in_block;

    if (slot == NULL || buffer == NULL || offset < slot->block_start) {
        return false;
    }
    in_block = offset - slot->block_start;
    if (in_block + size > slot->data_size) {
        return false;
    }
    memcpy(buffer, slot->data + in_block, size);
    slot->age = ++g_age;
    g_info.hits++;
    strcpy(g_info.status, "fscache: hit");
    return true;
}

int32_t fs_cache_read_at(const char *path, uint32_t offset, void *buffer, uint32_t size, fs_cache_loader_t loader)
{
    uint32_t copied = 0;

    if (size == 0) {
        return 0;
    }
    if (path == NULL || buffer == NULL || loader == NULL) {
        return -1;
    }
    if (!g_info.enabled) {
        return loader(path, offset, buffer, size);
    }

    while (copied < size) {
        uint32_t current = offset + copied;
        uint32_t block_start = current & ~(FS_CACHE_BLOCK_SIZE - 1u);
        uint32_t block_offset = current - block_start;
        uint32_t want = FS_CACHE_BLOCK_SIZE - block_offset;
        fs_cache_slot_t *slot;

        if (want > size - copied) {
            want = size - copied;
        }

        slot = fs_cache_find(path, block_start);
        if (slot == NULL) {
            int32_t loaded;

            g_info.misses++;
            slot = fs_cache_choose_slot();
            memset(slot, 0, sizeof(*slot));
            loaded = loader(path, block_start, slot->data, FS_CACHE_BLOCK_SIZE);
            if (loaded <= 0) {
                strcpy(g_info.status, "fscache: backend miss");
                return copied > 0 ? (int32_t) copied : loaded;
            }
            slot->valid = true;
            slot->block_start = block_start;
            slot->data_size = (uint32_t) loaded;
            slot->age = ++g_age;
            fs_cache_copy_path(slot->path, sizeof(slot->path), path);
            g_info.fills++;
        }

        if (block_offset >= slot->data_size) {
            strcpy(g_info.status, "fscache: eof");
            return (int32_t) copied;
        }
        if (block_offset + want > slot->data_size) {
            want = slot->data_size - block_offset;
        }
        if (!fs_cache_copy_from_slot(slot, current, (uint8_t *) buffer + copied, want)) {
            int32_t read_direct = loader(path, current, (uint8_t *) buffer + copied, want);

            if (read_direct <= 0) {
                return copied > 0 ? (int32_t) copied : read_direct;
            }
            copied += (uint32_t) read_direct;
            if ((uint32_t) read_direct < want) {
                return (int32_t) copied;
            }
            continue;
        }
        copied += want;
    }

    return (int32_t) copied;
}

void fs_cache_invalidate_path(const char *path)
{
    for (uint32_t i = 0; i < FS_CACHE_SLOTS; i++) {
        if (g_slots[i].valid && (path == NULL || fs_cache_path_equal(g_slots[i].path, path))) {
            g_slots[i].valid = false;
            g_info.invalidations++;
        }
    }
    strcpy(g_info.status, "fscache: invalidated");
}

void fs_cache_invalidate_all(void)
{
    fs_cache_invalidate_path(NULL);
}

const fs_cache_info_t *fs_cache_info(void)
{
    return &g_info;
}

const char *fs_cache_status(void)
{
    return g_info.status;
}
