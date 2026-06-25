#include "hugetlb.h"
#include "common.h"
#include "page.h"
#include "buddy.h"
#include "string.h"
#include "cpu.h"

#define HUGETLB_2MB_MAX      32
#define HUGETLB_1GB_MAX      4

static hugetlb_info_t g_hugetlb_info;
static uint64_t g_hugetlb_2mb_pages[HUGETLB_2MB_MAX];
static uint64_t g_hugetlb_1gb_pages[HUGETLB_1GB_MAX];
static char g_hugetlb_status[64];

static hugetlb_pool_t *hugetlb_get_pool(hugetlb_size_t size_type)
{
    if (size_type >= HUGETLB_MAX_POOLS) {
        return NULL;
    }
    return &g_hugetlb_info.pools[size_type];
}

static bool hugetlb_init_pool(hugetlb_pool_t *pool, hugetlb_size_t size_type,
                              uint64_t *page_storage, uint32_t max_pages)
{
    const cpu_info_t *cpu = cpu_current_info();

    memset(pool, 0, sizeof(*pool));
    pool->size_type = size_type;
    pool->max_pages = max_pages;
    pool->free_pages = 0;
    pool->used_pages = 0;
    pool->pages = page_storage;
    pool->ready = false;

    switch (size_type) {
    case HUGETLB_2MB:
        pool->page_size = HUGETLB_2MB_SIZE;
        pool->page_order = HUGETLB_2MB_ORDER;
        break;
    case HUGETLB_1GB:
        pool->page_size = HUGETLB_1GB_SIZE;
        pool->page_order = HUGETLB_1GB_ORDER;
        /* 检查 CPU 是否支持 1GB 大页 */
        if (!cpu->has_gbpages) {
            strcpy(g_hugetlb_status, "hugetlb: 1GB pages not supported");
            return false;
        }
        break;
    default:
        return false;
    }

    /* 预分配一些大页 */
    for (uint32_t i = 0; i < max_pages; i++) {
        uint64_t phys = buddy_alloc(pool->page_order);
        if (phys == 0) {
            break;
        }
        pool->pages[i] = phys;
        pool->free_pages++;
    }

    if (pool->free_pages > 0) {
        pool->ready = true;
    }

    return pool->ready;
}

void hugetlb_init(void)
{
    memset(&g_hugetlb_info, 0, sizeof(g_hugetlb_info));

    /* 初始化 2MB 大页池 */
    hugetlb_init_pool(&g_hugetlb_info.pools[HUGETLB_2MB], HUGETLB_2MB,
                      g_hugetlb_2mb_pages, HUGETLB_2MB_MAX);

    /* 初始化 1GB 大页池 */
    hugetlb_init_pool(&g_hugetlb_info.pools[HUGETLB_1GB], HUGETLB_1GB,
                      g_hugetlb_1gb_pages, HUGETLB_1GB_MAX);

    g_hugetlb_info.ready = g_hugetlb_info.pools[HUGETLB_2MB].ready;

    if (g_hugetlb_info.ready) {
        strcpy(g_hugetlb_status, "hugetlb: ready");
    } else {
        strcpy(g_hugetlb_status, "hugetlb: init failed");
    }
}

uint64_t hugetlb_alloc(hugetlb_size_t size_type)
{
    hugetlb_pool_t *pool = hugetlb_get_pool(size_type);
    uint64_t phys;

    if (pool == NULL || !pool->ready || pool->free_pages == 0) {
        return 0;
    }

    /* 从池中分配 */
    for (uint32_t i = 0; i < pool->max_pages; i++) {
        if (pool->pages[i] != 0) {
            phys = pool->pages[i];
            pool->pages[i] = 0;
            pool->free_pages--;
            pool->used_pages++;
            strcpy(g_hugetlb_status, "hugetlb: allocated");
            return phys;
        }
    }

    /* 池中没有了，尝试直接从伙伴系统分配 */
    phys = buddy_alloc(pool->page_order);
    if (phys != 0) {
        pool->used_pages++;
        strcpy(g_hugetlb_status, "hugetlb: allocated (on-demand)");
        return phys;
    }

    strcpy(g_hugetlb_status, "hugetlb: allocation failed");
    return 0;
}

bool hugetlb_free(uint64_t phys, hugetlb_size_t size_type)
{
    hugetlb_pool_t *pool = hugetlb_get_pool(size_type);

    if (pool == NULL || phys == 0) {
        return false;
    }

    /* 尝试放回池中 */
    for (uint32_t i = 0; i < pool->max_pages; i++) {
        if (pool->pages[i] == 0) {
            pool->pages[i] = phys;
            pool->free_pages++;
            pool->used_pages--;
            strcpy(g_hugetlb_status, "hugetlb: freed to pool");
            return true;
        }
    }

    /* 池满了，直接释放到伙伴系统 */
    if (buddy_free(phys, pool->page_order)) {
        pool->used_pages--;
        strcpy(g_hugetlb_status, "hugetlb: freed to buddy");
        return true;
    }

    strcpy(g_hugetlb_status, "hugetlb: free failed");
    return false;
}

uint32_t hugetlb_free_count(hugetlb_size_t size_type)
{
    hugetlb_pool_t *pool = hugetlb_get_pool(size_type);
    if (pool == NULL) {
        return 0;
    }
    return pool->free_pages;
}

uint32_t hugetlb_used_count(hugetlb_size_t size_type)
{
    hugetlb_pool_t *pool = hugetlb_get_pool(size_type);
    if (pool == NULL) {
        return 0;
    }
    return pool->used_pages;
}

uint64_t hugetlb_page_size(hugetlb_size_t size_type)
{
    hugetlb_pool_t *pool = hugetlb_get_pool(size_type);
    if (pool == NULL) {
        return 0;
    }
    return pool->page_size;
}

bool hugetlb_supported(hugetlb_size_t size_type)
{
    const cpu_info_t *cpu = cpu_current_info();

    switch (size_type) {
    case HUGETLB_2MB:
        return true;  /* 2MB 大页总是支持 */
    case HUGETLB_1GB:
        return cpu->has_gbpages;
    default:
        return false;
    }
}

const hugetlb_info_t *hugetlb_info(void)
{
    return &g_hugetlb_info;
}

const char *hugetlb_status(void)
{
    return g_hugetlb_status;
}
