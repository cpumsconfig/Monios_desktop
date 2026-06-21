#include "pool.h"
#include "common.h"

static pool_stats_t g_pool_stats;
static char g_pool_status[64];

static void pool_copy_name(char *dst, uint32_t size, const char *src)
{
    uint32_t index = 0;

    if (size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    while (src[index] != '\0' && index + 1 < size) {
        dst[index] = src[index];
        index++;
    }
    dst[index] = '\0';
}

void pool_system_init(void)
{
    memset(&g_pool_stats, 0, sizeof(g_pool_stats));
    strcpy(g_pool_status, "pool: ready");
}

void pool_init(pool_t *pool, const char *name, uint32_t *storage, uint32_t capacity)
{
    if (pool == NULL) {
        return;
    }
    memset(pool, 0, sizeof(*pool));
    pool_copy_name(pool->name, sizeof(pool->name), name);
    bitmap_bind(&pool->map, storage, capacity);
    pool->capacity = capacity;
    g_pool_stats.pools++;
    g_pool_stats.slots_total += capacity;
}

int32_t pool_alloc_slot(pool_t *pool)
{
    int32_t slot;

    if (pool == NULL) {
        return -1;
    }
    slot = bitmap_allocate_first(&pool->map);
    if (slot < 0) {
        return -1;
    }
    pool->used++;
    pool->alloc_count++;
    g_pool_stats.alloc_ops++;
    g_pool_stats.slots_used++;
    return slot;
}

bool pool_free_slot(pool_t *pool, uint32_t slot)
{
    if (pool == NULL || !bitmap_release(&pool->map, slot)) {
        return false;
    }
    if (pool->used > 0) {
        pool->used--;
    }
    pool->free_count++;
    g_pool_stats.free_ops++;
    if (g_pool_stats.slots_used > 0) {
        g_pool_stats.slots_used--;
    }
    return true;
}

uint32_t pool_used(const pool_t *pool)
{
    return pool == NULL ? 0 : pool->used;
}

uint32_t pool_capacity(const pool_t *pool)
{
    return pool == NULL ? 0 : pool->capacity;
}

const pool_stats_t *pool_stats(void)
{
    return &g_pool_stats;
}

const char *pool_status(void)
{
    return g_pool_status;
}
