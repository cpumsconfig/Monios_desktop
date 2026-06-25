#ifndef _PAGE_H_
#define _PAGE_H_

#include "stdbool.h"
#include "stdint.h"

#define PAGE_SIZE           4096
#define PAGE_SHIFT          12
#define PAGE_MASK           (~(PAGE_SIZE - 1))

#define PAGE_ORDER_2MB      9    /* 2^9 = 512 页 = 2MB */
#define PAGE_ORDER_1GB      18   /* 2^18 = 262144 页 = 1GB */

/* 页标志 */
#define PAGE_FLAG_USED      (1 << 0)
#define PAGE_FLAG_DIRTY     (1 << 1)
#define PAGE_FLAG_LOCKED    (1 << 2)
#define PAGE_FLAG_RESERVED  (1 << 3)
#define PAGE_FLAG_HUGE      (1 << 4)
#define PAGE_FLAG_COMPOUND  (1 << 5)
#define PAGE_FLAG_ZERO      (1 << 6)
#define PAGE_FLAG_HIGHMEM   (1 << 7)

typedef struct page {
    uint64_t flags;
    uint32_t refcount;
    uint32_t order;
    uint64_t virtual;
    uint64_t physical;
    struct page *next;
    struct page *prev;
} page_t;

typedef struct {
    page_t *pages;
    uint32_t total_pages;
    uint32_t free_pages;
    uint32_t used_pages;
    uint32_t dirty_pages;
    uint64_t base_phys;
    bool ready;
} page_info_t;

void page_init(void);
page_t *page_alloc(uint32_t order);
page_t *page_alloc_zero(uint32_t order);
void page_free(page_t *page);
void page_get(page_t *page);
bool page_put(page_t *page);
page_t *page_from_phys(uint64_t phys);
page_t *page_from_virt(uint64_t virt);
uint64_t page_to_phys(page_t *page);
uint64_t page_to_virt(page_t *page);
bool page_is_dirty(page_t *page);
void page_set_dirty(page_t *page);
void page_clear_dirty(page_t *page);
bool page_is_locked(page_t *page);
void page_lock(page_t *page);
void page_unlock(page_t *page);
const page_info_t *page_info(void);
const char *page_status(void);

#endif
