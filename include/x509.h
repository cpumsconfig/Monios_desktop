#ifndef _X509_H_
#define _X509_H_

#include "stdbool.h"
#include "stdint.h"
#include "rsa.h"

#define X509_MAX_CERT_SIZE 4096
#define X509_MAX_CHAIN_DEPTH 8
#define X509_MAX_NAME_ENTRIES 16
#define X509_MAX_EXTENSIONS 8

/* ASN.1 tags */
#define ASN1_TAG_BOOLEAN     0x01
#define ASN1_TAG_INTEGER     0x02
#define ASN1_TAG_BIT_STRING  0x03
#define ASN1_TAG_OCTET_STRING 0x04
#define ASN1_TAG_NULL        0x05
#define ASN1_TAG_OID         0x06
#define ASN1_TAG_SEQUENCE    0x30
#define ASN1_TAG_SET         0x31
#define ASN1_TAG_UTF8_STRING 0x0C
#define ASN1_TAG_PRINTABLE_STRING 0x13
#define ASN1_TAG_IA5_STRING  0x16
#define ASN1_TAG_UTCTIME     0x17
#define ASN1_TAG_GENERALIZED_TIME 0x18
#define ASN1_TAG_CONSTRUCTED 0x20
#define ASN1_TAG_CONTEXT_SPECIFIC 0x80

/* X.509 name entry */
typedef struct {
    uint8_t oid[32];
    uint32_t oid_len;
    char value[128];
    uint32_t value_len;
} x509_name_entry_t;

/* X.509 name (issuer/subject) */
typedef struct {
    x509_name_entry_t entries[X509_MAX_NAME_ENTRIES];
    uint32_t count;
} x509_name_t;

/* X.509 validity */
typedef struct {
    uint64_t not_before;
    uint64_t not_after;
} x509_validity_t;

/* X.509 certificate */
typedef struct {
    uint8_t raw[X509_MAX_CERT_SIZE];
    uint32_t raw_len;

    uint8_t tbs[X509_MAX_CERT_SIZE];
    uint32_t tbs_len;

    int32_t version;
    uint8_t serial_number[64];
    uint32_t serial_len;

    uint8_t signature_oid[32];
    uint32_t signature_oid_len;

    x509_name_t issuer;
    x509_validity_t validity;
    x509_name_t subject;

    uint8_t pubkey_oid[32];
    uint32_t pubkey_oid_len;

    uint8_t pubkey_modulus[RSA_MAX_MODULUS_BYTES];
    uint32_t pubkey_modulus_len;
    uint8_t pubkey_exponent[16];
    uint32_t pubkey_exponent_len;

    uint8_t signature_algo_oid[32];
    uint32_t signature_algo_oid_len;

    uint8_t signature[RSA_MAX_MODULUS_BYTES];
    uint32_t signature_len;

    bool is_ca;
    int32_t path_len_constraint;

    rsa_pubkey_t rsa_key;
    bool rsa_key_ready;
} x509_cert_t;

/* Certificate chain */
typedef struct {
    x509_cert_t certs[X509_MAX_CHAIN_DEPTH];
    uint32_t count;
} x509_chain_t;

/* Trusted root certificates */
typedef struct {
    x509_cert_t roots[16];
    uint32_t count;
} x509_trust_store_t;

/* ASN.1 parsing */
int32_t asn1_read_length(const uint8_t *data, uint32_t len, uint32_t *out_len, uint32_t *bytes_read);
int32_t asn1_parse_sequence(const uint8_t *data, uint32_t len, const uint8_t **out_content, uint32_t *out_len);
int32_t asn1_parse_integer(const uint8_t *data, uint32_t len, const uint8_t **out_content, uint32_t *out_len);
int32_t asn1_parse_bit_string(const uint8_t *data, uint32_t len, const uint8_t **out_content, uint32_t *out_len);
int32_t asn1_parse_oid(const uint8_t *data, uint32_t len, const uint8_t **out_oid, uint32_t *out_len);
int32_t asn1_parse_string(const uint8_t *data, uint32_t len, char *out_str, uint32_t max_len);

/* X.509 certificate parsing */
int32_t x509_parse_cert(x509_cert_t *cert, const uint8_t *data, uint32_t len);
int32_t x509_parse_name(x509_name_t *name, const uint8_t *data, uint32_t len);
int32_t x509_parse_validity(x509_validity_t *validity, const uint8_t *data, uint32_t len);
int32_t x509_parse_spki(x509_cert_t *cert, const uint8_t *data, uint32_t len);

/* Certificate verification */
int32_t x509_verify_signature(const x509_cert_t *cert, const x509_cert_t *issuer_cert);
int32_t x509_verify_chain(const x509_chain_t *chain, const x509_trust_store_t *trust_store);
int32_t x509_check_name_match(const x509_name_t *a, const x509_name_t *b);
int32_t x509_check_validity(const x509_cert_t *cert, uint64_t current_time);

/* Trust store */
void x509_init_trust_store(x509_trust_store_t *store);
int32_t x509_add_trusted_root(x509_trust_store_t *store, const uint8_t *data, uint32_t len);
int32_t x509_find_issuer(const x509_trust_store_t *store, const x509_name_t *issuer, x509_cert_t *out_cert);

/* Utility */
int32_t x509_get_common_name(const x509_name_t *name, char *out, uint32_t max_len);
uint64_t x509_parse_time(const uint8_t *data, uint32_t len, bool utc);

#endif
