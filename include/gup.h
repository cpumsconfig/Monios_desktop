#ifndef _GUP_H_
#define _GUP_H_

#include "stdbool.h"
#include "stdint.h"
#include "page.h"

#define GUP_MAX_PAGES    256

typedef enum {
    GUP_READ = 0,
    GUP_WRITE = 1,
    GUP_FAST = 2
} gup_flags_t;

typedef struct {
    uint64_t user_addr;
    uint32_t page_count;
    uint32_t pinned_count;
    page_t **pages;
    bool write;
    bool locked;
} gup_context_t;

void gup_init(void);
int gup_get_user_pages(uint64_t start, uint32_t nr_pages, gup_flags_t flags,
                       page_t **pages);
void gup_put_user_pages(page_t **pages, uint32_t nr_pages, bool dirty);
bool gup_pin_user_page(uint64_t user_addr, gup_flags_t flags, page_t **page);
void gup_unpin_user_page(page_t *page, bool dirty);
uint64_t gup_page_to_phys(page_t *page);
void *gup_kmap(page_t *page);
void gup_kunmap(page_t *page);
const char *gup_status(void);

#endif
