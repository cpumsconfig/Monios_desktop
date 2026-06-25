#include "gup.h"
#include "common.h"
#include "page.h"
#include "vma.h"
#include "string.h"

static char g_gup_status[64];
static page_t *g_gup_page_ptrs[GUP_MAX_PAGES];

void gup_init(void)
{
    memset(g_gup_page_ptrs, 0, sizeof(g_gup_page_ptrs));
    strcpy(g_gup_status, "gup: ready");
}

static bool gup_vaddr_valid(uint64_t vaddr)
{
    /* 简化实现：检查地址是否在用户空间范围内 */
    /* 实际实现中应该检查 VMA */
    return vaddr != 0;
}

static uint64_t gup_virt_to_phys(uint64_t vaddr)
{
    /* 简化实现：假设恒等映射 */
    /* 实际实现中应该遍历页表 */
    return vaddr;
}

int gup_get_user_pages(uint64_t start, uint32_t nr_pages, gup_flags_t flags,
                       page_t **pages)
{
    uint32_t pinned = 0;
    uint64_t vaddr;
    uint64_t phys;
    page_t *page;

    if (pages == NULL || nr_pages == 0 || nr_pages > GUP_MAX_PAGES) {
        strcpy(g_gup_status, "gup: invalid params");
        return -1;
    }

    if (!gup_vaddr_valid(start)) {
        strcpy(g_gup_status, "gup: invalid address");
        return -1;
    }

    for (uint32_t i = 0; i < nr_pages; i++) {
        vaddr = start + ((uint64_t) i << PAGE_SHIFT);
        phys = gup_virt_to_phys(vaddr);

        if (phys == 0) {
            /* 页错误，需要分配页面 */
            /* 简化实现：直接分配新页面 */
            page = page_alloc(0);
            if (page == NULL) {
                strcpy(g_gup_status, "gup: allocation failed");
                break;
            }
            /* 设置虚拟地址 */
            page->virtual = vaddr & PAGE_MASK;
        } else {
            /* 获取已存在的页面 */
            page = page_from_phys(phys);
            if (page == NULL) {
                strcpy(g_gup_status, "gup: page not found");
                break;
            }
            page_get(page);
        }

        /* 如果是写操作，设置脏页标志 */
        if (flags & GUP_WRITE) {
            page_set_dirty(page);
        }

        pages[i] = page;
        pinned++;
    }

    if (pinned == 0) {
        return -1;
    }

    strcpy(g_gup_status, "gup: pages pinned");
    return (int) pinned;
}

void gup_put_user_pages(page_t **pages, uint32_t nr_pages, bool dirty)
{
    if (pages == NULL || nr_pages == 0) {
        return;
    }

    for (uint32_t i = 0; i < nr_pages; i++) {
        if (pages[i] != NULL) {
            if (dirty) {
                page_set_dirty(pages[i]);
            }
            page_put(pages[i]);
        }
    }

    strcpy(g_gup_status, "gup: pages unpinned");
}

bool gup_pin_user_page(uint64_t user_addr, gup_flags_t flags, page_t **page)
{
    int ret;

    if (page == NULL) {
        return false;
    }

    ret = gup_get_user_pages(user_addr & PAGE_MASK, 1, flags, page);
    return ret > 0;
}

void gup_unpin_user_page(page_t *page, bool dirty)
{
    if (page == NULL) {
        return;
    }
    gup_put_user_pages(&page, 1, dirty);
}

uint64_t gup_page_to_phys(page_t *page)
{
    if (page == NULL) {
        return 0;
    }
    return page_to_phys(page);
}

void *gup_kmap(page_t *page)
{
    if (page == NULL) {
        return NULL;
    }
    /* 简化实现：直接返回物理地址作为内核虚拟地址 */
    /* 实际实现中需要映射到内核地址空间 */
    return (void *) page_to_phys(page);
}

void gup_kunmap(page_t *page)
{
    /* 简化实现：什么都不做 */
    (void) page;
}

const char *gup_status(void)
{
    return g_gup_status;
}
