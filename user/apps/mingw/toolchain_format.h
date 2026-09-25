#ifndef MONIOS_TINY_TOOLCHAIN_FORMAT_H
#define MONIOS_TINY_TOOLCHAIN_FORMAT_H

#include "stdint.h"

#define MOBJ_MAGIC_0 'M'
#define MOBJ_MAGIC_1 'O'
#define MOBJ_MAGIC_2 'B'
#define MOBJ_MAGIC_3 'J'
#define MOBJ_VERSION 1U
#define MOBJ_HEADER_SIZE 16U
#define MOBJ_OP_SIZE 16U
#define MOBJ_MAX_OPS 128U
#define MOBJ_MAX_STRING_BYTES 8192U

#define MOBJ_OP_WRITE 1U
#define MOBJ_OP_EXIT  2U

static void mobj_write_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t) (value & 0xFFU);
    dst[1] = (uint8_t) ((value >> 8) & 0xFFU);
    dst[2] = (uint8_t) ((value >> 16) & 0xFFU);
    dst[3] = (uint8_t) ((value >> 24) & 0xFFU);
}

static uint32_t mobj_read_u32(const uint8_t *src)
{
    return (uint32_t) src[0] |
           ((uint32_t) src[1] << 8) |
           ((uint32_t) src[2] << 16) |
           ((uint32_t) src[3] << 24);
}

static void mobj_write_i32(uint8_t *dst, int32_t value)
{
    mobj_write_u32(dst, (uint32_t) value);
}

static int32_t mobj_read_i32(const uint8_t *src)
{
    return (int32_t) mobj_read_u32(src);
}

#endif
