#ifndef _HASH_H_
#define _HASH_H_

#include "stdbool.h"
#include "stdint.h"

/* SHA-256 (legacy names) */
#define HASH_SHA256_DIGEST_SIZE 32
#define HASH_SHA256_HEX_SIZE    64

void hash_sha256(const uint8_t *data, uint32_t size, uint8_t digest[HASH_SHA256_DIGEST_SIZE]);
void hash_sha256_hex(const uint8_t *data, uint32_t size, char output[HASH_SHA256_HEX_SIZE + 1]);

/* MD5 */
#define MD5_DIGEST_SIZE    16
#define MD5_BLOCK_SIZE     64

typedef struct {
    uint32_t state[4];
    uint64_t count;
    uint8_t buffer[MD5_BLOCK_SIZE];
} md5_ctx_t;

void md5_init(md5_ctx_t *ctx);
void md5_update(md5_ctx_t *ctx, const uint8_t *data, uint32_t len);
void md5_final(md5_ctx_t *ctx, uint8_t digest[MD5_DIGEST_SIZE]);
void md5(const uint8_t *data, uint32_t len, uint8_t digest[MD5_DIGEST_SIZE]);

/* SHA-1 */
#define SHA1_DIGEST_SIZE   20
#define SHA1_BLOCK_SIZE    64

typedef struct {
    uint32_t state[5];
    uint64_t count;
    uint8_t buffer[SHA1_BLOCK_SIZE];
} sha1_ctx_t;

void sha1_init(sha1_ctx_t *ctx);
void sha1_update(sha1_ctx_t *ctx, const uint8_t *data, uint32_t len);
void sha1_final(sha1_ctx_t *ctx, uint8_t digest[SHA1_DIGEST_SIZE]);
void sha1(const uint8_t *data, uint32_t len, uint8_t digest[SHA1_DIGEST_SIZE]);

/* SHA-256 */
#define SHA256_DIGEST_SIZE 32
#define SHA256_BLOCK_SIZE  64

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[SHA256_BLOCK_SIZE];
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);
void sha256(const uint8_t *data, uint32_t len, uint8_t digest[SHA256_DIGEST_SIZE]);

/* HMAC */
void hmac_md5(const uint8_t *key, uint32_t key_len,
              const uint8_t *data, uint32_t data_len,
              uint8_t digest[MD5_DIGEST_SIZE]);
void hmac_sha1(const uint8_t *key, uint32_t key_len,
               const uint8_t *data, uint32_t data_len,
               uint8_t digest[SHA1_DIGEST_SIZE]);
void hmac_sha256(const uint8_t *key, uint32_t key_len,
                 const uint8_t *data, uint32_t data_len,
                 uint8_t digest[SHA256_DIGEST_SIZE]);

#endif
