#ifndef _GPU_H_
#define _GPU_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool initialized;
    bool framebuffer_ready;
    bool acceleration_ready;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint32_t submit_count;
    uint32_t present_count;
    char backend[24];
    char status[64];
} gpu_info_t;

void gpu_init(void);
void gpu_refresh(void);
const gpu_info_t *gpu_info(void);
const char *gpu_status(void);

#endif
