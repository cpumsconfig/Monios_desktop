#include "common.h"
#include "hash.h"

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t buffer[64];
    uint32_t buffer_size;
} sha256_ctx_t;

static const uint32_t g_sha256_constants[64] = {
    0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5,
    0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
    0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
    0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
    0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC,
    0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
    0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7,
    0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
    0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
    0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
    0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3,
    0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
    0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5,
    0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
    0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
    0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
};

static uint32_t sha256_rotr(uint32_t value, uint32_t count)
{
    return (value >> count) | (value << (32 - count));
}

static uint32_t sha256_choose(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ ((~x) & z);
}

static uint32_t sha256_majority(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

static uint32_t sha256_upper_sigma0(uint32_t x)
{
    return sha256_rotr(x, 2) ^ sha256_rotr(x, 13) ^ sha256_rotr(x, 22);
}

static uint32_t sha256_upper_sigma1(uint32_t x)
{
    return sha256_rotr(x, 6) ^ sha256_rotr(x, 11) ^ sha256_rotr(x, 25);
}

static uint32_t sha256_lower_sigma0(uint32_t x)
{
    return sha256_rotr(x, 7) ^ sha256_rotr(x, 18) ^ (x >> 3);
}

static uint32_t sha256_lower_sigma1(uint32_t x)
{
    return sha256_rotr(x, 17) ^ sha256_rotr(x, 19) ^ (x >> 10);
}

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (uint32_t i = 0; i < 16; i++) {
        w[i] = ((uint32_t) block[i * 4] << 24) |
               ((uint32_t) block[i * 4 + 1] << 16) |
               ((uint32_t) block[i * 4 + 2] << 8) |
               (uint32_t) block[i * 4 + 3];
    }

    for (uint32_t i = 16; i < 64; i++) {
        w[i] = sha256_lower_sigma1(w[i - 2]) + w[i - 7] + sha256_lower_sigma0(w[i - 15]) + w[i - 16];
    }

    for (uint32_t i = 0; i < 64; i++) {
        uint32_t t1 = h + sha256_upper_sigma1(e) + sha256_choose(e, f, g) + g_sha256_constants[i] + w[i];
        uint32_t t2 = sha256_upper_sigma0(a) + sha256_majority(a, b, c);

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(sha256_ctx_t *ctx)
{
    ctx->state[0] = 0x6A09E667;
    ctx->state[1] = 0xBB67AE85;
    ctx->state[2] = 0x3C6EF372;
    ctx->state[3] = 0xA54FF53A;
    ctx->state[4] = 0x510E527F;
    ctx->state[5] = 0x9B05688C;
    ctx->state[6] = 0x1F83D9AB;
    ctx->state[7] = 0x5BE0CD19;
    ctx->bit_count = 0;
    ctx->buffer_size = 0;
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t size)
{
    while (size > 0) {
        uint32_t copy_size = 64 - ctx->buffer_size;
        if (copy_size > size) {
            copy_size = size;
        }

        memcpy(ctx->buffer + ctx->buffer_size, data, copy_size);
        ctx->buffer_size += copy_size;
        ctx->bit_count += (uint64_t) copy_size * 8;
        data += copy_size;
        size -= copy_size;

        if (ctx->buffer_size == 64) {
            sha256_transform(ctx, ctx->buffer);
            ctx->buffer_size = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t digest[HASH_SHA256_DIGEST_SIZE])
{
    uint64_t bit_count = ctx->bit_count;

    ctx->buffer[ctx->buffer_size++] = 0x80;
    if (ctx->buffer_size > 56) {
        while (ctx->buffer_size < 64) {
            ctx->buffer[ctx->buffer_size++] = 0;
        }
        sha256_transform(ctx, ctx->buffer);
        ctx->buffer_size = 0;
    }

    while (ctx->buffer_size < 56) {
        ctx->buffer[ctx->buffer_size++] = 0;
    }

    for (uint32_t i = 0; i < 8; i++) {
        ctx->buffer[63 - i] = (uint8_t) (bit_count >> (i * 8));
    }
    sha256_transform(ctx, ctx->buffer);

    for (uint32_t i = 0; i < 8; i++) {
        digest[i * 4] = (uint8_t) (ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t) (ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t) (ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t) ctx->state[i];
    }
}

void hash_sha256(const uint8_t *data, uint32_t size, uint8_t digest[HASH_SHA256_DIGEST_SIZE])
{
    sha256_ctx_t ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, data, size);
    sha256_final(&ctx, digest);
}

void hash_sha256_hex(const uint8_t *data, uint32_t size, char output[HASH_SHA256_HEX_SIZE + 1])
{
    static const char hex[] = "0123456789abcdef";
    uint8_t digest[HASH_SHA256_DIGEST_SIZE];

    hash_sha256(data, size, digest);
    for (uint32_t i = 0; i < HASH_SHA256_DIGEST_SIZE; i++) {
        output[i * 2] = hex[digest[i] >> 4];
        output[i * 2 + 1] = hex[digest[i] & 0x0F];
    }
    output[HASH_SHA256_HEX_SIZE] = '\0';
}
