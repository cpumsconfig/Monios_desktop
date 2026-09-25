#ifndef _PNG_H_
#define _PNG_H_

#include "stdint.h"

/*
 * Minimal freestanding encoders used by the screenshot / screen-recording
 * tools (Task 21 / Task 22).  No malloc, no libc -- every function writes
 * into a caller-provided buffer.
 *
 * Pixel input format for the RGB helpers: tightly packed, row-major,
 * one byte per channel: R0 G0 B0 R1 G1 B1 ...
 *
 * Output buffer sizing:
 *   PNG:  w*h*3 + w*(overhead ~= 40 bytes per 65535-byte block) + 128.
 *         A safe upper bound is w*h*3 + 65536.
 *   BMP:  54 + h*(((w*3 + 3) & ~3)).
 */

/* Encode 24-bit RGB pixels as a PNG (zlib stored blocks, filter 0).
 * Returns the number of bytes written to out, or 0 on error. */
uint32_t png_encode_rgb(uint8_t *out, uint32_t out_cap,
                        const uint8_t *rgb, uint32_t w, uint32_t h);

/* Encode 24-bit RGB pixels as a Windows BMP (BITMAPINFOHEADER, bottom-up).
 * Returns the number of bytes written to out, or 0 on error. */
uint32_t bmp_encode_rgb(uint8_t *out, uint32_t out_cap,
                        const uint8_t *rgb, uint32_t w, uint32_t h);

#endif
