#include "common.h"
#include "fs_cache.h"

/*
 * 文件系统缓存实现
 *  - 读：LRU 页面缓存
 *  - 写：write-back（dirty 槽延迟刷盘）
 *
 * 注意：槽存储为静态 BSS 数组，大小固定为 FS_CACHE_DEFAULT_SLOTS。
 * FS_CACHE_CTL_SET_SLOTS 会把请求的槽位数钳位到静态池大小。
 */

#define FS_CACHE_PATH_MAX 512U

typedef struct {
    bool valid;
    bool dirty;
    fs_cache_flusher_t flusher;
    char path[FS_CACHE_PATH_MAX];
    uint32_t block_start;
    uint32_t data_size;
    uint8_t data[FS_CACHE_BLOCK_SIZE];
    uint32_t age;
} fs_cache_slot_t;

static fs_cache_slot_t g_slots[FS_CACHE_DEFAULT_SLOTS];
static fs_cache_info_t g_info;
static uint32_t g_age;
static fs_cache_flusher_t g_flusher;

static bool fs_cache_path_equal(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    return strcmp(a, b) == 0;
}

static bool fs_cache_path_is_cacheable(const char *path)
{
    uint32_t i = 0;

    if (path == NULL) {
        return false;
    }
    while (path[i] != '\0') {
        if (i + 1 >= FS_CACHE_PATH_MAX) {
            return false;
        }
        i++;
    }
    return true;
}

static bool fs_cache_copy_path(char *dst, uint32_t size, const char *src)
{
    uint32_t i = 0;

    if (dst == NULL || src == NULL || size == 0) {
        return false;
    }
    while (src[i] != '\0' && i + 1 < size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return src[i] == '\0';
}

static uint32_t fs_cache_active_slots(void)
{
    uint32_t n = g_info.configured_slots;

    if (n == 0) {
        n = FS_CACHE_DEFAULT_SLOTS;
    }
    if (n > FS_CACHE_DEFAULT_SLOTS) {
        n = FS_CACHE_DEFAULT_SLOTS;
    }
    if (n < 16U) {
        n = 16U;
    }
    return n;
}

void fs_cache_init(void)
{
    memset(g_slots, 0, sizeof(g_slots));
    memset(&g_info, 0, sizeof(g_info));
    g_info.enabled = true;
    g_info.slots = FS_CACHE_DEFAULT_SLOTS;
    g_info.configured_slots = FS_CACHE_DEFAULT_SLOTS;
    g_info.block_size = FS_CACHE_BLOCK_SIZE;
    strcpy(g_info.status, "fscache: ready");
    g_age = 1;
    g_flusher = NULL;
}

void fs_cache_set_flusher(fs_cache_flusher_t flusher)
{
    g_flusher = flusher;
}

static fs_cache_slot_t *fs_cache_find(const char *path, uint32_t block_start)
{
    uint32_t n = fs_cache_active_slots();

    for (uint32_t i = 0; i < n; i++) {
        if (g_slots[i].valid &&
            g_slots[i].block_start == block_start &&
            fs_cache_path_equal(g_slots[i].path, path)) {
            return &g_slots[i];
        }
    }
    return NULL;
}

/* 把 dirty 槽写回（如果可能）。返回 true 表示已写回（或无需写回）。 */
static bool fs_cache_flush_slot(fs_cache_slot_t *slot, fs_cache_flusher_t flusher)
{
    int32_t done;

    if (flusher == NULL) {
        flusher = slot->flusher != NULL ? slot->flusher : g_flusher;
    }
    if (flusher == NULL) {
        return false;
    }
    done = flusher(slot->path, slot->block_start, slot->data, slot->data_size);
    if (done < 0 || (uint32_t) done != slot->data_size) {
        return false;
    }
    slot->dirty = false;
    g_info.flushes++;
    g_info.dirty_count = (g_info.dirty_count > 0) ? g_info.dirty_count - 1U : 0U;
    return true;
}

static fs_cache_slot_t *fs_cache_choose_slot(void)
{
    uint32_t oldest = 0;
    uint32_t n = fs_cache_active_slots();

    for (uint32_t i = 0; i < n; i++) {
        if (!g_slots[i].valid) {
            return &g_slots[i];
        }
        if (g_slots[i].age < g_slots[oldest].age) {
            oldest = i;
        }
    }
    /* 全满：淘汰 LRU。若最旧槽是 dirty，先尝试写回再复用。 */
    if (g_slots[oldest].dirty) {
        if (!fs_cache_flush_slot(&g_slots[oldest], NULL)) return NULL;
    }
    g_info.evictions++;
    return &g_slots[oldest];
}

static bool fs_cache_copy_from_slot(fs_cache_slot_t *slot, uint32_t offset, void *buffer, uint32_t size)
{
    uint32_t in_block;

    if (slot == NULL || buffer == NULL || offset < slot->block_start) {
        return false;
    }
    in_block = offset - slot->block_start;
    if (in_block >= slot->data_size || size > slot->data_size - in_block) {
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
    if (offset > 0xFFFFFFFFu - (size - 1u)) {
        return -1;
    }
    if (!g_info.enabled || !fs_cache_path_is_cacheable(path)) {
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
            uint32_t load_size = FS_CACHE_BLOCK_SIZE;
            int32_t loaded;

            g_info.misses++;
            slot = fs_cache_choose_slot();
            if (slot == NULL) return copied ? (int32_t) copied : -1;
            /* choose_slot 可能复用了一个 dirty 槽（已写回），直接清空 */
            memset(slot, 0, sizeof(*slot));
            if (block_start > 0xFFFFFFFFu - (load_size - 1u)) {
                load_size = 0xFFFFFFFFu - block_start + 1u;
            }
            loaded = loader(path, block_start, slot->data, load_size);
            if (loaded > (int32_t)load_size) return -1;
            if (loaded <= 0) {
                strcpy(g_info.status, "fscache: backend miss");
                return copied > 0 ? (int32_t) copied : loaded;
            }
            slot->valid = true;
            slot->block_start = block_start;
            slot->data_size = (uint32_t) loaded;
            slot->age = ++g_age;
            slot->dirty = false;
            if (!fs_cache_copy_path(slot->path, sizeof(slot->path), path)) {
                slot->valid = false;
                return copied > 0 ? (int32_t) copied : loader(path, current, (uint8_t *) buffer + copied, want);
            }
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

int32_t fs_cache_write_at(const char *path, uint32_t offset, const void *buffer, uint32_t size, fs_cache_flusher_t flusher)
{
    uint32_t copied = 0;

    if (size == 0) {
        return 0;
    }
    if (path == NULL || buffer == NULL) {
        return -1;
    }
    if (offset > 0xFFFFFFFFu - (size - 1u)) {
        return -1;
    }
    if (flusher == NULL) flusher = g_flusher;
    if (flusher == NULL) return -1;
    if (!g_info.enabled || !fs_cache_path_is_cacheable(path)) {
        /* 缓存禁用：直接走 flusher */
        if (flusher == NULL) {
            return -1;
        }
        return flusher(path, offset, buffer, size);
    }

    while (copied < size) {
        uint32_t current = offset + copied;
        uint32_t block_start = current & ~(FS_CACHE_BLOCK_SIZE - 1u);
        uint32_t block_offset = current - block_start;
        uint32_t want = FS_CACHE_BLOCK_SIZE - block_offset;
        fs_cache_slot_t *slot;
        uint32_t room;

        if (want > size - copied) {
            want = size - copied;
        }

        slot = fs_cache_find(path, block_start);
        /* No read callback is available here.  Writing an uncached partial
         * block as zero-filled data would destroy its untouched bytes.  Use
         * the backend's offset write, after draining any cached older version. */
        if (block_offset != 0 || want != FS_CACHE_BLOCK_SIZE) {
            int32_t done;
            if (slot != NULL && slot->dirty && !fs_cache_flush_slot(slot, NULL))
                return copied ? (int32_t)copied : -1;
            if (slot != NULL) slot->valid = false;
            done = flusher(path, current, (const uint8_t *)buffer + copied, want);
            if (done < 0 || (uint32_t)done > want) return copied ? (int32_t)copied : -1;
            copied += (uint32_t)done;
            if ((uint32_t)done != want) return (int32_t)copied;
            continue;
        }
        if (slot != NULL && slot->dirty && slot->flusher != flusher &&
            !fs_cache_flush_slot(slot, NULL)) return copied ? (int32_t)copied : -1;
        if (slot == NULL) {
            /* 写分配：新建槽。如果该块超出已有文件大小，需要先从磁盘读回填剩余部分，
             * 否则只写一部分会读到旧数据。简化处理：槽大小直接取整个块。 */
            slot = fs_cache_choose_slot();
            if (slot == NULL) return copied ? (int32_t) copied : -1;
            memset(slot, 0, sizeof(*slot));
            slot->valid = true;
            slot->block_start = block_start;
            slot->data_size = FS_CACHE_BLOCK_SIZE;
            slot->age = ++g_age;
            if (!fs_cache_copy_path(slot->path, sizeof(slot->path), path)) {
                slot->valid = false;
                return (int32_t) copied;
            }
            /* 写时缺失且没有 loader 可读：零填充该块（后续 flusher 负责整块写回） */
            g_info.fills++;
        }

        room = FS_CACHE_BLOCK_SIZE - block_offset;
        if (want > room) {
            want = room;
        }
        memcpy(slot->data + block_offset, (const uint8_t *) buffer + copied, want);
        slot->flusher = flusher;
        if (block_offset + want > slot->data_size) {
            slot->data_size = block_offset + want;
        }
        if (!slot->dirty) {
            slot->dirty = true;
            g_info.dirty_count++;
        }
        slot->age = ++g_age;
        g_info.dirty_writes++;
        copied += want;
    }

    (void) flusher;  /* flusher 仅在 flush 时使用 */
    strcpy(g_info.status, "fscache: write-back");
    return (int32_t) copied;
}

uint32_t fs_cache_flush(void)
{
    uint32_t n = fs_cache_active_slots();
    uint32_t flushed = 0;

    for (uint32_t i = 0; i < n; i++) {
        if (g_slots[i].valid && g_slots[i].dirty) {
            if (fs_cache_flush_slot(&g_slots[i], NULL)) {
                flushed++;
            }
        }
    }
    if (flushed > 0) {
        strcpy(g_info.status, "fscache: flushed");
    }
    return flushed;
}

void fs_cache_invalidate_path(const char *path)
{
    uint32_t n = fs_cache_active_slots();

    for (uint32_t i = 0; i < n; i++) {
        if (g_slots[i].valid && (path == NULL || fs_cache_path_equal(g_slots[i].path, path))) {
            if (g_slots[i].dirty && !fs_cache_flush_slot(&g_slots[i], NULL)) continue;
            g_slots[i].valid = false;
            g_slots[i].dirty = false;
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
    g_info.slots = fs_cache_active_slots();
    return &g_info;
}

const char *fs_cache_status(void)
{
    return g_info.status;
}

uint64_t fs_cache_ctl(uint64_t cmd, uint64_t arg1, uint64_t arg2)
{
    (void) arg2;

    switch (cmd) {
    case FS_CACHE_CTL_STATUS: {
        fs_cache_info_t *out = (fs_cache_info_t *) arg1;

        g_info.slots = fs_cache_active_slots();
        if (out != NULL) {
            *out = g_info;
        }
        return 0;
    }
    case FS_CACHE_CTL_FLUSH:
        return (uint64_t) fs_cache_flush();
    case FS_CACHE_CTL_ENABLE:
        if (arg1 == 0) {
            fs_cache_flush();
            if (g_info.dirty_count != 0) return (uint64_t)-1;
            fs_cache_invalidate_all();
        }
        g_info.enabled = (arg1 != 0U);
        strcpy(g_info.status, g_info.enabled ? "fscache: enabled" : "fscache: disabled");
        return 0;
    case FS_CACHE_CTL_SET_SLOTS: {
        uint32_t want = (uint32_t) arg1;

        if (want < 16U) {
            want = 16U;
        }
        if (want > FS_CACHE_DEFAULT_SLOTS) {
            want = FS_CACHE_DEFAULT_SLOTS;
        }
        if (want < fs_cache_active_slots()) {
            fs_cache_flush();
            if (g_info.dirty_count != 0) return (uint64_t)-1;
            fs_cache_invalidate_all();
        }
        g_info.configured_slots = want;
        g_info.slots = want;
        return (uint64_t) want;
    }
    case FS_CACHE_CTL_DROP:
        fs_cache_flush();
        if (g_info.dirty_count != 0) return (uint64_t)-1;
        fs_cache_invalidate_all();
        return 0;
    default:
        return (uint64_t) -1;
    }
}

