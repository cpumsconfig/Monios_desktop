int printf(const char *, ...);
#include "../lib/fs_cache.c"
static int result;
static int32_t flush(const char *p, uint32_t off, const void *data, uint32_t size)
{ (void)p; (void)off; (void)data; (void)size; return result; }
static uint8_t disk[FS_CACHE_BLOCK_SIZE * 2], block[FS_CACHE_BLOCK_SIZE], readback[16];
static bool disk_failed;
static int32_t disk_write(const char *p, uint32_t off, const void *data, uint32_t size)
{
    (void)p;
    if (disk_failed || off > sizeof(disk) || size > sizeof(disk) - off) return -1;
    memcpy(disk + off, data, size); return (int32_t)size;
}
static int32_t disk_read(const char *p, uint32_t off, void *data, uint32_t size)
{
    (void)p;
    if (off >= sizeof(disk)) return 0;
    if (size > sizeof(disk) - off) size = sizeof(disk) - off;
    memcpy(data, disk + off, size); return (int32_t)size;
}
int main(void)
{
    fs_cache_init();
    fs_cache_set_flusher(flush);
    for (uint32_t i = 0; i < fs_cache_active_slots(); i++) {
        g_slots[i].valid = true;
        g_slots[i].dirty = true;
        g_slots[i].data_size = 4;
        g_slots[i].data[0] = 0xAB;
        g_slots[i].age = i;
    }
    g_info.dirty_count = fs_cache_active_slots();
    result = -1;
    if (fs_cache_choose_slot() != NULL || !g_slots[0].dirty) return 1;
    result = 2;
    if (fs_cache_choose_slot() != NULL || !g_slots[0].dirty) return 2;
    if (g_slots[0].data[0] != 0xAB || g_info.evictions != 0) return 3;
    result = 4;
    if (fs_cache_choose_slot() != &g_slots[0] || g_slots[0].dirty) return 4;
    fs_cache_init();
    memset(disk, 0xAA, sizeof(disk));
    memset(block, 0xBB, sizeof(block));
    if (fs_cache_write_at("file", 5, "xyz", 3, disk_write) != 3) return 5;
    if (disk[4] != 0xAA || disk[8] != 0xAA || memcmp(disk + 5, "xyz", 3)) return 6;
    if (fs_cache_read_at("file", 0, readback, sizeof(readback), disk_read) != sizeof(readback)) return 7;
    if (fs_cache_write_at("file", 5, "abc", 3, disk_write) != 3) return 8;
    if (fs_cache_read_at("file", 5, readback, 3, disk_read) != 3 || memcmp(readback, "abc", 3)) return 9;
    if (fs_cache_write_at("file", 0, block, sizeof(block), disk_write) != sizeof(block)) return 10;
    disk_failed = true;
    if (fs_cache_ctl(FS_CACHE_CTL_DROP, 0, 0) != (uint64_t)-1) return 11;
    if (fs_cache_ctl(FS_CACHE_CTL_ENABLE, 0, 0) != (uint64_t)-1 || !g_info.enabled) return 12;
    if (fs_cache_ctl(FS_CACHE_CTL_SET_SLOTS, 16, 0) != (uint64_t)-1 || g_info.configured_slots != FS_CACHE_DEFAULT_SLOTS) return 13;
    fs_cache_invalidate_all();
    if (g_info.dirty_count != 1 || fs_cache_find("file", 0) == NULL) return 14;
    disk_failed = false;
    if (fs_cache_flush() != 1 || memcmp(disk, block, sizeof(block))) return 15;
    if (fs_cache_ctl(FS_CACHE_CTL_ENABLE, 0, 0) != 0 || g_info.enabled) return 16;
    fs_cache_set_flusher(disk_write);
    if (fs_cache_write_at("file", 2, "ok", 2, NULL) != 2 || memcmp(disk + 2, "ok", 2)) return 17;
    printf("Cache writeback failure/short-write/retry: PASS\n");
    return 0;
}
