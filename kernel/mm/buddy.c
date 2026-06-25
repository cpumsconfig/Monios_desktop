#include "buddy.h"
#include "common.h"
#include "frame.h"

static buddy_info_t g_buddy_info;
static char g_buddy_status[64];

void buddy_init(void)
{
    memset(&g_buddy_info, 0, sizeof(g_buddy_info));
    g_buddy_info.ready = true;
    g_buddy_info.max_order = BUDDY_MAX_ORDER;
    strcpy(g_buddy_status, "buddy: ready");
}

uint64_t buddy_alloc(uint32_t order)
{
    uint32_t frames;
    uint64_t base;

    if (order > BUDDY_MAX_ORDER) {
        strcpy(g_buddy_status, "buddy: bad order");
        return 0;
    }
    frames = 1u << order;
    base = frame_alloc_aligned(frames, frames);
    if (base == 0) {
        g_buddy_info.failed_allocs++;
        strcpy(g_buddy_status, "buddy: allocation failed");
        return 0;
    }
    g_buddy_info.alloc_count++;
    g_buddy_info.split_count += order;
    g_buddy_info.last_base = base;
    g_buddy_info.last_order = order;
    strcpy(g_buddy_status, "buddy: allocated");
    return base;
}

bool buddy_free(uint64_t base, uint32_t order)
{
    if (order > BUDDY_MAX_ORDER || !frame_free(base, 1u << order)) {
        strcpy(g_buddy_status, "buddy: free failed");
        return false;
    }
    g_buddy_info.free_count++;
    g_buddy_info.last_base = base;
    g_buddy_info.last_order = order;
    strcpy(g_buddy_status, "buddy: freed");
    return true;
}

const buddy_info_t *buddy_info(void)
{
    return &g_buddy_info;
}

const char *buddy_status(void)
{
    return g_buddy_status;
}
