#ifndef _HASH_H_
#define _HASH_H_

#include "stdint.h"

#define HASH_SHA256_DIGEST_SIZE 32
#define HASH_SHA256_HEX_SIZE    64

void hash_sha256(const uint8_t *data, uint32_t size, uint8_t digest[HASH_SHA256_DIGEST_SIZE]);
void hash_sha256_hex(const uint8_t *data, uint32_t size, char output[HASH_SHA256_HEX_SIZE + 1]);

#endif
