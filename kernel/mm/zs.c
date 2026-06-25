#include "zs.h"
#include "common.h"
#include "page.h"
#include "memory.h"
#include "string.h"

#define ZS_MAX_POOLS    4
#define ZS_OBJ_MAGIC    0x5A534F42  /* "ZSOB" */

typedef struct {
    uint32_t magic;
    uint32_t size;
    uint32_t compressed_size;
    uint32_t class_idx;
    bool compressed;
    uint8_t data[];
} zs_object_t;

static zs_pool_t g_zs_pools[ZS_MAX_POOLS];
static zs_info_t g_zs_info;
static char g_zs_status[64];
static uint32_t g_zs_pool_count = 0;

static uint32_t zs_next_power_of_two(uint32_t size)
{
    uint32_t result = ZS_MIN_SIZE;
    while (result < size && result < ZS_MAX_SIZE) {
        result <<= 1;
    }
    return result;
}

static int32_t zs_find_size_class(zs_pool_t *pool, uint32_t size)
{
    for (uint32_t i = 0; i < pool->class_count; i++) {
        if (pool->size_classes[i] >= size) {
            return (int32_t) i;
        }
    }
    return -1;
}

static void zs_init_size_classes(zs_pool_t *pool)
{
    uint32_t size = ZS_MIN_SIZE;
    pool->class_count = 0;

    while (size <= ZS_MAX_SIZE && pool->class_count < ZS_MAX_SIZE_CLASSES) {
        pool->size_classes[pool->class_count] = size;
        pool->class_count++;
        size <<= 1;
    }
}

/* 简化的压缩函数（实际实现中应该使用 LZ4 等算法） */
static uint32_t zs_compress(const uint8_t *src, uint32_t src_len,
                            uint8_t *dst, uint32_t dst_len)
{
    /* 简化实现：直接复制，不压缩 */
    /* 实际实现中应该使用真正的压缩算法 */
    if (dst_len < src_len) {
        return 0;
    }
    memcpy(dst, src, src_len);
    return src_len;
}

/* 简化的解压函数 */
static uint32_t zs_decompress(const uint8_t *src, uint32_t src_len,
                              uint8_t *dst, uint32_t dst_len)
{
    /* 简化实现：直接复制，不解压 */
    if (dst_len < src_len) {
        return 0;
    }
    memcpy(dst, src, src_len);
    return src_len;
}

void zs_init(void)
{
    memset(g_zs_pools, 0, sizeof(g_zs_pools));
    memset(&g_zs_info, 0, sizeof(g_zs_info));

    g_zs_info.ready = true;
    strcpy(g_zs_status, "zs: ready");
}

zs_pool_t *zs_create_pool(const char *name, zs_compression_t compression)
{
    zs_pool_t *pool;

    if (g_zs_pool_count >= ZS_MAX_POOLS) {
        strcpy(g_zs_status, "zs: too many pools");
        return NULL;
    }

    pool = &g_zs_pools[g_zs_pool_count];
    memset(pool, 0, sizeof(*pool));

    pool->name = name;
    pool->compression = compression;
    pool->total_size = 0;
    pool->used_size = 0;
    pool->compressed_size = 0;
    pool->obj_count = 0;

    zs_init_size_classes(pool);

    pool->ready = true;
    g_zs_pool_count++;
    g_zs_info.pool_count = g_zs_pool_count;

    strcpy(g_zs_status, "zs: pool created");
    return pool;
}

void *zs_malloc(zs_pool_t *pool, uint32_t size)
{
    zs_object_t *obj;
    uint32_t alloc_size;
    int32_t class_idx;

    if (pool == NULL || !pool->ready || size == 0 || size > ZS_MAX_SIZE) {
        strcpy(g_zs_status, "zs: invalid params");
        return NULL;
    }

    class_idx = zs_find_size_class(pool, size);
    if (class_idx < 0) {
        strcpy(g_zs_status, "zs: size too large");
        return NULL;
    }

    alloc_size = pool->size_classes[class_idx];

    /* 分配对象（包括头部） */
    obj = (zs_object_t *) kmalloc(sizeof(zs_object_t) + alloc_size);
    if (obj == NULL) {
        strcpy(g_zs_status, "zs: allocation failed");
        return NULL;
    }

    memset(obj, 0, sizeof(zs_object_t) + alloc_size);
    obj->magic = ZS_OBJ_MAGIC;
    obj->size = size;
    obj->compressed_size = alloc_size;
    obj->class_idx = (uint32_t) class_idx;
    obj->compressed = false;

    pool->total_size += alloc_size;
    pool->used_size += size;
    pool->compressed_size += alloc_size;
    pool->obj_count++;

    g_zs_info.total_memory += alloc_size;
    g_zs_info.total_compressed += alloc_size;
    g_zs_info.total_objects++;

    strcpy(g_zs_status, "zs: allocated");
    return obj->data;
}

void zs_free(zs_pool_t *pool, void *ptr)
{
    zs_object_t *obj;

    if (pool == NULL || ptr == NULL) {
        return;
    }

    obj = (zs_object_t *) ((uint8_t *) ptr - sizeof(zs_object_t));
    if (obj->magic != ZS_OBJ_MAGIC) {
        strcpy(g_zs_status, "zs: invalid magic");
        return;
    }

    pool->total_size -= obj->compressed_size;
    pool->used_size -= obj->size;
    pool->compressed_size -= obj->compressed_size;
    pool->obj_count--;

    g_zs_info.total_memory -= obj->compressed_size;
    g_zs_info.total_compressed -= obj->compressed_size;
    g_zs_info.total_objects--;

    kfree(obj);

    strcpy(g_zs_status, "zs: freed");
}

void *zs_realloc(zs_pool_t *pool, void *ptr, uint32_t new_size)
{
    void *new_ptr;
    zs_object_t *old_obj;
    uint32_t copy_size;

    if (pool == NULL || new_size == 0) {
        if (ptr != NULL) {
            zs_free(pool, ptr);
        }
        return NULL;
    }

    if (ptr == NULL) {
        return zs_malloc(pool, new_size);
    }

    old_obj = (zs_object_t *) ((uint8_t *) ptr - sizeof(zs_object_t));
    if (old_obj->magic != ZS_OBJ_MAGIC) {
        return NULL;
    }

    /* 如果新大小在同一个 size class 中，直接返回 */
    int32_t old_class = zs_find_size_class(pool, old_obj->size);
    int32_t new_class = zs_find_size_class(pool, new_size);
    if (old_class == new_class && old_class >= 0) {
        old_obj->size = new_size;
        return ptr;
    }

    /* 否则分配新的并复制 */
    new_ptr = zs_malloc(pool, new_size);
    if (new_ptr == NULL) {
        return NULL;
    }

    copy_size = (old_obj->size < new_size) ? old_obj->size : new_size;
    memcpy(new_ptr, ptr, copy_size);

    zs_free(pool, ptr);

    strcpy(g_zs_status, "zs: reallocated");
    return new_ptr;
}

uint32_t zs_malloc_usable_size(void *ptr)
{
    zs_object_t *obj;

    if (ptr == NULL) {
        return 0;
    }

    obj = (zs_object_t *) ((uint8_t *) ptr - sizeof(zs_object_t));
    if (obj->magic != ZS_OBJ_MAGIC) {
        return 0;
    }

    return obj->compressed_size;
}

uint64_t zs_pool_total_size(zs_pool_t *pool)
{
    if (pool == NULL) {
        return 0;
    }
    return pool->total_size;
}

uint64_t zs_pool_used_size(zs_pool_t *pool)
{
    if (pool == NULL) {
        return 0;
    }
    return pool->used_size;
}

uint64_t zs_pool_compressed_size(zs_pool_t *pool)
{
    if (pool == NULL) {
        return 0;
    }
    return pool->compressed_size;
}

float zs_pool_compression_ratio(zs_pool_t *pool)
{
    if (pool == NULL || pool->used_size == 0) {
        return 1.0f;
    }
    return (float) pool->compressed_size / (float) pool->used_size;
}

const zs_info_t *zs_info(void)
{
    return &g_zs_info;
}

const char *zs_status(void)
{
    return g_zs_status;
}
