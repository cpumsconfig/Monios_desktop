#include "cma.h"
#include "common.h"
#include "bitmap.h"
#include "string.h"
#include "frame.h"

#define CMA_PAGE_SHIFT    12  /* 4KB 页 */
#define CMA_PAGE_SIZE     (1 << CMA_PAGE_SHIFT)

static cma_info_t g_cma_info;
static char g_cma_status[64];
static uint32_t g_cma_bitmap_storage[CMA_MAX_REGIONS][1024];

static cma_region_t *cma_get_region(uint32_t idx)
{
    if (idx >= g_cma_info.region_count) {
        return NULL;
    }
    return &g_cma_info.regions[idx];
}

static uint32_t cma_pages(uint64_t size)
{
    return (uint32_t) ((size + CMA_PAGE_SIZE - 1) >> CMA_PAGE_SHIFT);
}

int cma_create_region(const char *name, uint64_t base_phys, uint64_t size)
{
    cma_region_t *region;
    uint32_t page_count;
    uint32_t bitmap_words;

    if (g_cma_info.region_count >= CMA_MAX_REGIONS) {
        strcpy(g_cma_status, "cma: too many regions");
        return -1;
    }

    if (size == 0 || (size & (CMA_PAGE_SIZE - 1)) != 0) {
        strcpy(g_cma_status, "cma: invalid size");
        return -1;
    }

    if ((base_phys & (CMA_PAGE_SIZE - 1)) != 0) {
        strcpy(g_cma_status, "cma: invalid base");
        return -1;
    }

    region = &g_cma_info.regions[g_cma_info.region_count];
    memset(region, 0, sizeof(*region));

    region->name = name;
    region->base_phys = base_phys;
    region->size = size;
    region->free_size = size;
    region->used_size = 0;
    region->page_shift = CMA_PAGE_SHIFT;

    page_count = cma_pages(size);
    bitmap_words = (page_count + BITMAP_WORD_BITS - 1) / BITMAP_WORD_BITS;

    if (bitmap_words > 1024) {
        strcpy(g_cma_status, "cma: region too large");
        return -1;
    }

    region->bitmap = g_cma_bitmap_storage[g_cma_info.region_count];
    region->bitmap_size = page_count;
    memset(region->bitmap, 0, bitmap_words * sizeof(uint32_t));

    region->ready = true;
    g_cma_info.region_count++;

    strcpy(g_cma_status, "cma: region created");
    return (int) (g_cma_info.region_count - 1);
}

void cma_init(void)
{
    memset(&g_cma_info, 0, sizeof(g_cma_info));

    /* 创建默认 CMA 区域 */
    /* 注意：实际实现中应该从预留的物理内存中分配 */
    /* 这里我们创建一个虚拟的区域用于演示 */

    g_cma_info.ready = true;
    strcpy(g_cma_status, "cma: ready");
}

uint64_t cma_alloc(uint32_t region_idx, uint64_t size, uint32_t align)
{
    cma_region_t *region = cma_get_region(region_idx);
    uint32_t page_count;
    uint32_t align_pages;
    int32_t start_page;
    uint64_t phys;

    if (region == NULL || !region->ready || size == 0) {
        return 0;
    }

    if (size > region->free_size) {
        strcpy(g_cma_status, "cma: out of memory");
        return 0;
    }

    page_count = cma_pages(size);
    align_pages = (align > CMA_PAGE_SIZE) ? cma_pages(align) : 1;

    /* 在位图中查找连续的空闲页 */
    start_page = bitmap_find_run_zero((bitmap_t *) region->bitmap, 0,
                                       page_count, align_pages);
    if (start_page < 0) {
        strcpy(g_cma_status, "cma: no contiguous memory");
        return 0;
    }

    /* 标记为已分配 */
    for (uint32_t i = 0; i < page_count; i++) {
        bitmap_set((bitmap_t *) region->bitmap, (uint32_t) (start_page + i));
    }

    phys = region->base_phys + ((uint64_t) start_page << CMA_PAGE_SHIFT);
    region->free_size -= size;
    region->used_size += size;

    strcpy(g_cma_status, "cma: allocated");
    return phys;
}

bool cma_free(uint32_t region_idx, uint64_t base_phys, uint64_t size)
{
    cma_region_t *region = cma_get_region(region_idx);
    uint32_t start_page;
    uint32_t page_count;

    if (region == NULL || !region->ready || size == 0) {
        return false;
    }

    if (base_phys < region->base_phys ||
        base_phys + size > region->base_phys + region->size) {
        strcpy(g_cma_status, "cma: invalid address");
        return false;
    }

    start_page = (uint32_t) ((base_phys - region->base_phys) >> CMA_PAGE_SHIFT);
    page_count = cma_pages(size);

    /* 标记为空闲 */
    for (uint32_t i = 0; i < page_count; i++) {
        bitmap_clear((bitmap_t *) region->bitmap, start_page + i);
    }

    region->free_size += size;
    region->used_size -= size;

    strcpy(g_cma_status, "cma: freed");
    return true;
}

uint64_t cma_free_size(uint32_t region_idx)
{
    cma_region_t *region = cma_get_region(region_idx);
    if (region == NULL) {
        return 0;
    }
    return region->free_size;
}

uint64_t cma_used_size(uint32_t region_idx)
{
    cma_region_t *region = cma_get_region(region_idx);
    if (region == NULL) {
        return 0;
    }
    return region->used_size;
}

uint64_t cma_total_size(uint32_t region_idx)
{
    cma_region_t *region = cma_get_region(region_idx);
    if (region == NULL) {
        return 0;
    }
    return region->size;
}

const cma_info_t *cma_info(void)
{
    return &g_cma_info;
}

const char *cma_status(void)
{
    return g_cma_status;
}
