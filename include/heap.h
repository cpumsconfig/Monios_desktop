#ifndef _HEAP_H_
#define _HEAP_H_

#include "stdint.h"

typedef struct {
    uint64_t base;
    uint64_t size;
    uint64_t used;
    uint64_t free_bytes;
    uint64_t high_water_used;
    uint32_t alloc_count;
    uint32_t free_count;
} heap_info_t;

void heap_init(void);
const heap_info_t *heap_info(void);
const char *heap_status(void);

#endif
