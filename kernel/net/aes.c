#include "aes.h"
#include "string.h"
#include "stddef.h"

/* AES S-box */
static const uint8_t aes_sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
    0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
    0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
    0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
    0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
    0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
    0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
    0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
    0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
    0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
    0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
    0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
    0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

/* AES inverse S-box */
static const uint8_t aes_inv_sbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38,
    0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87,
    0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d,
    0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2,
    0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16,
    0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda,
    0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a,
    0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02,
    0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea,
    0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85,
    0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89,
    0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20,
    0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31,
    0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d,
    0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0,
    0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26,
    0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

/* Rcon */
static const uint8_t aes_rcon[11] = {
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

/* GF(2^8) multiplication by 2 */
static uint8_t aes_gf_mul2(uint8_t x)
{
    return (x << 1) ^ ((x & 0x80) ? 0x1b : 0x00);
}

/* GF(2^8) multiplication by 3 */
static uint8_t aes_gf_mul3(uint8_t x)
{
    return aes_gf_mul2(x) ^ x;
}

/* GF(2^8) multiplication by 0x0e */
static uint8_t aes_gf_mul_e(uint8_t x)
{
    uint8_t x2 = aes_gf_mul2(x);
    uint8_t x4 = aes_gf_mul2(x2);
    uint8_t x8 = aes_gf_mul2(x4);
    return x8 ^ x4 ^ x2;
}

/* GF(2^8) multiplication by 0x0b */
static uint8_t aes_gf_mul_b(uint8_t x)
{
    uint8_t x2 = aes_gf_mul2(x);
    uint8_t x4 = aes_gf_mul2(x2);
    uint8_t x8 = aes_gf_mul2(x4);
    return x8 ^ x2 ^ x;
}

/* GF(2^8) multiplication by 0x0d */
static uint8_t aes_gf_mul_d(uint8_t x)
{
    uint8_t x2 = aes_gf_mul2(x);
    uint8_t x4 = aes_gf_mul2(x2);
    uint8_t x8 = aes_gf_mul2(x4);
    return x8 ^ x4 ^ x;
}

/* GF(2^8) multiplication by 0x09 */
static uint8_t aes_gf_mul_9(uint8_t x)
{
    uint8_t x2 = aes_gf_mul2(x);
    uint8_t x4 = aes_gf_mul2(x2);
    uint8_t x8 = aes_gf_mul2(x4);
    return x8 ^ x;
}

/* SubBytes */
static void aes_sub_bytes(uint8_t state[4][4])
{
    int i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            state[i][j] = aes_sbox[state[i][j]];
        }
    }
}

/* InvSubBytes */
static void aes_inv_sub_bytes(uint8_t state[4][4])
{
    int i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            state[i][j] = aes_inv_sbox[state[i][j]];
        }
    }
}

/* ShiftRows */
static void aes_shift_rows(uint8_t state[4][4])
{
    uint8_t temp;

    /* Row 1: shift left by 1 */
    temp = state[1][0];
    state[1][0] = state[1][1];
    state[1][1] = state[1][2];
    state[1][2] = state[1][3];
    state[1][3] = temp;

    /* Row 2: shift left by 2 */
    temp = state[2][0]; state[2][0] = state[2][2]; state[2][2] = temp;
    temp = state[2][1]; state[2][1] = state[2][3]; state[2][3] = temp;

    /* Row 3: shift left by 3 (or right by 1) */
    temp = state[3][3];
    state[3][3] = state[3][2];
    state[3][2] = state[3][1];
    state[3][1] = state[3][0];
    state[3][0] = temp;
}

/* InvShiftRows */
static void aes_inv_shift_rows(uint8_t state[4][4])
{
    uint8_t temp;

    /* Row 1: shift right by 1 */
    temp = state[1][3];
    state[1][3] = state[1][2];
    state[1][2] = state[1][1];
    state[1][1] = state[1][0];
    state[1][0] = temp;

    /* Row 2: shift right by 2 */
    temp = state[2][0]; state[2][0] = state[2][2]; state[2][2] = temp;
    temp = state[2][1]; state[2][1] = state[2][3]; state[2][3] = temp;

    /* Row 3: shift right by 3 (or left by 1) */
    temp = state[3][0];
    state[3][0] = state[3][1];
    state[3][1] = state[3][2];
    state[3][2] = state[3][3];
    state[3][3] = temp;
}

/* MixColumns */
static void aes_mix_columns(uint8_t state[4][4])
{
    int i;
    uint8_t s0, s1, s2, s3;
    for (i = 0; i < 4; i++) {
        s0 = state[0][i];
        s1 = state[1][i];
        s2 = state[2][i];
        s3 = state[3][i];
        state[0][i] = aes_gf_mul2(s0) ^ aes_gf_mul3(s1) ^ s2 ^ s3;
        state[1][i] = s0 ^ aes_gf_mul2(s1) ^ aes_gf_mul3(s2) ^ s3;
        state[2][i] = s0 ^ s1 ^ aes_gf_mul2(s2) ^ aes_gf_mul3(s3);
        state[3][i] = aes_gf_mul3(s0) ^ s1 ^ s2 ^ aes_gf_mul2(s3);
    }
}

/* InvMixColumns */
static void aes_inv_mix_columns(uint8_t state[4][4])
{
    int i;
    uint8_t s0, s1, s2, s3;
    for (i = 0; i < 4; i++) {
        s0 = state[0][i];
        s1 = state[1][i];
        s2 = state[2][i];
        s3 = state[3][i];
        state[0][i] = aes_gf_mul_e(s0) ^ aes_gf_mul_b(s1) ^ aes_gf_mul_d(s2) ^ aes_gf_mul_9(s3);
        state[1][i] = aes_gf_mul_9(s0) ^ aes_gf_mul_e(s1) ^ aes_gf_mul_b(s2) ^ aes_gf_mul_d(s3);
        state[2][i] = aes_gf_mul_d(s0) ^ aes_gf_mul_9(s1) ^ aes_gf_mul_e(s2) ^ aes_gf_mul_b(s3);
        state[3][i] = aes_gf_mul_b(s0) ^ aes_gf_mul_d(s1) ^ aes_gf_mul_9(s2) ^ aes_gf_mul_e(s3);
    }
}

/* AddRoundKey */
static void aes_add_round_key(uint8_t state[4][4], const uint32_t *round_key)
{
    int i, j;
    for (i = 0; i < 4; i++) {
        uint32_t k = round_key[i];
        for (j = 0; j < 4; j++) {
            state[j][i] ^= (uint8_t) ((k >> (24 - j * 8)) & 0xFF);
        }
    }
}

/* Key expansion */
static void aes_key_expansion(const uint8_t *key, uint32_t key_len,
                               uint32_t *round_keys, uint32_t rounds)
{
    uint32_t i, j;
    uint32_t temp;
    uint32_t nk = key_len / 4;
    uint32_t nr = rounds;

    for (i = 0; i < nk; i++) {
        round_keys[i] = ((uint32_t) key[i * 4] << 24) |
                        ((uint32_t) key[i * 4 + 1] << 16) |
                        ((uint32_t) key[i * 4 + 2] << 8) |
                        ((uint32_t) key[i * 4 + 3]);
    }

    for (i = nk; i < 4 * (nr + 1); i++) {
        temp = round_keys[i - 1];
        if (i % nk == 0) {
            /* RotWord */
            temp = ((temp << 8) | (temp >> 24));
            /* SubWord */
            temp = ((uint32_t) aes_sbox[(temp >> 24) & 0xFF] << 24) |
                   ((uint32_t) aes_sbox[(temp >> 16) & 0xFF] << 16) |
                   ((uint32_t) aes_sbox[(temp >> 8) & 0xFF] << 8) |
                   ((uint32_t) aes_sbox[temp & 0xFF]);
            /* XOR with Rcon */
            temp ^= ((uint32_t) aes_rcon[i / nk]) << 24;
        } else if (nk > 6 && i % nk == 4) {
            /* SubWord for AES-256 */
            temp = ((uint32_t) aes_sbox[(temp >> 24) & 0xFF] << 24) |
                   ((uint32_t) aes_sbox[(temp >> 16) & 0xFF] << 16) |
                   ((uint32_t) aes_sbox[(temp >> 8) & 0xFF] << 8) |
                   ((uint32_t) aes_sbox[temp & 0xFF]);
        }
        round_keys[i] = round_keys[i - nk] ^ temp;
    }
}

int32_t aes_init(aes_ctx_t *ctx, const uint8_t *key, uint32_t key_len)
{
    if (ctx == NULL || key == NULL) {
        return -1;
    }

    switch (key_len) {
        case AES_KEY_SIZE_128:
            ctx->rounds = 10;
            break;
        case AES_KEY_SIZE_192:
            ctx->rounds = 12;
            break;
        case AES_KEY_SIZE_256:
            ctx->rounds = 14;
            break;
        default:
            return -1;
    }

    ctx->key_len = key_len;
    memset(ctx->iv, 0, AES_BLOCK_SIZE);
    aes_key_expansion(key, key_len, ctx->round_keys, ctx->rounds);
    return 0;
}

void aes_set_iv(aes_ctx_t *ctx, const uint8_t iv[AES_BLOCK_SIZE])
{
    if (ctx && iv) {
        memcpy(ctx->iv, iv, AES_BLOCK_SIZE);
    }
}

void aes_encrypt_ecb(aes_ctx_t *ctx, const uint8_t input[AES_BLOCK_SIZE],
                     uint8_t output[AES_BLOCK_SIZE])
{
    uint8_t state[4][4];
    uint32_t round;
    int i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            state[j][i] = input[i * 4 + j];
        }
    }

    aes_add_round_key(state, ctx->round_keys);

    for (round = 1; round < ctx->rounds; round++) {
        aes_sub_bytes(state);
        aes_shift_rows(state);
        aes_mix_columns(state);
        aes_add_round_key(state, &ctx->round_keys[round * 4]);
    }

    aes_sub_bytes(state);
    aes_shift_rows(state);
    aes_add_round_key(state, &ctx->round_keys[ctx->rounds * 4]);

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            output[i * 4 + j] = state[j][i];
        }
    }
}

void aes_decrypt_ecb(aes_ctx_t *ctx, const uint8_t input[AES_BLOCK_SIZE],
                     uint8_t output[AES_BLOCK_SIZE])
{
    uint8_t state[4][4];
    uint32_t round;
    int i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            state[j][i] = input[i * 4 + j];
        }
    }

    aes_add_round_key(state, &ctx->round_keys[ctx->rounds * 4]);

    for (round = ctx->rounds - 1; round > 0; round--) {
        aes_inv_shift_rows(state);
        aes_inv_sub_bytes(state);
        aes_add_round_key(state, &ctx->round_keys[round * 4]);
        aes_inv_mix_columns(state);
    }

    aes_inv_shift_rows(state);
    aes_inv_sub_bytes(state);
    aes_add_round_key(state, ctx->round_keys);

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            output[i * 4 + j] = state[j][i];
        }
    }
}

int32_t aes_encrypt_cbc(aes_ctx_t *ctx, const uint8_t *input, uint32_t length,
                        uint8_t *output)
{
    uint8_t iv[AES_BLOCK_SIZE];
    uint32_t i;

    if (ctx == NULL || input == NULL || output == NULL) {
        return -1;
    }
    if (length % AES_BLOCK_SIZE != 0) {
        return -1;
    }

    memcpy(iv, ctx->iv, AES_BLOCK_SIZE);

    for (i = 0; i < length; i += AES_BLOCK_SIZE) {
        uint8_t block[AES_BLOCK_SIZE];
        int j;
        for (j = 0; j < AES_BLOCK_SIZE; j++) {
            block[j] = input[i + j] ^ iv[j];
        }
        aes_encrypt_ecb(ctx, block, &output[i]);
        memcpy(iv, &output[i], AES_BLOCK_SIZE);
    }

    return 0;
}

int32_t aes_decrypt_cbc(aes_ctx_t *ctx, const uint8_t *input, uint32_t length,
                        uint8_t *output)
{
    uint8_t iv[AES_BLOCK_SIZE];
    uint32_t i;

    if (ctx == NULL || input == NULL || output == NULL) {
        return -1;
    }
    if (length % AES_BLOCK_SIZE != 0) {
        return -1;
    }

    memcpy(iv, ctx->iv, AES_BLOCK_SIZE);

    for (i = 0; i < length; i += AES_BLOCK_SIZE) {
        uint8_t block[AES_BLOCK_SIZE];
        int j;
        aes_decrypt_ecb(ctx, &input[i], block);
        for (j = 0; j < AES_BLOCK_SIZE; j++) {
            output[i + j] = block[j] ^ iv[j];
        }
        memcpy(iv, &input[i], AES_BLOCK_SIZE);
    }

    return 0;
}
