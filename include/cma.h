#ifndef _CMA_H_
#define _CMA_H_

#include "stdbool.h"
#include "stdint.h"

#define CMA_MAX_REGIONS    4
#define CMA_DEFAULT_SIZE   (16 * 1024 * 1024)  /* 16MB 默认大小 */

typedef struct {
    const char *name;
    uint64_t base_phys;
    uint64_t size;
    uint64_t free_size;
    uint64_t used_size;
    uint32_t *bitmap;
    uint32_t bitmap_size;
    uint32_t page_shift;
    bool ready;
} cma_region_t;

typedef struct {
    cma_region_t regions[CMA_MAX_REGIONS];
    uint32_t region_count;
    bool ready;
} cma_info_t;

void cma_init(void);
int cma_create_region(const char *name, uint64_t base_phys, uint64_t size);
uint64_t cma_alloc(uint32_t region_idx, uint64_t size, uint32_t align);
bool cma_free(uint32_t region_idx, uint64_t base_phys, uint64_t size);
uint64_t cma_free_size(uint32_t region_idx);
uint64_t cma_used_size(uint32_t region_idx);
uint64_t cma_total_size(uint32_t region_idx);
const cma_info_t *cma_info(void);
const char *cma_status(void);

#endif
