#include "hash.h"
#include "string.h"

/* ============================================================
 *  MD5
 * ============================================================ */

#define MD5_F(x, y, z) (((x) & (y)) | ((~(x)) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & (~(z))))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | (~(z))))
#define MD5_ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define MD5_FF(a, b, c, d, x, s, ac) \
    do { (a) += MD5_F((b), (c), (d)) + (x) + (uint32_t)(ac); \
         (a) = MD5_ROTATE_LEFT((a), (s)); (a) += (b); } while (0)
#define MD5_GG(a, b, c, d, x, s, ac) \
    do { (a) += MD5_G((b), (c), (d)) + (x) + (uint32_t)(ac); \
         (a) = MD5_ROTATE_LEFT((a), (s)); (a) += (b); } while (0)
#define MD5_HH(a, b, c, d, x, s, ac) \
    do { (a) += MD5_H((b), (c), (d)) + (x) + (uint32_t)(ac); \
         (a) = MD5_ROTATE_LEFT((a), (s)); (a) += (b); } while (0)
#define MD5_II(a, b, c, d, x, s, ac) \
    do { (a) += MD5_I((b), (c), (d)) + (x) + (uint32_t)(ac); \
         (a) = MD5_ROTATE_LEFT((a), (s)); (a) += (b); } while (0)

static void md5_transform(uint32_t state[4], const uint8_t block[64])
{
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t x[16];
    int i;

    for (i = 0; i < 16; i++) {
        x[i] = ((uint32_t) block[i * 4]) |
               (((uint32_t) block[i * 4 + 1]) << 8) |
               (((uint32_t) block[i * 4 + 2]) << 16) |
               (((uint32_t) block[i * 4 + 3]) << 24);
    }

    /* Round 1 */
    MD5_FF(a, b, c, d, x[ 0],  7, 0xd76aa478);
    MD5_FF(d, a, b, c, x[ 1], 12, 0xe8c7b756);
    MD5_FF(c, d, a, b, x[ 2], 17, 0x242070db);
    MD5_FF(b, c, d, a, x[ 3], 22, 0xc1bdceee);
    MD5_FF(a, b, c, d, x[ 4],  7, 0xf57c0faf);
    MD5_FF(d, a, b, c, x[ 5], 12, 0x4787c62a);
    MD5_FF(c, d, a, b, x[ 6], 17, 0xa8304613);
    MD5_FF(b, c, d, a, x[ 7], 22, 0xfd469501);
    MD5_FF(a, b, c, d, x[ 8],  7, 0x698098d8);
    MD5_FF(d, a, b, c, x[ 9], 12, 0x8b44f7af);
    MD5_FF(c, d, a, b, x[10], 17, 0xffff5bb1);
    MD5_FF(b, c, d, a, x[11], 22, 0x895cd7be);
    MD5_FF(a, b, c, d, x[12],  7, 0x6b901122);
    MD5_FF(d, a, b, c, x[13], 12, 0xfd987193);
    MD5_FF(c, d, a, b, x[14], 17, 0xa679438e);
    MD5_FF(b, c, d, a, x[15], 22, 0x49b40821);

    /* Round 2 */
    MD5_GG(a, b, c, d, x[ 1],  5, 0xf61e2562);
    MD5_GG(d, a, b, c, x[ 6],  9, 0xc040b340);
    MD5_GG(c, d, a, b, x[11], 14, 0x265e5a51);
    MD5_GG(b, c, d, a, x[ 0], 20, 0xe9b6c7aa);
    MD5_GG(a, b, c, d, x[ 5],  5, 0xd62f105d);
    MD5_GG(d, a, b, c, x[10],  9, 0x02441453);
    MD5_GG(c, d, a, b, x[15], 14, 0xd8a1e681);
    MD5_GG(b, c, d, a, x[ 4], 20, 0xe7d3fbc8);
    MD5_GG(a, b, c, d, x[ 9],  5, 0x21e1cde6);
    MD5_GG(d, a, b, c, x[14],  9, 0xc33707d6);
    MD5_GG(c, d, a, b, x[ 3], 14, 0xf4d50d87);
    MD5_GG(b, c, d, a, x[ 8], 20, 0x455a14ed);
    MD5_GG(a, b, c, d, x[13],  5, 0xa9e3e905);
    MD5_GG(d, a, b, c, x[ 2],  9, 0xfcefa3f8);
    MD5_GG(c, d, a, b, x[ 7], 14, 0x676f02d9);
    MD5_GG(b, c, d, a, x[12], 20, 0x8d2a4c8a);

    /* Round 3 */
    MD5_HH(a, b, c, d, x[ 5],  4, 0xfffa3942);
    MD5_HH(d, a, b, c, x[ 8], 11, 0x8771f681);
    MD5_HH(c, d, a, b, x[11], 16, 0x6d9d6122);
    MD5_HH(b, c, d, a, x[14], 23, 0xfde5380c);
    MD5_HH(a, b, c, d, x[ 1],  4, 0xa4beea44);
    MD5_HH(d, a, b, c, x[ 4], 11, 0x4bdecfa9);
    MD5_HH(c, d, a, b, x[ 7], 16, 0xf6bb4b60);
    MD5_HH(b, c, d, a, x[10], 23, 0xbebfbc70);
    MD5_HH(a, b, c, d, x[13],  4, 0x289b7ec6);
    MD5_HH(d, a, b, c, x[ 0], 11, 0xeaa127fa);
    MD5_HH(c, d, a, b, x[ 3], 16, 0xd4ef3085);
    MD5_HH(b, c, d, a, x[ 6], 23, 0x04881d05);
    MD5_HH(a, b, c, d, x[ 9],  4, 0xd9d4d039);
    MD5_HH(d, a, b, c, x[12], 11, 0xe6db99e5);
    MD5_HH(c, d, a, b, x[15], 16, 0x1fa27cf8);
    MD5_HH(b, c, d, a, x[ 2], 23, 0xc4ac5665);

    /* Round 4 */
    MD5_II(a, b, c, d, x[ 0],  6, 0xf4292244);
    MD5_II(d, a, b, c, x[ 7], 10, 0x432aff97);
    MD5_II(c, d, a, b, x[14], 15, 0xab9423a7);
    MD5_II(b, c, d, a, x[ 5], 21, 0xfc93a039);
    MD5_II(a, b, c, d, x[12],  6, 0x655b59c3);
    MD5_II(d, a, b, c, x[ 3], 10, 0x8f0ccc92);
    MD5_II(c, d, a, b, x[10], 15, 0xffeff47d);
    MD5_II(b, c, d, a, x[ 1], 21, 0x85845dd1);
    MD5_II(a, b, c, d, x[ 8],  6, 0x6fa87e4f);
    MD5_II(d, a, b, c, x[15], 10, 0xfe2ce6e0);
    MD5_II(c, d, a, b, x[ 6], 15, 0xa3014314);
    MD5_II(b, c, d, a, x[13], 21, 0x4e0811a1);
    MD5_II(a, b, c, d, x[ 4],  6, 0xf7537e82);
    MD5_II(d, a, b, c, x[11], 10, 0xbd3af235);
    MD5_II(c, d, a, b, x[ 2], 15, 0x2ad7d2bb);
    MD5_II(b, c, d, a, x[ 9], 21, 0xeb86d391);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

void md5_init(md5_ctx_t *ctx)
{
    ctx->count = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
}

void md5_update(md5_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t i, index, part_len;
    index = (uint32_t) ((ctx->count >> 3) & 0x3F);
    ctx->count += ((uint64_t) len) << 3;
    part_len = 64 - index;

    if (len >= part_len) {
        memcpy(&ctx->buffer[index], data, part_len);
        md5_transform(ctx->state, ctx->buffer);
        for (i = part_len; i + 63 < len; i += 64) {
            md5_transform(ctx->state, &data[i]);
        }
        index = 0;
    } else {
        i = 0;
    }
    memcpy(&ctx->buffer[index], &data[i], len - i);
}

void md5_final(md5_ctx_t *ctx, uint8_t digest[MD5_DIGEST_SIZE])
{
    uint8_t bits[8];
    uint32_t index, pad_len;
    int i;
    static const uint8_t padding[64] = { 0x80 };

    for (i = 0; i < 8; i++) {
        bits[i] = (uint8_t) ((ctx->count >> (i * 8)) & 0xFF);
    }

    index = (uint32_t) ((ctx->count >> 3) & 0x3f);
    pad_len = (index < 56) ? (56 - index) : (120 - index);
    md5_update(ctx, padding, pad_len);
    md5_update(ctx, bits, 8);

    for (i = 0; i < 4; i++) {
        digest[i * 4]     = (uint8_t) (ctx->state[i] & 0xFF);
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

#define SHA1_ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define SHA1_F0(b, c, d) (((b) & (c)) | ((~(b)) & (d)))
#define SHA1_F1(b, c, d) ((b) ^ (c) ^ (d))
#define SHA1_F2(b, c, d) (((b) & (c)) | ((b) & (d)) | ((c) & (d)))
#define SHA1_F3(b, c, d) ((b) ^ (c) ^ (d))

static void sha1_transform(uint32_t state[5], const uint8_t block[64])
{
    uint32_t a, b, c, d, e, temp;
    uint32_t w[80];
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t) block[i * 4] << 24) |
               ((uint32_t) block[i * 4 + 1] << 16) |
               ((uint32_t) block[i * 4 + 2] << 8) |
               ((uint32_t) block[i * 4 + 3]);
    }
    for (i = 16; i < 80; i++) {
        w[i] = SHA1_ROTATE_LEFT(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4];

    for (i = 0; i < 20; i++) {
        temp = SHA1_ROTATE_LEFT(a, 5) + SHA1_F0(b, c, d) + e + w[i] + 0x5A827999;
        e = d; d = c; c = SHA1_ROTATE_LEFT(b, 30); b = a; a = temp;
    }
    for (i = 20; i < 40; i++) {
        temp = SHA1_ROTATE_LEFT(a, 5) + SHA1_F1(b, c, d) + e + w[i] + 0x6ED9EBA1;
        e = d; d = c; c = SHA1_ROTATE_LEFT(b, 30); b = a; a = temp;
    }
    for (i = 40; i < 60; i++) {
        temp = SHA1_ROTATE_LEFT(a, 5) + SHA1_F2(b, c, d) + e + w[i] + 0x8F1BBCDC;
        e = d; d = c; c = SHA1_ROTATE_LEFT(b, 30); b = a; a = temp;
    }
    for (i = 60; i < 80; i++) {
        temp = SHA1_ROTATE_LEFT(a, 5) + SHA1_F3(b, c, d) + e + w[i] + 0xCA62C1D6;
        e = d; d = c; c = SHA1_ROTATE_LEFT(b, 30); b = a; a = temp;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

void sha1_init(sha1_ctx_t *ctx)
{
    ctx->count = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
}

void sha1_update(sha1_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t i, index, part_len;
    index = (uint32_t) ((ctx->count >> 3) & 0x3F);
    ctx->count += ((uint64_t) len) << 3;
    part_len = 64 - index;

    if (len >= part_len) {
        memcpy(&ctx->buffer[index], data, part_len);
        sha1_transform(ctx->state, ctx->buffer);
        for (i = part_len; i + 63 < len; i += 64) {
            sha1_transform(ctx->state, &data[i]);
        }
        index = 0;
    } else {
        i = 0;
    }
    memcpy(&ctx->buffer[index], &data[i], len - i);
}

void sha1_final(sha1_ctx_t *ctx, uint8_t digest[SHA1_DIGEST_SIZE])
{
    uint8_t bits[8];
    uint32_t index, pad_len;
    int i;
    static const uint8_t padding[64] = { 0x80 };

    for (i = 0; i < 8; i++) {
        bits[i] = (uint8_t) ((ctx->count >> (56 - i * 8)) & 0xFF);
    }

    index = (uint32_t) ((ctx->count >> 3) & 0x3f);
    pad_len = (index < 56) ? (56 - index) : (120 - index);
    sha1_update(ctx, padding, pad_len);
    sha1_update(ctx, bits, 8);

    for (i = 0; i < 5; i++) {
        digest[i * 4]     = (uint8_t) ((ctx->state[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = (uint8_t) ((ctx->state[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = (uint8_t) ((ctx->state[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = (uint8_t) (ctx->state[i] & 0xFF);
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
 *  SHA-256
 * ============================================================ */

#define SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA256_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_EP0(x) (SHA256_ROTR(x, 2) ^ SHA256_ROTR(x, 13) ^ SHA256_ROTR(x, 22))
#define SHA256_EP1(x) (SHA256_ROTR(x, 6) ^ SHA256_ROTR(x, 11) ^ SHA256_ROTR(x, 25))
#define SHA256_SIG0(x) (SHA256_ROTR(x, 7) ^ SHA256_ROTR(x, 18) ^ ((x) >> 3))
#define SHA256_SIG1(x) (SHA256_ROTR(x, 17) ^ SHA256_ROTR(x, 19) ^ ((x) >> 10))

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void sha256_transform(uint32_t state[8], const uint8_t block[64])
{
    uint32_t a, b, c, d, e, f, g, h, t1, t2;
    uint32_t w[64];
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t) block[i * 4] << 24) |
               ((uint32_t) block[i * 4 + 1] << 16) |
               ((uint32_t) block[i * 4 + 2] << 8) |
               ((uint32_t) block[i * 4 + 3]);
    }
    for (i = 16; i < 64; i++) {
        w[i] = SHA256_SIG1(w[i - 2]) + w[i - 7] + SHA256_SIG0(w[i - 15]) + w[i - 16];
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + SHA256_EP1(e) + SHA256_CH(e, f, g) + sha256_k[i] + w[i];
        t2 = SHA256_EP0(a) + SHA256_MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void sha256_init(sha256_ctx_t *ctx)
{
    ctx->count = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t i, index, part_len;
    index = (uint32_t) ((ctx->count >> 3) & 0x3F);
    ctx->count += ((uint64_t) len) << 3;
    part_len = 64 - index;

    if (len >= part_len) {
        memcpy(&ctx->buffer[index], data, part_len);
        sha256_transform(ctx->state, ctx->buffer);
        for (i = part_len; i + 63 < len; i += 64) {
            sha256_transform(ctx->state, &data[i]);
        }
        index = 0;
    } else {
        i = 0;
    }
    memcpy(&ctx->buffer[index], &data[i], len - i);
}

void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE])
{
    uint8_t bits[8];
    uint32_t index, pad_len;
    int i;
    static const uint8_t padding[64] = { 0x80 };

    for (i = 0; i < 8; i++) {
        bits[i] = (uint8_t) ((ctx->count >> (56 - i * 8)) & 0xFF);
    }

    index = (uint32_t) ((ctx->count >> 3) & 0x3f);
    pad_len = (index < 56) ? (56 - index) : (120 - index);
    sha256_update(ctx, padding, pad_len);
    sha256_update(ctx, bits, 8);

    for (i = 0; i < 8; i++) {
        digest[i * 4]     = (uint8_t) ((ctx->state[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = (uint8_t) ((ctx->state[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = (uint8_t) ((ctx->state[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = (uint8_t) (ctx->state[i] & 0xFF);
    }
}

void sha256(const uint8_t *data, uint32_t len, uint8_t digest[SHA256_DIGEST_SIZE])
{
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

/* ============================================================
 *  HMAC
 * ============================================================ */

void hmac_md5(const uint8_t *key, uint32_t key_len,
              const uint8_t *data, uint32_t data_len,
              uint8_t digest[MD5_DIGEST_SIZE])
{
    md5_ctx_t ctx;
    uint8_t k_ipad[64], k_opad[64];
    uint8_t tk[16];
    int i;

    if (key_len > 64) {
        md5(key, key_len, tk);
        key = tk;
        key_len = 16;
    }

    memset(k_ipad, 0, 64);
    memset(k_opad, 0, 64);
    memcpy(k_ipad, key, key_len);
    memcpy(k_opad, key, key_len);

    for (i = 0; i < 64; i++) {
        k_ipad[i] ^= 0x36;
        k_opad[i] ^= 0x5c;
    }

    md5_init(&ctx);
    md5_update(&ctx, k_ipad, 64);
    md5_update(&ctx, data, data_len);
    md5_final(&ctx, digest);

    md5_init(&ctx);
    md5_update(&ctx, k_opad, 64);
    md5_update(&ctx, digest, 16);
    md5_final(&ctx, digest);
}

void hmac_sha1(const uint8_t *key, uint32_t key_len,
               const uint8_t *data, uint32_t data_len,
               uint8_t digest[SHA1_DIGEST_SIZE])
{
    sha1_ctx_t ctx;
    uint8_t k_ipad[64], k_opad[64];
    uint8_t tk[20];
    int i;

    if (key_len > 64) {
        sha1(key, key_len, tk);
        key = tk;
        key_len = 20;
    }

    memset(k_ipad, 0, 64);
    memset(k_opad, 0, 64);
    memcpy(k_ipad, key, key_len);
    memcpy(k_opad, key, key_len);

    for (i = 0; i < 64; i++) {
        k_ipad[i] ^= 0x36;
        k_opad[i] ^= 0x5c;
    }

    sha1_init(&ctx);
    sha1_update(&ctx, k_ipad, 64);
    sha1_update(&ctx, data, data_len);
    sha1_final(&ctx, digest);

    sha1_init(&ctx);
    sha1_update(&ctx, k_opad, 64);
    sha1_update(&ctx, digest, 20);
    sha1_final(&ctx, digest);
}

void hmac_sha256(const uint8_t *key, uint32_t key_len,
                 const uint8_t *data, uint32_t data_len,
                 uint8_t digest[SHA256_DIGEST_SIZE])
{
    sha256_ctx_t ctx;
    uint8_t k_ipad[64], k_opad[64];
    uint8_t tk[32];
    int i;

    if (key_len > 64) {
        sha256(key, key_len, tk);
        key = tk;
        key_len = 32;
    }

    memset(k_ipad, 0, 64);
    memset(k_opad, 0, 64);
    memcpy(k_ipad, key, key_len);
    memcpy(k_opad, key, key_len);

    for (i = 0; i < 64; i++) {
        k_ipad[i] ^= 0x36;
        k_opad[i] ^= 0x5c;
    }

    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, 64);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, digest);

    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, 64);
    sha256_update(&ctx, digest, 32);
    sha256_final(&ctx, digest);
}
