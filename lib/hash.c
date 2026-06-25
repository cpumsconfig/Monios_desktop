#include "common.h"
#include "hash.h"

/* ============================================================
 *  SHA-256
 * ============================================================ */

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

void sha256_init(sha256_ctx_t *ctx)
{
    ctx->state[0] = 0x6A09E667;
    ctx->state[1] = 0xBB67AE85;
    ctx->state[2] = 0x3C6EF372;
    ctx->state[3] = 0xA54FF53A;
    ctx->state[4] = 0x510E527F;
    ctx->state[5] = 0x9B05688C;
    ctx->state[6] = 0x1F83D9AB;
    ctx->state[7] = 0x5BE0CD19;
    ctx->count = 0;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t buffer_len = (uint32_t) (ctx->count % 64);
    ctx->count += len;

    while (len > 0) {
        uint32_t copy_len = 64 - buffer_len;
        if (copy_len > len) {
            copy_len = len;
        }

        memcpy(ctx->buffer + buffer_len, data, copy_len);
        buffer_len += copy_len;
        data += copy_len;
        len -= copy_len;

        if (buffer_len == 64) {
            sha256_transform(ctx, ctx->buffer);
            buffer_len = 0;
        }
    }
}

void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE])
{
    uint64_t bit_count = ctx->count * 8;
    uint32_t buffer_len = (uint32_t) (ctx->count % 64);

    ctx->buffer[buffer_len++] = 0x80;

    if (buffer_len > 56) {
        while (buffer_len < 64) {
            ctx->buffer[buffer_len++] = 0;
        }
        sha256_transform(ctx, ctx->buffer);
        buffer_len = 0;
    }

    while (buffer_len < 56) {
        ctx->buffer[buffer_len++] = 0;
    }

    for (int i = 7; i >= 0; i--) {
        ctx->buffer[buffer_len++] = (uint8_t) (bit_count >> (i * 8));
    }

    sha256_transform(ctx, ctx->buffer);

    for (uint32_t i = 0; i < 8; i++) {
        digest[i * 4] = (uint8_t) (ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t) (ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t) (ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t) ctx->state[i];
    }
}

void sha256(const uint8_t *data, uint32_t len, uint8_t digest[SHA256_DIGEST_SIZE])
{
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

/* Legacy names */
void hash_sha256(const uint8_t *data, uint32_t size, uint8_t digest[HASH_SHA256_DIGEST_SIZE])
{
    sha256(data, size, digest);
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

/* ============================================================
 *  MD5
 * ============================================================ */

static const uint32_t md5_s[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

static const uint32_t md5_k[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static uint32_t md5_left_rotate(uint32_t x, uint32_t n)
{
    return (x << n) | (x >> (32 - n));
}

static void md5_transform(md5_ctx_t *ctx, const uint8_t block[64])
{
    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t m[16];

    for (uint32_t i = 0; i < 16; i++) {
        m[i] = ((uint32_t) block[i * 4]) |
               ((uint32_t) block[i * 4 + 1] << 8) |
               ((uint32_t) block[i * 4 + 2] << 16) |
               ((uint32_t) block[i * 4 + 3] << 24);
    }

    for (uint32_t i = 0; i < 64; i++) {
        uint32_t f, g;

        if (i < 16) {
            f = (b & c) | ((~b) & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | ((~d) & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | (~d));
            g = (7 * i) % 16;
        }

        uint32_t temp = d;
        d = c;
        c = b;
        b = b + md5_left_rotate(a + f + md5_k[i] + m[g], md5_s[i]);
        a = temp;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
}

void md5_init(md5_ctx_t *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->count = 0;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

void md5_update(md5_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t buffer_len = (uint32_t) (ctx->count % 64);
    ctx->count += len;

    while (len > 0) {
        uint32_t copy_len = 64 - buffer_len;
        if (copy_len > len) {
            copy_len = len;
        }

        memcpy(ctx->buffer + buffer_len, data, copy_len);
        buffer_len += copy_len;
        data += copy_len;
        len -= copy_len;

        if (buffer_len == 64) {
            md5_transform(ctx, ctx->buffer);
            buffer_len = 0;
        }
    }
}

void md5_final(md5_ctx_t *ctx, uint8_t digest[MD5_DIGEST_SIZE])
{
    uint64_t bit_count = ctx->count * 8;
    uint32_t buffer_len = (uint32_t) (ctx->count % 64);

    ctx->buffer[buffer_len++] = 0x80;

    if (buffer_len > 56) {
        while (buffer_len < 64) {
            ctx->buffer[buffer_len++] = 0;
        }
        md5_transform(ctx, ctx->buffer);
        buffer_len = 0;
    }

    while (buffer_len < 56) {
        ctx->buffer[buffer_len++] = 0;
    }

    for (uint32_t i = 0; i < 8; i++) {
        ctx->buffer[buffer_len++] = (uint8_t) (bit_count >> (i * 8));
    }

    md5_transform(ctx, ctx->buffer);

    for (uint32_t i = 0; i < 4; i++) {
        digest[i * 4] = (uint8_t) (ctx->state[i] & 0xFF);
        digest[i * 4 + 1] = (uint8_t) ((ctx->state[i] >> 8) & 0xFF);
        digest[i * 4 + 2] = (uint8_t) ((ctx->state[i] >> 16) & 0xFF);
        digest[i * 4 + 3] = (uint8_t) ((ctx->state[i] >> 24) & 0xFF);
    }
}

void md5(const uint8_t *data, uint32_t len, uint8_t digest[MD5_DIGEST_SIZE])
{
    md5_ctx_t ctx;
    md5_init(&ctx);
    md5_update(&ctx, data, len);
    md5_final(&ctx, digest);
}

/* ============================================================
 *  SHA-1
 * ============================================================ */

static uint32_t sha1_left_rotate(uint32_t value, uint32_t count)
{
    return (value << count) | (value >> (32 - count));
}

static void sha1_transform(sha1_ctx_t *ctx, const uint8_t block[64])
{
    uint32_t w[80];
    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];

    for (uint32_t i = 0; i < 16; i++) {
        w[i] = ((uint32_t) block[i * 4] << 24) |
               ((uint32_t) block[i * 4 + 1] << 16) |
               ((uint32_t) block[i * 4 + 2] << 8) |
               (uint32_t) block[i * 4 + 3];
    }

    for (uint32_t i = 16; i < 80; i++) {
        w[i] = sha1_left_rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    for (uint32_t i = 0; i < 80; i++) {
        uint32_t f, k;

        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }

        uint32_t temp = sha1_left_rotate(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = sha1_left_rotate(b, 30);
        b = a;
        a = temp;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
}

void sha1_init(sha1_ctx_t *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

void sha1_update(sha1_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t buffer_len = (uint32_t) (ctx->count % 64);
    ctx->count += len;

    while (len > 0) {
        uint32_t copy_len = 64 - buffer_len;
        if (copy_len > len) {
            copy_len = len;
        }

        memcpy(ctx->buffer + buffer_len, data, copy_len);
        buffer_len += copy_len;
        data += copy_len;
        len -= copy_len;

        if (buffer_len == 64) {
            sha1_transform(ctx, ctx->buffer);
            buffer_len = 0;
        }
    }
}

void sha1_final(sha1_ctx_t *ctx, uint8_t digest[SHA1_DIGEST_SIZE])
{
    uint64_t bit_count = ctx->count * 8;
    uint32_t buffer_len = (uint32_t) (ctx->count % 64);

    ctx->buffer[buffer_len++] = 0x80;

    if (buffer_len > 56) {
        while (buffer_len < 64) {
            ctx->buffer[buffer_len++] = 0;
        }
        sha1_transform(ctx, ctx->buffer);
        buffer_len = 0;
    }

    while (buffer_len < 56) {
        ctx->buffer[buffer_len++] = 0;
    }

    for (int i = 7; i >= 0; i--) {
        ctx->buffer[buffer_len++] = (uint8_t) (bit_count >> (i * 8));
    }

    sha1_transform(ctx, ctx->buffer);

    for (uint32_t i = 0; i < 5; i++) {
        digest[i * 4] = (uint8_t) (ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t) (ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t) (ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t) ctx->state[i];
    }
}

void sha1(const uint8_t *data, uint32_t len, uint8_t digest[SHA1_DIGEST_SIZE])
{
    sha1_ctx_t ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, digest);
}

/* ============================================================
 *  HMAC
 * ============================================================ */

void hmac_md5(const uint8_t *key, uint32_t key_len,
              const uint8_t *data, uint32_t data_len,
              uint8_t digest[MD5_DIGEST_SIZE])
{
    md5_ctx_t ctx;
    uint8_t k_ipad[MD5_BLOCK_SIZE];
    uint8_t k_opad[MD5_BLOCK_SIZE];
    uint8_t tmp_key[MD5_BLOCK_SIZE];

    if (key_len > MD5_BLOCK_SIZE) {
        md5(key, key_len, tmp_key);
        key = tmp_key;
        key_len = MD5_DIGEST_SIZE;
    }

    memset(k_ipad, 0x36, MD5_BLOCK_SIZE);
    memset(k_opad, 0x5C, MD5_BLOCK_SIZE);

    for (uint32_t i = 0; i < key_len; i++) {
        k_ipad[i] ^= key[i];
        k_opad[i] ^= key[i];
    }

    md5_init(&ctx);
    md5_update(&ctx, k_ipad, MD5_BLOCK_SIZE);
    md5_update(&ctx, data, data_len);
    md5_final(&ctx, digest);

    md5_init(&ctx);
    md5_update(&ctx, k_opad, MD5_BLOCK_SIZE);
    md5_update(&ctx, digest, MD5_DIGEST_SIZE);
    md5_final(&ctx, digest);
}

void hmac_sha1(const uint8_t *key, uint32_t key_len,
               const uint8_t *data, uint32_t data_len,
               uint8_t digest[SHA1_DIGEST_SIZE])
{
    sha1_ctx_t ctx;
    uint8_t k_ipad[SHA1_BLOCK_SIZE];
    uint8_t k_opad[SHA1_BLOCK_SIZE];
    uint8_t tmp_key[SHA1_BLOCK_SIZE];

    if (key_len > SHA1_BLOCK_SIZE) {
        sha1(key, key_len, tmp_key);
        key = tmp_key;
        key_len = SHA1_DIGEST_SIZE;
    }

    memset(k_ipad, 0x36, SHA1_BLOCK_SIZE);
    memset(k_opad, 0x5C, SHA1_BLOCK_SIZE);

    for (uint32_t i = 0; i < key_len; i++) {
        k_ipad[i] ^= key[i];
        k_opad[i] ^= key[i];
    }

    sha1_init(&ctx);
    sha1_update(&ctx, k_ipad, SHA1_BLOCK_SIZE);
    sha1_update(&ctx, data, data_len);
    sha1_final(&ctx, digest);

    sha1_init(&ctx);
    sha1_update(&ctx, k_opad, SHA1_BLOCK_SIZE);
    sha1_update(&ctx, digest, SHA1_DIGEST_SIZE);
    sha1_final(&ctx, digest);
}

void hmac_sha256(const uint8_t *key, uint32_t key_len,
                 const uint8_t *data, uint32_t data_len,
                 uint8_t digest[SHA256_DIGEST_SIZE])
{
    sha256_ctx_t ctx;
    uint8_t k_ipad[SHA256_BLOCK_SIZE];
    uint8_t k_opad[SHA256_BLOCK_SIZE];
    uint8_t tmp_key[SHA256_BLOCK_SIZE];

    if (key_len > SHA256_BLOCK_SIZE) {
        sha256(key, key_len, tmp_key);
        key = tmp_key;
        key_len = SHA256_DIGEST_SIZE;
    }

    memset(k_ipad, 0x36, SHA256_BLOCK_SIZE);
    memset(k_opad, 0x5C, SHA256_BLOCK_SIZE);

    for (uint32_t i = 0; i < key_len; i++) {
        k_ipad[i] ^= key[i];
        k_opad[i] ^= key[i];
    }

    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, digest);

    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, digest, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, digest);
}
