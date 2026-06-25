#ifndef _FRAME_H_
#define _FRAME_H_

#include "stdbool.h"
#include "stdint.h"

#define FRAME_PAGE_SIZE 4096U

typedef struct {
    uint64_t base;
    uint32_t total_frames;
    uint32_t used_frames;
    uint32_t reserved_frames;
    uint32_t alloc_requests;
    uint32_t free_requests;
    uint64_t last_base;
    uint32_t last_count;
} frame_info_t;

void frame_init(void);
uint64_t frame_alloc(uint32_t frame_count);
uint64_t frame_alloc_aligned(uint32_t frame_count, uint32_t align_frames);
bool frame_free(uint64_t base, uint32_t frame_count);
bool frame_reserve(uint64_t base, uint32_t frame_count);
const frame_info_t *frame_info(void);
const char *frame_status(void);

#endif
