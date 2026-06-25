#ifndef _RSA_H_
#define _RSA_H_

#include "stdbool.h"
#include "stdint.h"

#define RSA_MAX_MODULUS_BITS 2048
#define RSA_MAX_MODULUS_BYTES (RSA_MAX_MODULUS_BITS / 8)
#define RSA_MAX_MODULUS_WORDS (RSA_MAX_MODULUS_BITS / 32)

/* Big integer (little-endian words) */
typedef struct {
    uint32_t words[RSA_MAX_MODULUS_WORDS * 2];
    uint32_t length;  /* number of 32-bit words used */
} bignum_t;

/* RSA public key */
typedef struct {
    bignum_t n;  /* modulus */
    bignum_t e;  /* public exponent */
    uint32_t bits;
} rsa_pubkey_t;

/* RSA private key (for completeness, though we mostly need public) */
typedef struct {
    bignum_t n;  /* modulus */
    bignum_t e;  /* public exponent */
    bignum_t d;  /* private exponent */
    bignum_t p;  /* prime p */
    bignum_t q;  /* prime q */
    bignum_t dp;
    bignum_t dq;
    bignum_t qinv;
    uint32_t bits;
} rsa_privkey_t;

/* Big integer operations */
void bignum_init(bignum_t *a);
void bignum_from_bytes(bignum_t *a, const uint8_t *bytes, uint32_t len);
void bignum_to_bytes(const bignum_t *a, uint8_t *bytes, uint32_t len);
int32_t bignum_cmp(const bignum_t *a, const bignum_t *b);
void bignum_add(bignum_t *result, const bignum_t *a, const bignum_t *b);
void bignum_sub(bignum_t *result, const bignum_t *a, const bignum_t *b);
void bignum_mul(bignum_t *result, const bignum_t *a, const bignum_t *b);
void bignum_mod(bignum_t *result, const bignum_t *a, const bignum_t *m);
void bignum_modmul(bignum_t *result, const bignum_t *a, const bignum_t *b, const bignum_t *m);
void bignum_modpow(bignum_t *result, const bignum_t *base, const bignum_t *exp, const bignum_t *m);

/* RSA operations */
int32_t rsa_pubkey_init(rsa_pubkey_t *key, const uint8_t *modulus, uint32_t mod_len,
                        const uint8_t *exponent, uint32_t exp_len);
int32_t rsa_public_encrypt(const rsa_pubkey_t *key, const uint8_t *input, uint32_t input_len,
                           uint8_t *output, uint32_t output_len);
int32_t rsa_public_decrypt(const rsa_pubkey_t *key, const uint8_t *input, uint32_t input_len,
                           uint8_t *output, uint32_t output_len);

/* PKCS#1 v1.5 signature verification */
int32_t rsa_verify_pkcs1_v15(const rsa_pubkey_t *key,
                             const uint8_t *signature, uint32_t sig_len,
                             const uint8_t *digest, uint32_t digest_len,
                             int32_t hash_type);

/* Hash type IDs for PKCS#1 */
#define RSA_HASH_MD5     1
#define RSA_HASH_SHA1    2
#define RSA_HASH_SHA256  3

#endif
