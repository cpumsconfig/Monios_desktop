#ifndef _HUGETLB_H_
#define _HUGETLB_H_

#include "stdbool.h"
#include "stdint.h"

#define HUGETLB_2MB_SHIFT    21
#define HUGETLB_2MB_SIZE     (1ULL << HUGETLB_2MB_SHIFT)
#define HUGETLB_2MB_ORDER    9    /* 2^9 = 512 普通页 */

#define HUGETLB_1GB_SHIFT    30
#define HUGETLB_1GB_SIZE     (1ULL << HUGETLB_1GB_SHIFT)
#define HUGETLB_1GB_ORDER    18   /* 2^18 = 262144 普通页 */

#define HUGETLB_MAX_POOLS    2

typedef enum {
    HUGETLB_2MB = 0,
    HUGETLB_1GB = 1
} hugetlb_size_t;

typedef struct {
    hugetlb_size_t size_type;
    uint64_t page_size;
    uint32_t page_order;
    uint32_t max_pages;
    uint32_t free_pages;
    uint32_t used_pages;
    uint64_t *pages;  /* 物理地址数组 */
    bool ready;
} hugetlb_pool_t;

typedef struct {
    hugetlb_pool_t pools[HUGETLB_MAX_POOLS];
    bool ready;
} hugetlb_info_t;

void hugetlb_init(void);
uint64_t hugetlb_alloc(hugetlb_size_t size_type);
bool hugetlb_free(uint64_t phys, hugetlb_size_t size_type);
uint32_t hugetlb_free_count(hugetlb_size_t size_type);
uint32_t hugetlb_used_count(hugetlb_size_t size_type);
uint64_t hugetlb_page_size(hugetlb_size_t size_type);
bool hugetlb_supported(hugetlb_size_t size_type);
const hugetlb_info_t *hugetlb_info(void);
const char *hugetlb_status(void);

#endif
