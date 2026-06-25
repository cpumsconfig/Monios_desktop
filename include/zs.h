#ifndef _ZS_H_
#define _ZS_H_

#include "stdbool.h"
#include "stdint.h"

#define ZS_MAX_SIZE_CLASSES    16
#define ZS_MIN_SIZE            16
#define ZS_MAX_SIZE            4096
#define ZS_PAGE_SIZE           4096

typedef enum {
    ZS_COMP_NONE = 0,
    ZS_COMP_LZ4 = 1,
    ZS_COMP_DEFLATE = 2
} zs_compression_t;

typedef struct zs_pool {
    const char *name;
    zs_compression_t compression;
    uint32_t size_classes[ZS_MAX_SIZE_CLASSES];
    uint32_t class_count;
    uint64_t total_size;
    uint64_t used_size;
    uint64_t compressed_size;
    uint32_t obj_count;
    bool ready;
} zs_pool_t;

typedef struct {
    uint32_t pool_count;
    uint64_t total_memory;
    uint64_t total_compressed;
    uint32_t total_objects;
    bool ready;
} zs_info_t;

void zs_init(void);
zs_pool_t *zs_create_pool(const char *name, zs_compression_t compression);
void *zs_malloc(zs_pool_t *pool, uint32_t size);
void zs_free(zs_pool_t *pool, void *ptr);
void *zs_realloc(zs_pool_t *pool, void *ptr, uint32_t new_size);
uint32_t zs_malloc_usable_size(void *ptr);
uint64_t zs_pool_total_size(zs_pool_t *pool);
uint64_t zs_pool_used_size(zs_pool_t *pool);
uint64_t zs_pool_compressed_size(zs_pool_t *pool);
float zs_pool_compression_ratio(zs_pool_t *pool);
const zs_info_t *zs_info(void);
const char *zs_status(void);

#endif
