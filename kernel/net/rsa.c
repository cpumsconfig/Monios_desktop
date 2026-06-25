#include "rsa.h"
#include "string.h"
#include "hash.h"
#include "stddef.h"

/* ============================================================
 *  Big integer operations (little-endian words)
 * ============================================================ */

void bignum_init(bignum_t *a)
{
    memset(a->words, 0, sizeof(a->words));
    a->length = 1;
}

void bignum_from_bytes(bignum_t *a, const uint8_t *bytes, uint32_t len)
{
    uint32_t i, j;
    bignum_init(a);

    if (len == 0 || bytes == NULL) {
        return;
    }

    /* Skip leading zeros */
    while (len > 0 && bytes[0] == 0) {
        bytes++;
        len--;
    }

    if (len == 0) {
        return;
    }

    a->length = (len + 3) / 4;
    if (a->length > RSA_MAX_MODULUS_WORDS * 2) {
        a->length = RSA_MAX_MODULUS_WORDS * 2;
    }

    /* Convert from big-endian bytes to little-endian words */
    for (i = 0; i < a->length; i++) {
        uint32_t word = 0;
        for (j = 0; j < 4; j++) {
            int byte_idx = (int) len - 1 - (int) (i * 4 + j);
            if (byte_idx >= 0) {
                word |= ((uint32_t) bytes[byte_idx]) << (j * 8);
            }
        }
        a->words[i] = word;
    }

    /* Trim leading zeros */
    while (a->length > 1 && a->words[a->length - 1] == 0) {
        a->length--;
    }
}

void bignum_to_bytes(const bignum_t *a, uint8_t *bytes, uint32_t len)
{
    uint32_t i, j;
    uint32_t byte_len = a->length * 4;

    memset(bytes, 0, len);

    if (byte_len > len) {
        byte_len = len;
    }

    /* Convert from little-endian words to big-endian bytes */
    for (i = 0; i < a->length && i * 4 < len; i++) {
        uint32_t word = a->words[i];
        for (j = 0; j < 4 && i * 4 + j < len; j++) {
            int byte_idx = (int) len - 1 - (int) (i * 4 + j);
            if (byte_idx >= 0) {
                bytes[byte_idx] = (uint8_t) ((word >> (j * 8)) & 0xFF);
            }
        }
    }
}

int32_t bignum_cmp(const bignum_t *a, const bignum_t *b)
{
    uint32_t i;

    if (a->length != b->length) {
        return a->length > b->length ? 1 : -1;
    }

    for (i = a->length; i > 0; i--) {
        if (a->words[i - 1] != b->words[i - 1]) {
            return a->words[i - 1] > b->words[i - 1] ? 1 : -1;
        }
    }

    return 0;
}

void bignum_add(bignum_t *result, const bignum_t *a, const bignum_t *b)
{
    uint32_t i;
    uint64_t carry = 0;
    uint32_t max_len = a->length > b->length ? a->length : b->length;

    for (i = 0; i < max_len; i++) {
        uint64_t sum = carry;
        if (i < a->length) sum += a->words[i];
        if (i < b->length) sum += b->words[i];
        result->words[i] = (uint32_t) sum;
        carry = sum >> 32;
    }

    if (carry && i < RSA_MAX_MODULUS_WORDS * 2) {
        result->words[i] = (uint32_t) carry;
        result->length = i + 1;
    } else {
        result->length = i;
    }
}

void bignum_sub(bignum_t *result, const bignum_t *a, const bignum_t *b)
{
    uint32_t i;
    int64_t borrow = 0;

    for (i = 0; i < a->length; i++) {
        int64_t diff = (int64_t) a->words[i] - borrow;
        if (i < b->length) {
            diff -= b->words[i];
        }
        if (diff < 0) {
            diff += 0x100000000LL;
            borrow = 1;
        } else {
            borrow = 0;
        }
        result->words[i] = (uint32_t) diff;
    }

    result->length = a->length;
    while (result->length > 1 && result->words[result->length - 1] == 0) {
        result->length--;
    }
}

void bignum_mul(bignum_t *result, const bignum_t *a, const bignum_t *b)
{
    uint32_t i, j;
    uint64_t carry;

    bignum_init(result);
    result->length = a->length + b->length;
    if (result->length > RSA_MAX_MODULUS_WORDS * 2) {
        result->length = RSA_MAX_MODULUS_WORDS * 2;
    }

    for (i = 0; i < a->length; i++) {
        carry = 0;
        for (j = 0; j < b->length && i + j < RSA_MAX_MODULUS_WORDS * 2; j++) {
            uint64_t product = (uint64_t) a->words[i] * b->words[j];
            uint64_t sum = product + result->words[i + j] + carry;
            result->words[i + j] = (uint32_t) sum;
            carry = sum >> 32;
        }
        if (i + j < RSA_MAX_MODULUS_WORDS * 2) {
            result->words[i + j] = (uint32_t) carry;
        }
    }

    while (result->length > 1 && result->words[result->length - 1] == 0) {
        result->length--;
    }
}

void bignum_mod(bignum_t *result, const bignum_t *a, const bignum_t *m)
{
    bignum_t temp;
    uint32_t i;

    /* Simple subtraction-based mod (slow but works for our purposes) */
    bignum_init(result);
    memcpy(result->words, a->words, a->length * 4);
    result->length = a->length;

    while (bignum_cmp(result, m) >= 0) {
        bignum_sub(&temp, result, m);
        memcpy(result->words, temp.words, temp.length * 4);
        result->length = temp.length;
    }
}

void bignum_modmul(bignum_t *result, const bignum_t *a, const bignum_t *b, const bignum_t *m)
{
    bignum_t product;
    bignum_mul(&product, a, b);
    bignum_mod(result, &product, m);
}

void bignum_modpow(bignum_t *result, const bignum_t *base, const bignum_t *exp, const bignum_t *m)
{
    bignum_t base_copy;
    bignum_t exp_copy;
    bignum_t temp;

    bignum_init(result);
    result->words[0] = 1;
    result->length = 1;

    memcpy(&base_copy, base, sizeof(bignum_t));
    memcpy(&exp_copy, exp, sizeof(bignum_t));

    bignum_mod(&base_copy, &base_copy, m);

    while (exp_copy.length > 0) {
        if (exp_copy.words[0] & 1) {
            bignum_modmul(&temp, result, &base_copy, m);
            memcpy(result->words, temp.words, temp.length * 4);
            result->length = temp.length;
        }

        /* exp >>= 1 */
        {
            uint32_t i;
            uint32_t carry = 0;
            for (i = exp_copy.length; i > 0; i--) {
                uint32_t new_carry = exp_copy.words[i - 1] & 1;
                exp_copy.words[i - 1] = (exp_copy.words[i - 1] >> 1) | (carry << 31);
                carry = new_carry;
            }
            while (exp_copy.length > 1 && exp_copy.words[exp_copy.length - 1] == 0) {
                exp_copy.length--;
            }
        }

        bignum_modmul(&temp, &base_copy, &base_copy, m);
        memcpy(&base_copy, &temp, sizeof(bignum_t));
    }
}

/* ============================================================
 *  RSA operations
 * ============================================================ */

int32_t rsa_pubkey_init(rsa_pubkey_t *key, const uint8_t *modulus, uint32_t mod_len,
                        const uint8_t *exponent, uint32_t exp_len)
{
    if (key == NULL || modulus == NULL || exponent == NULL) {
        return -1;
    }

    bignum_init(&key->n);
    bignum_init(&key->e);
    bignum_from_bytes(&key->n, modulus, mod_len);
    bignum_from_bytes(&key->e, exponent, exp_len);
    key->bits = mod_len * 8;

    return 0;
}

int32_t rsa_public_encrypt(const rsa_pubkey_t *key, const uint8_t *input, uint32_t input_len,
                           uint8_t *output, uint32_t output_len)
{
    bignum_t m, c;

    if (key == NULL || input == NULL || output == NULL) {
        return -1;
    }
    if (input_len > key->bits / 8) {
        return -1;
    }
    if (output_len < key->bits / 8) {
        return -1;
    }

    bignum_from_bytes(&m, input, input_len);
    bignum_modpow(&c, &m, &key->e, &key->n);
    bignum_to_bytes(&c, output, key->bits / 8);

    return key->bits / 8;
}

int32_t rsa_public_decrypt(const rsa_pubkey_t *key, const uint8_t *input, uint32_t input_len,
                           uint8_t *output, uint32_t output_len)
{
    bignum_t c, m;

    if (key == NULL || input == NULL || output == NULL) {
        return -1;
    }
    if (input_len != key->bits / 8) {
        return -1;
    }
    if (output_len < key->bits / 8) {
        return -1;
    }

    bignum_from_bytes(&c, input, input_len);
    bignum_modpow(&m, &c, &key->e, &key->n);
    bignum_to_bytes(&m, output, key->bits / 8);

    return key->bits / 8;
}

/* PKCS#1 v1.5 DigestInfo prefixes */
static const uint8_t rsa_md5_prefix[] = {
    0x30, 0x20, 0x30, 0x0c, 0x06, 0x08, 0x2a, 0x86,
    0x48, 0x86, 0xf7, 0x0d, 0x02, 0x05, 0x05, 0x00,
    0x04, 0x10
};

static const uint8_t rsa_sha1_prefix[] = {
    0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2b, 0x0e,
    0x03, 0x02, 0x1a, 0x05, 0x00, 0x04, 0x14
};

static const uint8_t rsa_sha256_prefix[] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86,
    0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05,
    0x00, 0x04, 0x20
};

int32_t rsa_verify_pkcs1_v15(const rsa_pubkey_t *key,
                             const uint8_t *signature, uint32_t sig_len,
                             const uint8_t *digest, uint32_t digest_len,
                             int32_t hash_type)
{
    uint8_t decrypted[RSA_MAX_MODULUS_BYTES];
    int32_t dec_len;
    const uint8_t *prefix;
    uint32_t prefix_len;
    uint32_t i, ps_len;

    if (key == NULL || signature == NULL || digest == NULL) {
        return -1;
    }
    if (sig_len != key->bits / 8) {
        return -1;
    }

    /* Select prefix */
    switch (hash_type) {
        case RSA_HASH_MD5:
            prefix = rsa_md5_prefix;
            prefix_len = sizeof(rsa_md5_prefix);
            if (digest_len != MD5_DIGEST_SIZE) return -1;
            break;
        case RSA_HASH_SHA1:
            prefix = rsa_sha1_prefix;
            prefix_len = sizeof(rsa_sha1_prefix);
            if (digest_len != SHA1_DIGEST_SIZE) return -1;
            break;
        case RSA_HASH_SHA256:
            prefix = rsa_sha256_prefix;
            prefix_len = sizeof(rsa_sha256_prefix);
            if (digest_len != SHA256_DIGEST_SIZE) return -1;
            break;
        default:
            return -1;
    }

    /* Decrypt signature with public key */
    dec_len = rsa_public_decrypt(key, signature, sig_len, decrypted, sizeof(decrypted));
    if (dec_len < 0) {
        return -1;
    }

    /* Check PKCS#1 v1.5 padding */
    /* Format: 0x00 0x01 PS 0x00 TLV */
    if (decrypted[0] != 0x00 || decrypted[1] != 0x01) {
        return -1;
    }

    /* Find the 0x00 separator after PS */
    ps_len = 0;
    for (i = 2; i < (uint32_t) dec_len; i++) {
        if (decrypted[i] == 0x00) {
            break;
        }
        if (decrypted[i] != 0xFF) {
            return -1;
        }
        ps_len++;
    }

    if (i >= (uint32_t) dec_len) {
        return -1;
    }
    i++; /* skip the 0x00 */

    /* Check minimum PS length (at least 8 bytes) */
    if (ps_len < 8) {
        return -1;
    }

    /* Verify DigestInfo */
    if (i + prefix_len + digest_len > (uint32_t) dec_len) {
        return -1;
    }

    /* Compare prefix */
    if (memcmp(&decrypted[i], prefix, prefix_len) != 0) {
        return -1;
    }
    i += prefix_len;

    /* Compare digest */
    if (memcmp(&decrypted[i], digest, digest_len) != 0) {
        return -1;
    }

    return 0; /* success */
}
