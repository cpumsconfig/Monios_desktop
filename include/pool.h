#ifndef _POOL_H_
#define _POOL_H_

#include "bitmap.h"

typedef struct {
    bitmap_t map;
    uint32_t capacity;
    uint32_t used;
    uint32_t alloc_count;
    uint32_t free_count;
    char name[16];
} pool_t;

typedef struct {
    uint32_t pools;
    uint32_t slots_total;
    uint32_t slots_used;
    uint32_t alloc_ops;
    uint32_t free_ops;
} pool_stats_t;

void pool_system_init(void);
void pool_init(pool_t *pool, const char *name, uint32_t *storage, uint32_t capacity);
int32_t pool_alloc_slot(pool_t *pool);
bool pool_free_slot(pool_t *pool, uint32_t slot);
uint32_t pool_used(const pool_t *pool);
uint32_t pool_capacity(const pool_t *pool);
const pool_stats_t *pool_stats(void);
const char *pool_status(void);

#endif
