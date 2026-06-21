#ifndef _BUDDY_H_
#define _BUDDY_H_

#include "stdbool.h"
#include "stdint.h"

#define BUDDY_MAX_ORDER 12U

typedef struct {
    bool ready;
    uint32_t max_order;
    uint32_t alloc_count;
    uint32_t free_count;
    uint32_t split_count;
    uint32_t failed_allocs;
    uint64_t last_base;
    uint32_t last_order;
} buddy_info_t;

void buddy_init(void);
uint64_t buddy_alloc(uint32_t order);
bool buddy_free(uint64_t base, uint32_t order);
const buddy_info_t *buddy_info(void);
const char *buddy_status(void);

#endif
