#ifndef _BITMAP_H_
#define _BITMAP_H_

#include "stdbool.h"
#include "stdint.h"

#define BITMAP_WORD_BITS 32U

typedef struct {
    uint32_t *words;
    uint32_t bit_count;
} bitmap_t;

typedef struct {
    uint32_t set_ops;
    uint32_t clear_ops;
    uint32_t test_ops;
    uint32_t alloc_ops;
    uint32_t free_ops;
} bitmap_stats_t;

void bitmap_init(void);
void bitmap_bind(bitmap_t *bitmap, uint32_t *storage, uint32_t bit_count);
void bitmap_zero(bitmap_t *bitmap);
bool bitmap_set(bitmap_t *bitmap, uint32_t index);
bool bitmap_clear(bitmap_t *bitmap, uint32_t index);
bool bitmap_test(const bitmap_t *bitmap, uint32_t index);
int32_t bitmap_find_first_zero(const bitmap_t *bitmap, uint32_t start_index);
int32_t bitmap_find_run_zero(const bitmap_t *bitmap, uint32_t start_index, uint32_t run_length, uint32_t align_bits);
int32_t bitmap_allocate_first(bitmap_t *bitmap);
bool bitmap_release(bitmap_t *bitmap, uint32_t index);
uint32_t bitmap_count_set(const bitmap_t *bitmap);
const bitmap_stats_t *bitmap_stats(void);
const char *bitmap_status(void);

#endif
