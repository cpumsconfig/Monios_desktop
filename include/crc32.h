#ifndef _CRC32_H_
#define _CRC32_H_

#include "stdint.h"

/*
 * CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320, init 0xFFFFFFFF,
 * final xor 0xFFFFFFFF) -- the same checksum used by ZIP archives (and
 * Ethernet / gzip).  Pure freestanding C, no malloc, usable by both the
 * kernel and user applications.
 *
 * One-shot:
 *   uint32_t c = crc32_buf(data, len);
 * Streaming:
 *   uint32_t c = crc32_init();
 *   c = crc32_update(c, chunk1, n1);
 *   c = crc32_update(c, chunk2, n2);
 *   c = crc32_final(c);
 */

uint32_t crc32_init(void);
uint32_t crc32_update(uint32_t crc, const void *data, uint32_t len);
uint32_t crc32_final(uint32_t crc);
uint32_t crc32_buf(const void *data, uint32_t len);

#endif
