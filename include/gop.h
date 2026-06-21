#ifndef _GOP_H_
#define _GOP_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool available;
    uint32_t framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_bytes;
    char backend[24];
    char status[64];
} gop_info_t;

void gop_init(void);
const gop_info_t *gop_info(void);
const char *gop_status(void);

#endif
