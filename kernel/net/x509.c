#include "x509.h"
#include "string.h"
#include "hash.h"
#include "stddef.h"

/* OID definitions */
static const uint8_t oid_rsa_encryption[] = { 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01 };
#define OID_RSA_ENCRYPTION_LEN 9

static const uint8_t oid_md5_with_rsa[] = { 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x04 };
#define OID_MD5_WITH_RSA_LEN 9

static const uint8_t oid_sha1_with_rsa[] = { 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x05 };
#define OID_SHA1_WITH_RSA_LEN 9

static const uint8_t oid_sha256_with_rsa[] = { 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0B };
#define OID_SHA256_WITH_RSA_LEN 9

static const uint8_t oid_common_name[] = { 0x55, 0x04, 0x03 };
#define OID_COMMON_NAME_LEN 3

static const uint8_t oid_basic_constraints[] = { 0x55, 0x1D, 0x13 };
#define OID_BASIC_CONSTRAINTS_LEN 3

/* ============================================================
 *  ASN.1 parsing
 * ============================================================ */

int32_t asn1_read_length(const uint8_t *data, uint32_t len, uint32_t *out_len, uint32_t *bytes_read)
{
    if (len < 2) {
        return -1;
    }

    if ((data[1] & 0x80) == 0) {
        /* Short form */
        *out_len = data[1];
        *bytes_read = 2;
        return 0;
    }

    /* Long form */
    uint32_t num_bytes = data[1] & 0x7F;
    if (num_bytes == 0 || num_bytes > 4) {
        return -1;
    }
    if (len < 2 + num_bytes) {
        return -1;
    }

    *out_len = 0;
    for (uint32_t i = 0; i < num_bytes; i++) {
        *out_len = (*out_len << 8) | data[2 + i];
    }
    *bytes_read = 2 + num_bytes;
    return 0;
}

int32_t asn1_parse_sequence(const uint8_t *data, uint32_t len, const uint8_t **out_content, uint32_t *out_len)
{
    if (len < 2 || data[0] != ASN1_TAG_SEQUENCE) {
        return -1;
    }

    uint32_t content_len, header_len;
    if (asn1_read_length(data, len, &content_len, &header_len) != 0) {
        return -1;
    }

    if (header_len + content_len > len) {
        return -1;
    }

    *out_content = &data[header_len];
    *out_len = content_len;
    return 0;
}

int32_t asn1_parse_integer(const uint8_t *data, uint32_t len, const uint8_t **out_content, uint32_t *out_len)
{
    if (len < 2 || data[0] != ASN1_TAG_INTEGER) {
        return -1;
    }

    uint32_t content_len, header_len;
    if (asn1_read_length(data, len, &content_len, &header_len) != 0) {
        return -1;
    }

    if (header_len + content_len > len) {
        return -1;
    }

    *out_content = &data[header_len];
    *out_len = content_len;
    return 0;
}

int32_t asn1_parse_bit_string(const uint8_t *data, uint32_t len, const uint8_t **out_content, uint32_t *out_len)
{
    if (len < 3 || data[0] != ASN1_TAG_BIT_STRING) {
        return -1;
    }

    uint32_t content_len, header_len;
    if (asn1_read_length(data, len, &content_len, &header_len) != 0) {
        return -1;
    }

    if (header_len + content_len > len || content_len < 1) {
        return -1;
    }

    /* First byte is unused bits count */
    *out_content = &data[header_len + 1];
    *out_len = content_len - 1;
    return 0;
}

int32_t asn1_parse_oid(const uint8_t *data, uint32_t len, const uint8_t **out_oid, uint32_t *out_len)
{
    if (len < 2 || data[0] != ASN1_TAG_OID) {
        return -1;
    }

    uint32_t content_len, header_len;
    if (asn1_read_length(data, len, &content_len, &header_len) != 0) {
        return -1;
    }

    if (header_len + content_len > len) {
        return -1;
    }

    *out_oid = &data[header_len];
    *out_len = content_len;
    return 0;
}

int32_t asn1_parse_string(const uint8_t *data, uint32_t len, char *out_str, uint32_t max_len)
{
    if (len < 2) {
        return -1;
    }

    uint8_t tag = data[0];
    if (tag != ASN1_TAG_UTF8_STRING && tag != ASN1_TAG_PRINTABLE_STRING &&
        tag != ASN1_TAG_IA5_STRING && tag != 0x14 /* T61String */) {
        return -1;
    }

    uint32_t content_len, header_len;
    if (asn1_read_length(data, len, &content_len, &header_len) != 0) {
        return -1;
    }

    if (header_len + content_len > len) {
        return -1;
    }

    uint32_t copy_len = content_len;
    if (copy_len >= max_len) {
        copy_len = max_len - 1;
    }

    memcpy(out_str, &data[header_len], copy_len);
    out_str[copy_len] = '\0';
    return copy_len;
}

/* ============================================================
 *  X.509 name parsing
 * ============================================================ */

int32_t x509_parse_name(x509_name_t *name, const uint8_t *data, uint32_t len)
{
    const uint8_t *content;
    uint32_t content_len;
    uint32_t offset = 0;

    memset(name, 0, sizeof(x509_name_t));

    if (asn1_parse_sequence(data, len, &content, &content_len) != 0) {
        return -1;
    }

    while (offset < content_len && name->count < X509_MAX_NAME_ENTRIES) {
        const uint8_t *set_content;
        uint32_t set_len;
        const uint8_t *seq_content;
        uint32_t seq_len;

        if (content[offset] != ASN1_TAG_SET) {
            break;
        }

        if (asn1_parse_sequence(&content[offset], content_len - offset, &set_content, &set_len) != 0) {
            break;
        }

        if (asn1_parse_sequence(set_content, set_len, &seq_content, &seq_len) != 0) {
            break;
        }

        /* Parse OID */
        const uint8_t *oid;
        uint32_t oid_len;
        if (asn1_parse_oid(seq_content, seq_len, &oid, &oid_len) != 0) {
            break;
        }

        if (oid_len > sizeof(name->entries[name->count].oid)) {
            oid_len = sizeof(name->entries[name->count].oid);
        }
        memcpy(name->entries[name->count].oid, oid, oid_len);
        name->entries[name->count].oid_len = oid_len;

        /* Skip OID TLV */
        uint32_t oid_header_len;
        uint32_t oid_content_len;
        asn1_read_length(seq_content, seq_len, &oid_content_len, &oid_header_len);
        uint32_t value_offset = oid_header_len + oid_content_len;

        /* Parse value */
        int32_t value_len = asn1_parse_string(&seq_content[value_offset], seq_len - value_offset,
                                              name->entries[name->count].value,
                                              sizeof(name->entries[name->count].value));
        if (value_len < 0) {
            value_len = 0;
        }
        name->entries[name->count].value_len = value_len;

        name->count++;
        offset += 2 + set_len; /* approximate, will fix */

        /* Calculate actual offset */
        uint32_t set_header_len;
        uint32_t set_content_len2;
        asn1_read_length(&content[offset], content_len - offset, &set_content_len2, &set_header_len);
        offset = offset + set_header_len + set_content_len2;
        break; /* For simplicity, just parse first RDN for now */
    }

    return 0;
}

/* ============================================================
 *  X.509 time parsing
 * ============================================================ */

uint64_t x509_parse_time(const uint8_t *data, uint32_t len, bool utc)
{
    /* Simplified time parsing - returns approximate Unix time */
    /* UTCTime: YYMMDDHHMMSSZ */
    /* GeneralizedTime: YYYYMMDDHHMMSSZ */
    uint32_t year, month, day, hour, min, sec;
    uint32_t offset = 0;

    if (len < 12) {
        return 0;
    }

    if (utc) {
        year = (data[0] - '0') * 10 + (data[1] - '0');
        if (year >= 50) year += 1900;
        else year += 2000;
        offset = 2;
    } else {
        if (len < 14) return 0;
        year = (data[0] - '0') * 1000 + (data[1] - '0') * 100 +
               (data[2] - '0') * 10 + (data[3] - '0');
        offset = 4;
    }

    month = (data[offset] - '0') * 10 + (data[offset + 1] - '0');
    day = (data[offset + 2] - '0') * 10 + (data[offset + 3] - '0');
    hour = (data[offset + 4] - '0') * 10 + (data[offset + 5] - '0');
    min = (data[offset + 6] - '0') * 10 + (data[offset + 7] - '0');
    sec = (data[offset + 8] - '0') * 10 + (data[offset + 9] - '0');

    /* Very simplified: just return a rough value */
    return (uint64_t) year * 31536000ULL + (uint64_t) month * 2592000ULL +
           (uint64_t) day * 86400ULL + (uint64_t) hour * 3600ULL +
           (uint64_t) min * 60ULL + sec;
}

int32_t x509_parse_validity(x509_validity_t *validity, const uint8_t *data, uint32_t len)
{
    const uint8_t *content;
    uint32_t content_len;

    if (asn1_parse_sequence(data, len, &content, &content_len) != 0) {
        return -1;
    }

    /* notBefore */
    uint32_t offset = 0;
    bool utc = (content[0] == ASN1_TAG_UTCTIME);
    uint32_t nb_len;
    uint32_t nb_header;
    asn1_read_length(content, content_len, &nb_len, &nb_header);
    validity->not_before = x509_parse_time(&content[nb_header], nb_len, utc);

    offset = nb_header + nb_len;

    /* notAfter */
    utc = (content[offset] == ASN1_TAG_UTCTIME);
    uint32_t na_len;
    uint32_t na_header;
    asn1_read_length(&content[offset], content_len - offset, &na_len, &na_header);
    validity->not_after = x509_parse_time(&content[offset + na_header], na_len, utc);

    return 0;
}

/* ============================================================
 *  X.509 SPKI parsing
 * ============================================================ */

int32_t x509_parse_spki(x509_cert_t *cert, const uint8_t *data, uint32_t len)
{
    const uint8_t *content;
    uint32_t content_len;

    if (asn1_parse_sequence(data, len, &content, &content_len) != 0) {
        return -1;
    }

    /* AlgorithmIdentifier */
    const uint8_t *algo_content;
    uint32_t algo_len;
    if (asn1_parse_sequence(content, content_len, &algo_content, &algo_len) != 0) {
        return -1;
    }

    /* Algorithm OID */
    const uint8_t *oid;
    uint32_t oid_len;
    if (asn1_parse_oid(algo_content, algo_len, &oid, &oid_len) != 0) {
        return -1;
    }

    if (oid_len > sizeof(cert->pubkey_oid)) {
        oid_len = sizeof(cert->pubkey_oid);
    }
    memcpy(cert->pubkey_oid, oid, oid_len);
    cert->pubkey_oid_len = oid_len;

    /* Skip algorithm identifier */
    uint32_t algo_header;
    uint32_t algo_content_len;
    asn1_read_length(content, content_len, &algo_content_len, &algo_header);
    uint32_t offset = algo_header + algo_content_len;

    /* subjectPublicKey BIT STRING */
    const uint8_t *bit_string;
    uint32_t bit_len;
    if (asn1_parse_bit_string(&content[offset], content_len - offset, &bit_string, &bit_len) != 0) {
        return -1;
    }

    /* For RSA keys, the bit string contains a DER-encoded RSAPublicKey */
    if (oid_len == OID_RSA_ENCRYPTION_LEN &&
        memcmp(oid, oid_rsa_encryption, OID_RSA_ENCRYPTION_LEN) == 0) {

        const uint8_t *rsa_content;
        uint32_t rsa_len;
        if (asn1_parse_sequence(bit_string, bit_len, &rsa_content, &rsa_len) != 0) {
            return -1;
        }

        /* modulus */
        const uint8_t *mod;
        uint32_t mod_len;
        if (asn1_parse_integer(rsa_content, rsa_len, &mod, &mod_len) != 0) {
            return -1;
        }

        if (mod_len > sizeof(cert->pubkey_modulus)) {
            mod_len = sizeof(cert->pubkey_modulus);
        }
        memcpy(cert->pubkey_modulus, mod, mod_len);
        cert->pubkey_modulus_len = mod_len;

        /* Skip modulus */
        uint32_t mod_header;
        uint32_t mod_content_len;
        asn1_read_length(rsa_content, rsa_len, &mod_content_len, &mod_header);
        uint32_t rsa_offset = mod_header + mod_content_len;

        /* exponent */
        const uint8_t *exp;
        uint32_t exp_len;
        if (asn1_parse_integer(&rsa_content[rsa_offset], rsa_len - rsa_offset, &exp, &exp_len) != 0) {
            return -1;
        }

        if (exp_len > sizeof(cert->pubkey_exponent)) {
            exp_len = sizeof(cert->pubkey_exponent);
        }
        memcpy(cert->pubkey_exponent, exp, exp_len);
        cert->pubkey_exponent_len = exp_len;

        /* Initialize RSA key */
        rsa_pubkey_init(&cert->rsa_key,
                        cert->pubkey_modulus, cert->pubkey_modulus_len,
                        cert->pubkey_exponent, cert->pubkey_exponent_len);
        cert->rsa_key_ready = true;
    }

    return 0;
}

/* ============================================================
 *  X.509 certificate parsing
 * ============================================================ */

int32_t x509_parse_cert(x509_cert_t *cert, const uint8_t *data, uint32_t len)
{
    const uint8_t *content;
    uint32_t content_len;

    memset(cert, 0, sizeof(x509_cert_t));

    if (len > X509_MAX_CERT_SIZE) {
        return -1;
    }

    memcpy(cert->raw, data, len);
    cert->raw_len = len;

    /* Certificate SEQUENCE */
    if (asn1_parse_sequence(data, len, &content, &content_len) != 0) {
        return -1;
    }

    uint32_t offset = 0;

    /* tbsCertificate */
    const uint8_t *tbs_content;
    uint32_t tbs_len;
    if (asn1_parse_sequence(content, content_len, &tbs_content, &tbs_len) != 0) {
        return -1;
    }

    /* Save TBS for signature verification */
    if (tbs_len > sizeof(cert->tbs)) {
        return -1;
    }
    memcpy(cert->tbs, tbs_content, tbs_len);
    cert->tbs_len = tbs_len;

    uint32_t tbs_offset = 0;

    /* version (optional, context-specific [0]) */
    cert->version = 1; /* default v1 */
    if (tbs_content[tbs_offset] == (ASN1_TAG_CONTEXT_SPECIFIC | ASN1_TAG_CONSTRUCTED | 0)) {
        const uint8_t *ver_content;
        uint32_t ver_len;
        if (asn1_parse_sequence(&tbs_content[tbs_offset], tbs_len - tbs_offset, &ver_content, &ver_len) == 0) {
            const uint8_t *ver_int;
            uint32_t ver_int_len;
            if (asn1_parse_integer(ver_content, ver_len, &ver_int, &ver_int_len) == 0 && ver_int_len > 0) {
                cert->version = ver_int[0] + 1;
            }
        }
        uint32_t ver_header;
        uint32_t ver_content_len;
        asn1_read_length(&tbs_content[tbs_offset], tbs_len - tbs_offset, &ver_content_len, &ver_header);
        tbs_offset += ver_header + ver_content_len;
    }

    /* serialNumber */
    const uint8_t *serial;
    uint32_t serial_len;
    if (asn1_parse_integer(&tbs_content[tbs_offset], tbs_len - tbs_offset, &serial, &serial_len) != 0) {
        return -1;
    }
    if (serial_len > sizeof(cert->serial_number)) {
        serial_len = sizeof(cert->serial_number);
    }
    memcpy(cert->serial_number, serial, serial_len);
    cert->serial_len = serial_len;

    uint32_t serial_header;
    uint32_t serial_content_len;
    asn1_read_length(&tbs_content[tbs_offset], tbs_len - tbs_offset, &serial_content_len, &serial_header);
    tbs_offset += serial_header + serial_content_len;

    /* signature (AlgorithmIdentifier) */
    const uint8_t *sig_algo_content;
    uint32_t sig_algo_len;
    if (asn1_parse_sequence(&tbs_content[tbs_offset], tbs_len - tbs_offset, &sig_algo_content, &sig_algo_len) != 0) {
        return -1;
    }

    const uint8_t *sig_oid;
    uint32_t sig_oid_len;
    if (asn1_parse_oid(sig_algo_content, sig_algo_len, &sig_oid, &sig_oid_len) == 0) {
        if (sig_oid_len > sizeof(cert->signature_oid)) {
            sig_oid_len = sizeof(cert->signature_oid);
        }
        memcpy(cert->signature_oid, sig_oid, sig_oid_len);
        cert->signature_oid_len = sig_oid_len;
    }

    uint32_t sig_algo_header;
    uint32_t sig_algo_content_len;
    asn1_read_length(&tbs_content[tbs_offset], tbs_len - tbs_offset, &sig_algo_content_len, &sig_algo_header);
    tbs_offset += sig_algo_header + sig_algo_content_len;

    /* issuer */
    x509_parse_name(&cert->issuer, &tbs_content[tbs_offset], tbs_len - tbs_offset);

    uint32_t issuer_header;
    uint32_t issuer_content_len;
    asn1_read_length(&tbs_content[tbs_offset], tbs_len - tbs_offset, &issuer_content_len, &issuer_header);
    tbs_offset += issuer_header + issuer_content_len;

    /* validity */
    x509_parse_validity(&cert->validity, &tbs_content[tbs_offset], tbs_len - tbs_offset);

    uint32_t validity_header;
    uint32_t validity_content_len;
    asn1_read_length(&tbs_content[tbs_offset], tbs_len - tbs_offset, &validity_content_len, &validity_header);
    tbs_offset += validity_header + validity_content_len;

    /* subject */
    x509_parse_name(&cert->subject, &tbs_content[tbs_offset], tbs_len - tbs_offset);

    uint32_t subject_header;
    uint32_t subject_content_len;
    asn1_read_length(&tbs_content[tbs_offset], tbs_len - tbs_offset, &subject_content_len, &subject_header);
    tbs_offset += subject_header + subject_content_len;

    /* subjectPublicKeyInfo */
    x509_parse_spki(cert, &tbs_content[tbs_offset], tbs_len - tbs_offset);

    uint32_t spki_header;
    uint32_t spki_content_len;
    asn1_read_length(&tbs_content[tbs_offset], tbs_len - tbs_offset, &spki_content_len, &spki_header);
    tbs_offset += spki_header + spki_content_len;

    /* Skip extensions for now */

    /* Skip to signature algorithm (outside TBS) */
    offset += 2 + tbs_len; /* approximate */

    /* Calculate actual offset for signatureAlgorithm */
    uint32_t tbs_header;
    uint32_t tbs_content_len2;
    asn1_read_length(content, content_len, &tbs_content_len2, &tbs_header);
    offset = tbs_header + tbs_content_len2;

    /* signatureAlgorithm */
    const uint8_t *sig2_content;
    uint32_t sig2_len;
    if (asn1_parse_sequence(&content[offset], content_len - offset, &sig2_content, &sig2_len) == 0) {
        const uint8_t *sig2_oid;
        uint32_t sig2_oid_len;
        if (asn1_parse_oid(sig2_content, sig2_len, &sig2_oid, &sig2_oid_len) == 0) {
            if (sig2_oid_len > sizeof(cert->signature_algo_oid)) {
                sig2_oid_len = sizeof(cert->signature_algo_oid);
            }
            memcpy(cert->signature_algo_oid, sig2_oid, sig2_oid_len);
            cert->signature_algo_oid_len = sig2_oid_len;
        }
    }

    uint32_t sig2_header;
    uint32_t sig2_content_len;
    asn1_read_length(&content[offset], content_len - offset, &sig2_content_len, &sig2_header);
    offset += sig2_header + sig2_content_len;

    /* signatureValue BIT STRING */
    const uint8_t *sig_value;
    uint32_t sig_value_len;
    if (asn1_parse_bit_string(&content[offset], content_len - offset, &sig_value, &sig_value_len) == 0) {
        if (sig_value_len > sizeof(cert->signature)) {
            sig_value_len = sizeof(cert->signature);
        }
        memcpy(cert->signature, sig_value, sig_value_len);
        cert->signature_len = sig_value_len;
    }

    return 0;
}

/* ============================================================
 *  Certificate verification
 * ============================================================ */

int32_t x509_verify_signature(const x509_cert_t *cert, const x509_cert_t *issuer_cert)
{
    int32_t hash_type;
    uint8_t digest[SHA256_DIGEST_SIZE];
    uint32_t digest_len;

    if (!issuer_cert->rsa_key_ready) {
        return -1;
    }

    /* Determine hash type from signature algorithm OID */
    if (cert->signature_algo_oid_len == OID_SHA256_WITH_RSA_LEN &&
        memcmp(cert->signature_algo_oid, oid_sha256_with_rsa, OID_SHA256_WITH_RSA_LEN) == 0) {
        hash_type = RSA_HASH_SHA256;
        sha256(cert->tbs, cert->tbs_len, digest);
        digest_len = SHA256_DIGEST_SIZE;
    } else if (cert->signature_algo_oid_len == OID_SHA1_WITH_RSA_LEN &&
               memcmp(cert->signature_algo_oid, oid_sha1_with_rsa, OID_SHA1_WITH_RSA_LEN) == 0) {
        hash_type = RSA_HASH_SHA1;
        sha1(cert->tbs, cert->tbs_len, digest);
        digest_len = SHA1_DIGEST_SIZE;
    } else if (cert->signature_algo_oid_len == OID_MD5_WITH_RSA_LEN &&
               memcmp(cert->signature_algo_oid, oid_md5_with_rsa, OID_MD5_WITH_RSA_LEN) == 0) {
        hash_type = RSA_HASH_MD5;
        md5(cert->tbs, cert->tbs_len, digest);
        digest_len = MD5_DIGEST_SIZE;
    } else {
        return -1; /* unsupported hash algorithm */
    }

    return rsa_verify_pkcs1_v15(&issuer_cert->rsa_key,
                                cert->signature, cert->signature_len,
                                digest, digest_len,
                                hash_type);
}

int32_t x509_check_name_match(const x509_name_t *a, const x509_name_t *b)
{
    /* Simplified name matching - just compare common names for now */
    char cn_a[128], cn_b[128];

    x509_get_common_name(a, cn_a, sizeof(cn_a));
    x509_get_common_name(b, cn_b, sizeof(cn_b));

    if (cn_a[0] == '\0' || cn_b[0] == '\0') {
        return -1;
    }

    return strcmp(cn_a, cn_b) == 0 ? 0 : -1;
}

int32_t x509_check_validity(const x509_cert_t *cert, uint64_t current_time)
{
    if (current_time < cert->validity.not_before) {
        return -1;
    }
    if (current_time > cert->validity.not_after) {
        return -1;
    }
    return 0;
}

int32_t x509_verify_chain(const x509_chain_t *chain, const x509_trust_store_t *trust_store)
{
    uint32_t i;
    x509_cert_t issuer;

    if (chain->count == 0) {
        return -1;
    }

    /* Start with the end-entity certificate */
    const x509_cert_t *current = &chain->certs[0];

    for (i = 0; i < chain->count; i++) {
        /* Find issuer in chain or trust store */
        bool found = false;

        /* Check if it's a self-signed root */
        if (x509_check_name_match(&current->subject, &current->issuer) == 0) {
            /* Self-signed - check if it's in trust store */
            if (x509_find_issuer(trust_store, &current->issuer, &issuer) == 0) {
                if (x509_verify_signature(current, &issuer) == 0) {
                    return 0; /* Success - chain verified to trusted root */
                }
            }
            return -1;
        }

        /* Look for issuer in the chain */
        for (uint32_t j = i + 1; j < chain->count; j++) {
            if (x509_check_name_match(&chain->certs[j].subject, &current->issuer) == 0) {
                if (x509_verify_signature(current, &chain->certs[j]) != 0) {
                    return -1;
                }
                current = &chain->certs[j];
                found = true;
                break;
            }
        }

        if (!found) {
            /* Look for issuer in trust store */
            if (x509_find_issuer(trust_store, &current->issuer, &issuer) == 0) {
                if (x509_verify_signature(current, &issuer) == 0) {
                    return 0; /* Success */
                }
            }
            return -1;
        }
    }

    return -1;
}

/* ============================================================
 *  Trust store
 * ============================================================ */

void x509_init_trust_store(x509_trust_store_t *store)
{
    memset(store, 0, sizeof(x509_trust_store_t));
}

int32_t x509_add_trusted_root(x509_trust_store_t *store, const uint8_t *data, uint32_t len)
{
    if (store->count >= 16) {
        return -1;
    }

    if (x509_parse_cert(&store->roots[store->count], data, len) != 0) {
        return -1;
    }

    store->count++;
    return 0;
}

int32_t x509_find_issuer(const x509_trust_store_t *store, const x509_name_t *issuer, x509_cert_t *out_cert)
{
    for (uint32_t i = 0; i < store->count; i++) {
        if (x509_check_name_match(&store->roots[i].subject, issuer) == 0) {
            memcpy(out_cert, &store->roots[i], sizeof(x509_cert_t));
            return 0;
        }
    }
    return -1;
}

/* ============================================================
 *  Utility functions
 * ============================================================ */

int32_t x509_get_common_name(const x509_name_t *name, char *out, uint32_t max_len)
{
    for (uint32_t i = 0; i < name->count; i++) {
        if (name->entries[i].oid_len == OID_COMMON_NAME_LEN &&
            memcmp(name->entries[i].oid, oid_common_name, OID_COMMON_NAME_LEN) == 0) {
            uint32_t copy_len = name->entries[i].value_len;
            if (copy_len >= max_len) {
                copy_len = max_len - 1;
            }
            memcpy(out, name->entries[i].value, copy_len);
            out[copy_len] = '\0';
            return 0;
        }
    }
    if (max_len > 0) {
        out[0] = '\0';
    }
    return -1;
}
