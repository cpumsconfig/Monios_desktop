#ifndef _LAZYALLOC_H_
#define _LAZYALLOC_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool used;
    uint32_t id;
    uint64_t base;
    uint64_t size;
    uint32_t total_pages;
    uint32_t committed_pages;
} lazy_region_t;

void lazyalloc_init(void);
int32_t lazyalloc_add(uint64_t base, uint64_t size);
bool lazyalloc_touch(uint64_t address);
uint32_t lazyalloc_count(void);
bool lazyalloc_snapshot(uint32_t index, lazy_region_t *out);
const char *lazyalloc_status(void);

#endif
