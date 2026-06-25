#include "lazyalloc.h"
#include "common.h"

#define LAZYALLOC_PAGE_SIZE 4096ULL
#define LAZYALLOC_MAX_REGIONS 8U

typedef struct {
    lazy_region_t public_region;
    uint64_t committed_mask;
} lazy_region_state_t;

static lazy_region_state_t g_regions[LAZYALLOC_MAX_REGIONS];
static uint32_t g_region_count;
static char g_lazyalloc_status[64];

static uint32_t lazyalloc_page_count(uint64_t size)
{
    return (uint32_t) ((size + LAZYALLOC_PAGE_SIZE - 1u) / LAZYALLOC_PAGE_SIZE);
}

void lazyalloc_init(void)
{
    memset(g_regions, 0, sizeof(g_regions));
    g_region_count = 0;
    strcpy(g_lazyalloc_status, "lazyalloc: ready");
    lazyalloc_add(0x043C0000ULL, 0x00030000ULL);
}

int32_t lazyalloc_add(uint64_t base, uint64_t size)
{
    lazy_region_state_t *region;
    uint32_t pages;

    if (g_region_count >= LAZYALLOC_MAX_REGIONS || size == 0) {
        strcpy(g_lazyalloc_status, "lazyalloc: table full");
        return -1;
    }
    pages = lazyalloc_page_count(size);
    if (pages > 64U) {
        strcpy(g_lazyalloc_status, "lazyalloc: region too large");
        return -1;
    }
    region = &g_regions[g_region_count];
    memset(region, 0, sizeof(*region));
    region->public_region.used = true;
    region->public_region.id = g_region_count;
    region->public_region.base = base;
    region->public_region.size = size;
    region->public_region.total_pages = pages;
    g_region_count++;
    strcpy(g_lazyalloc_status, "lazyalloc: region added");
    return (int32_t) region->public_region.id;
}

bool lazyalloc_touch(uint64_t address)
{
    for (uint32_t index = 0; index < g_region_count; index++) {
        lazy_region_state_t *region = &g_regions[index];

        if (address >= region->public_region.base && address < region->public_region.base + region->public_region.size) {
            uint32_t page = (uint32_t) ((address - region->public_region.base) / LAZYALLOC_PAGE_SIZE);
            uint64_t mask = 1ULL << page;

            if ((region->committed_mask & mask) == 0) {
                region->committed_mask |= mask;
                region->public_region.committed_pages++;
            }
            strcpy(g_lazyalloc_status, "lazyalloc: page committed");
            return true;
        }
    }
    strcpy(g_lazyalloc_status, "lazyalloc: region miss");
    return false;
}

uint32_t lazyalloc_count(void)
{
    return g_region_count;
}

bool lazyalloc_snapshot(uint32_t index, lazy_region_t *out)
{
    if (index >= g_region_count || out == NULL) {
        return false;
    }
    *out = g_regions[index].public_region;
    return true;
}

const char *lazyalloc_status(void)
{
    return g_lazyalloc_status;
}
