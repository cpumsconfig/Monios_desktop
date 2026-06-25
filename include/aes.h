#ifndef _AES_H_
#define _AES_H_

#include "stdbool.h"
#include "stdint.h"

#define AES_BLOCK_SIZE 16

#define AES_KEY_SIZE_128 16
#define AES_KEY_SIZE_192 24
#define AES_KEY_SIZE_256 32

#define AES_MAX_ROUNDS 14
#define AES_MAX_NR_KEY 60

typedef struct {
    uint32_t rounds;
    uint32_t key_len;
    uint32_t round_keys[AES_MAX_NR_KEY];
    uint8_t iv[AES_BLOCK_SIZE];
} aes_ctx_t;

int32_t aes_init(aes_ctx_t *ctx, const uint8_t *key, uint32_t key_len);
void aes_set_iv(aes_ctx_t *ctx, const uint8_t iv[AES_BLOCK_SIZE]);

void aes_encrypt_ecb(aes_ctx_t *ctx, const uint8_t input[AES_BLOCK_SIZE],
                     uint8_t output[AES_BLOCK_SIZE]);
void aes_decrypt_ecb(aes_ctx_t *ctx, const uint8_t input[AES_BLOCK_SIZE],
                     uint8_t output[AES_BLOCK_SIZE]);

int32_t aes_encrypt_cbc(aes_ctx_t *ctx, const uint8_t *input, uint32_t length,
                        uint8_t *output);
int32_t aes_decrypt_cbc(aes_ctx_t *ctx, const uint8_t *input, uint32_t length,
                        uint8_t *output);

#endif
