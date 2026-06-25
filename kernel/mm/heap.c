#include "heap.h"
#include "common.h"
#include "memory.h"

static heap_info_t g_heap_info;
static char g_heap_status[64];

static void heap_refresh(void)
{
    g_heap_info.base = memory_heap_base();
    g_heap_info.size = memory_heap_size();
    g_heap_info.used = memory_total_used();
    g_heap_info.free_bytes = memory_total_free();
    g_heap_info.high_water_used = memory_high_water_used();
    g_heap_info.alloc_count = memory_alloc_count();
    g_heap_info.free_count = memory_free_count();
}

void heap_init(void)
{
    memset(&g_heap_info, 0, sizeof(g_heap_info));
    strcpy(g_heap_status, "heap: online");
    heap_refresh();
}

const heap_info_t *heap_info(void)
{
    heap_refresh();
    return &g_heap_info;
}

const char *heap_status(void)
{
    heap_refresh();
    return g_heap_status;
}
