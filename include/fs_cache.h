#ifndef _FS_CACHE_H_
#define _FS_CACHE_H_

#include "stdbool.h"
#include "stdint.h"

typedef int32_t (*fs_cache_loader_t)(const char *path, uint32_t offset, void *buffer, uint32_t size);

typedef struct {
    bool enabled;
    uint32_t slots;
    uint32_t block_size;
    uint32_t hits;
    uint32_t misses;
    uint32_t fills;
    uint32_t invalidations;
    char status[64];
} fs_cache_info_t;

void fs_cache_init(void);
int32_t fs_cache_read_at(const char *path, uint32_t offset, void *buffer, uint32_t size, fs_cache_loader_t loader);
void fs_cache_invalidate_path(const char *path);
void fs_cache_invalidate_all(void);
const fs_cache_info_t *fs_cache_info(void);
const char *fs_cache_status(void);

#endif
