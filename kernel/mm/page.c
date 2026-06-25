#include "page.h"
#include "common.h"
#include "frame.h"
#include "buddy.h"
#include "string.h"

#define PAGE_MAX_COUNT      4096

static page_t g_page_array[PAGE_MAX_COUNT];
static page_info_t g_page_info;
static char g_page_status[64];

static page_t *page_find_by_phys(uint64_t phys)
{
    uint64_t offset;

    if (phys < g_page_info.base_phys) {
        return NULL;
    }
    offset = (phys - g_page_info.base_phys) >> PAGE_SHIFT;
    if (offset >= PAGE_MAX_COUNT) {
        return NULL;
    }
    return &g_page_array[offset];
}

void page_init(void)
{
    memset(g_page_array, 0, sizeof(g_page_array));
    memset(&g_page_info, 0, sizeof(g_page_info));

    g_page_info.pages = g_page_array;
    g_page_info.total_pages = PAGE_MAX_COUNT;
    g_page_info.free_pages = PAGE_MAX_COUNT;
    g_page_info.used_pages = 0;
    g_page_info.dirty_pages = 0;
    g_page_info.base_phys = 0x01000000ULL;
    g_page_info.ready = true;

    /* 初始化所有页 */
    for (uint32_t i = 0; i < PAGE_MAX_COUNT; i++) {
        g_page_array[i].flags = 0;
        g_page_array[i].refcount = 0;
        g_page_array[i].order = 0;
        g_page_array[i].physical = g_page_info.base_phys + ((uint64_t) i << PAGE_SHIFT);
        g_page_array[i].virtual = 0;
        g_page_array[i].next = NULL;
        g_page_array[i].prev = NULL;
    }

    strcpy(g_page_status, "page: ready");
}

page_t *page_alloc(uint32_t order)
{
    uint64_t phys;
    uint32_t page_count;
    page_t *page;

    if (!g_page_info.ready || order > 10) {
        strcpy(g_page_status, "page: bad order");
        return NULL;
    }

    page_count = 1u << order;
    if (page_count > g_page_info.free_pages) {
        strcpy(g_page_status, "page: out of memory");
        return NULL;
    }

    /* 使用伙伴系统分配 */
    phys = buddy_alloc(order);
    if (phys == 0) {
        strcpy(g_page_status, "page: allocation failed");
        return NULL;
    }

    page = page_find_by_phys(phys);
    if (page == NULL) {
        frame_free(phys, page_count);
        strcpy(g_page_status, "page: invalid phys");
        return NULL;
    }

    /* 标记所有页 */
    for (uint32_t i = 0; i < page_count; i++) {
        page[i].flags = PAGE_FLAG_USED;
        page[i].refcount = 1;
        page[i].order = order;
        if (i == 0) {
            page[i].flags |= PAGE_FLAG_COMPOUND;
        }
    }

    g_page_info.free_pages -= page_count;
    g_page_info.used_pages += page_count;

    strcpy(g_page_status, "page: allocated");
    return page;
}

page_t *page_alloc_zero(uint32_t order)
{
    page_t *page = page_alloc(order);
    if (page == NULL) {
        return NULL;
    }

    /* 清零页面 */
    uint32_t page_count = 1u << order;
    uint64_t phys = page_to_phys(page);
    memset((void *) phys, 0, page_count * PAGE_SIZE);

    page->flags |= PAGE_FLAG_ZERO;
    return page;
}

void page_free(page_t *page)
{
    uint32_t order;
    uint32_t page_count;
    uint64_t phys;

    if (page == NULL || !(page->flags & PAGE_FLAG_USED)) {
        return;
    }

    order = page->order;
    page_count = 1u << order;
    phys = page_to_phys(page);

    /* 清除所有页的标记 */
    for (uint32_t i = 0; i < page_count; i++) {
        page[i].flags = 0;
        page[i].refcount = 0;
        page[i].order = 0;
    }

    /* 释放到伙伴系统 */
    buddy_free(phys, order);

    g_page_info.free_pages += page_count;
    g_page_info.used_pages -= page_count;

    strcpy(g_page_status, "page: freed");
}

void page_get(page_t *page)
{
    if (page == NULL) {
        return;
    }
    page->refcount++;
}

bool page_put(page_t *page)
{
    if (page == NULL || page->refcount == 0) {
        return false;
    }

    page->refcount--;
    if (page->refcount == 0) {
        page_free(page);
        return true;
    }
    return false;
}

page_t *page_from_phys(uint64_t phys)
{
    return page_find_by_phys(phys & PAGE_MASK);
}

page_t *page_from_virt(uint64_t virt)
{
    /* 简化实现，假设虚拟地址和物理地址有固定偏移 */
    uint64_t phys = virt; /* 暂时假设恒等映射 */
    return page_from_phys(phys);
}

uint64_t page_to_phys(page_t *page)
{
    if (page == NULL) {
        return 0;
    }
    return page->physical;
}

uint64_t page_to_virt(page_t *page)
{
    if (page == NULL) {
        return 0;
    }
    if (page->virtual != 0) {
        return page->virtual;
    }
    return page->physical; /* 恒等映射 */
}

bool page_is_dirty(page_t *page)
{
    if (page == NULL) {
        return false;
    }
    return (page->flags & PAGE_FLAG_DIRTY) != 0;
}

void page_set_dirty(page_t *page)
{
    if (page == NULL) {
        return;
    }
    if (!(page->flags & PAGE_FLAG_DIRTY)) {
        page->flags |= PAGE_FLAG_DIRTY;
        g_page_info.dirty_pages++;
    }
}

void page_clear_dirty(page_t *page)
{
    if (page == NULL) {
        return;
    }
    if (page->flags & PAGE_FLAG_DIRTY) {
        page->flags &= ~PAGE_FLAG_DIRTY;
        if (g_page_info.dirty_pages > 0) {
            g_page_info.dirty_pages--;
        }
    }
}

bool page_is_locked(page_t *page)
{
    if (page == NULL) {
        return false;
    }
    return (page->flags & PAGE_FLAG_LOCKED) != 0;
}

void page_lock(page_t *page)
{
    if (page == NULL) {
        return;
    }
    page->flags |= PAGE_FLAG_LOCKED;
}

void page_unlock(page_t *page)
{
    if (page == NULL) {
        return;
    }
    page->flags &= ~PAGE_FLAG_LOCKED;
}

const page_info_t *page_info(void)
{
    return &g_page_info;
}

const char *page_status(void)
{
    return g_page_status;
}
