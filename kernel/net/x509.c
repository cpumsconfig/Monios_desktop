#include "x509.h"
#include "string.h"
#include "hash.h"
#include "stddef.h"
#include "rtc.h"

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

static const uint8_t oid_key_usage[] = { 0x55, 0x1D, 0x0F };
#define OID_KEY_USAGE_LEN 3

static const uint8_t oid_extended_key_usage[] = { 0x55, 0x1D, 0x25 };
#define OID_EXTENDED_KEY_USAGE_LEN 3

static const uint8_t oid_code_signing[] = {
    0x2B, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x03
};
#define OID_CODE_SIGNING_LEN 8

static uint64_t x509_current_time(void)
{
    rtc_time_t now;

    memset(&now, 0, sizeof(now));
    rtc_read_time(&now);
    if (now.year == 0 || now.month == 0 || now.month > 12 ||
        now.day == 0 || now.day > 31 || now.hour > 23 ||
        now.minute > 59 || now.second > 59) {
        return 0;
    }
    return (uint64_t) now.year * 31536000ULL +
           (uint64_t) now.month * 2592000ULL +
           (uint64_t) now.day * 86400ULL +
           (uint64_t) now.hour * 3600ULL +
           (uint64_t) now.minute * 60ULL +
           now.second;
}

/* ============================================================
 *  ASN.1 parsing
 * ============================================================ */

int32_t asn1_read_length(const uint8_t *data, uint32_t len, uint32_t *out_len, uint32_t *bytes_read)
{
    if (data == NULL || out_len == NULL || bytes_read == NULL || len < 2) {
        return -1;
    }

    if ((data[1] & 0x80) == 0) {
        /* Short form */
        *out_len = data[1];
        *bytes_read = 2;
        return 0;
    }

    /* Long form. DER forbids indefinite, non-minimal, and leading-zero lengths. */
    uint32_t num_bytes = data[1] & 0x7F;
    if (num_bytes == 0 || num_bytes > 4) {
        return -1;
    }
    if (num_bytes > len - 2U) {
        return -1;
    }
    if (data[2] == 0 ||
        (num_bytes == 1 && data[2] < 0x80)) {
        return -1;
    }

    *out_len = 0;
    for (uint32_t i = 0; i < num_bytes; i++) {
        *out_len = (*out_len << 8) | data[2 + i];
    }
    *bytes_read = 2 + num_bytes;
    return 0;
}

static bool asn1_tlv_total_length(const uint8_t *data, uint32_t len, uint32_t *total_out)
{
    uint32_t content_len;
    uint32_t header_len;

    if (data == NULL || total_out == NULL ||
        asn1_read_length(data, len, &content_len, &header_len) != 0 ||
        header_len > len ||
        content_len > len - header_len) {
        return false;
    }
    *total_out = header_len + content_len;
    return true;
}

static bool asn1_skip_tlv(const uint8_t *data, uint32_t len, uint32_t *offset)
{
    uint32_t total;

    if (data == NULL || offset == NULL || *offset > len ||
        !asn1_tlv_total_length(data + *offset, len - *offset, &total)) {
        return false;
    }
    *offset += total;
    return true;
}

static int32_t asn1_parse_container(const uint8_t *data,
                                    uint32_t len,
                                    uint8_t tag,
                                    const uint8_t **out_content,
                                    uint32_t *out_len)
{
    uint32_t content_len;
    uint32_t header_len;

    if (data == NULL || out_content == NULL || out_len == NULL ||
        len < 2 || data[0] != tag ||
        asn1_read_length(data, len, &content_len, &header_len) != 0 ||
        header_len > len || content_len > len - header_len) {
        return -1;
    }

    *out_content = &data[header_len];
    *out_len = content_len;
    return 0;
}

int32_t asn1_parse_sequence(const uint8_t *data, uint32_t len, const uint8_t **out_content, uint32_t *out_len)
{
    return asn1_parse_container(data,
                                len,
                                ASN1_TAG_SEQUENCE,
                                out_content,
                                out_len);
}

int32_t asn1_parse_integer(const uint8_t *data, uint32_t len, const uint8_t **out_content, uint32_t *out_len)
{
    uint32_t content_len;
    uint32_t header_len;

    if (data == NULL || out_content == NULL || out_len == NULL ||
        len < 2 || data[0] != ASN1_TAG_INTEGER ||
        asn1_read_length(data, len, &content_len, &header_len) != 0 ||
        header_len > len || content_len > len - header_len) {
        return -1;
    }

    *out_content = &data[header_len];
    *out_len = content_len;
    return 0;
}

int32_t asn1_parse_bit_string(const uint8_t *data, uint32_t len, const uint8_t **out_content, uint32_t *out_len)
{
    uint32_t content_len;
    uint32_t header_len;

    if (data == NULL || out_content == NULL || out_len == NULL ||
        len < 3 || data[0] != ASN1_TAG_BIT_STRING ||
        asn1_read_length(data, len, &content_len, &header_len) != 0 ||
        header_len > len || content_len > len - header_len ||
        content_len < 1) {
        return -1;
    }

    /* First byte is unused bits count */
    *out_content = &data[header_len + 1];
    *out_len = content_len - 1;
    return 0;
}

int32_t asn1_parse_oid(const uint8_t *data, uint32_t len, const uint8_t **out_oid, uint32_t *out_len)
{
    uint32_t content_len;
    uint32_t header_len;

    if (data == NULL || out_oid == NULL || out_len == NULL ||
        len < 2 || data[0] != ASN1_TAG_OID ||
        asn1_read_length(data, len, &content_len, &header_len) != 0 ||
        header_len > len || content_len > len - header_len) {
        return -1;
    }

    *out_oid = &data[header_len];
    *out_len = content_len;
    return 0;
}

int32_t asn1_parse_string(const uint8_t *data, uint32_t len, char *out_str, uint32_t max_len)
{
    if (data == NULL || out_str == NULL || max_len == 0 || len < 2) {
        return -1;
    }

    uint8_t tag = data[0];
    if (tag != ASN1_TAG_UTF8_STRING && tag != ASN1_TAG_PRINTABLE_STRING &&
        tag != ASN1_TAG_IA5_STRING && tag != 0x14 /* T61String */ &&
        tag != 0x1E /* BMPString */) {
        return -1;
    }

    uint32_t content_len, header_len;
    if (asn1_read_length(data, len, &content_len, &header_len) != 0) {
        return -1;
    }

    if (header_len > len || content_len > len - header_len) {
        return -1;
    }

    if (tag == 0x1E) {
        if ((content_len & 1U) != 0) {
            return -1;
        }
        uint32_t character_count = content_len / 2U;
        uint32_t copy_len = character_count;
        if (copy_len >= max_len) {
            copy_len = max_len - 1U;
        }
        for (uint32_t i = 0; i < copy_len; i++) {
            if (data[header_len + i * 2U] != 0) {
                return -1;
            }
            out_str[i] = (char) data[header_len + i * 2U + 1U];
        }
        out_str[copy_len] = '\0';
        return (int32_t) copy_len;
    }

    uint32_t copy_len = content_len;
    if (copy_len >= max_len) {
        copy_len = max_len - 1U;
    }

    memcpy(out_str, &data[header_len], copy_len);
    out_str[copy_len] = '\0';
    return (int32_t) copy_len;
}

/* ============================================================
 *  X.509 name parsing
 * ============================================================ */

int32_t x509_parse_name(x509_name_t *name, const uint8_t *data, uint32_t len)
{
    const uint8_t *content;
    uint32_t content_len;
    uint32_t offset = 0;
    uint32_t rdn_index = 0;

    if (name == NULL || data == NULL) {
        return -1;
    }
    memset(name, 0, sizeof(x509_name_t));

    if (asn1_parse_sequence(data, len, &content, &content_len) != 0) {
        return -1;
    }

    while (offset < content_len) {
        const uint8_t *set_content;
        uint32_t set_len;
        uint32_t set_total;
        uint32_t set_offset = 0;

        if (content[offset] != ASN1_TAG_SET) {
            return -1;
        }

        if (asn1_parse_container(&content[offset],
                                 content_len - offset,
                                 ASN1_TAG_SET,
                                 &set_content,
                                 &set_len) != 0) {
            return -1;
        }
        if (set_len == 0) {
            return -1;
        }

        while (set_offset < set_len) {
            const uint8_t *seq_content;
            uint32_t seq_len;
            uint32_t seq_total;
            uint32_t oid_header;
            uint32_t oid_content_len;
            uint32_t value_offset;
            const uint8_t *oid;
            uint32_t oid_len;

            if (name->count >= X509_MAX_NAME_ENTRIES ||
                asn1_parse_sequence(set_content + set_offset,
                                    set_len - set_offset,
                                    &seq_content,
                                    &seq_len) != 0 ||
                !asn1_tlv_total_length(set_content + set_offset,
                                       set_len - set_offset,
                                       &seq_total)) {
                return -1;
            }

            if (asn1_parse_oid(seq_content, seq_len, &oid, &oid_len) != 0 ||
                oid_len > sizeof(name->entries[name->count].oid) ||
                asn1_read_length(seq_content,
                                 seq_len,
                                 &oid_content_len,
                                 &oid_header) != 0 ||
                oid_header > seq_len ||
                oid_content_len > seq_len - oid_header) {
                return -1;
            }
            value_offset = oid_header + oid_content_len;
            if (value_offset >= seq_len ||
                asn1_parse_string(seq_content + value_offset,
                                  seq_len - value_offset,
                                  name->entries[name->count].value,
                                  sizeof(name->entries[name->count].value)) < 0) {
                return -1;
            }

            memcpy(name->entries[name->count].oid, oid, oid_len);
            name->entries[name->count].oid_len = oid_len;
            name->entries[name->count].value_len =
                (uint32_t) strlen(name->entries[name->count].value);
            name->entries[name->count].rdn_index = rdn_index;
            name->count++;
            set_offset += seq_total;
        }

        if (set_offset != set_len) {
            return -1;
        }
        if (!asn1_tlv_total_length(&content[offset],
                                   content_len - offset,
                                   &set_total) ||
            set_total == 0 ||
            set_total > content_len - offset) {
            return -1;
        }
        offset += set_total;
        rdn_index++;
    }

    return offset == content_len && name->count > 0 ? 0 : -1;
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

    if (data == NULL || len < (utc ? 12U : 14U)) {
        return 0;
    }
    for (uint32_t i = 0; i < len; i++) {
        if (i == len - 1U && data[i] == 'Z') {
            continue;
        }
        if (data[i] < '0' || data[i] > '9') {
            return 0;
        }
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
    uint32_t offset = 0;
    uint32_t value_len;
    uint32_t header_len;

    if (validity == NULL ||
        asn1_parse_sequence(data, len, &content, &content_len) != 0 ||
        content_len == 0) {
        return -1;
    }

    if ((content[0] != ASN1_TAG_UTCTIME &&
         content[0] != ASN1_TAG_GENERALIZED_TIME) ||
        asn1_read_length(content, content_len, &value_len, &header_len) != 0 ||
        header_len > content_len ||
        value_len > content_len - header_len) {
        return -1;
    }
    validity->not_before =
        x509_parse_time(content + header_len,
                        value_len,
                        content[0] == ASN1_TAG_UTCTIME);
    if (validity->not_before == 0) {
        return -1;
    }
    offset = header_len + value_len;
    if (offset >= content_len ||
        (content[offset] != ASN1_TAG_UTCTIME &&
         content[offset] != ASN1_TAG_GENERALIZED_TIME) ||
        asn1_read_length(content + offset,
                         content_len - offset,
                         &value_len,
                         &header_len) != 0 ||
        header_len > content_len - offset ||
        value_len > content_len - offset - header_len) {
        return -1;
    }
    validity->not_after =
        x509_parse_time(content + offset + header_len,
                        value_len,
                        content[offset] == ASN1_TAG_UTCTIME);
    if (validity->not_after == 0) {
        return -1;
    }

    return 0;
}

/* ============================================================
 *  X.509 SPKI parsing
 * ============================================================ */

int32_t x509_parse_spki(x509_cert_t *cert, const uint8_t *data, uint32_t len)
{
    const uint8_t *content;
    uint32_t content_len;

    if (cert == NULL ||
        asn1_parse_sequence(data, len, &content, &content_len) != 0) {
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
    if (asn1_read_length(content,
                         content_len,
                         &algo_content_len,
                         &algo_header) != 0 ||
        algo_header > content_len ||
        algo_content_len > content_len - algo_header) {
        return -1;
    }
    uint32_t offset = algo_header + algo_content_len;

    /* subjectPublicKey BIT STRING */
    const uint8_t *bit_string;
    uint32_t bit_len;
    if (offset >= content_len ||
        asn1_parse_bit_string(&content[offset], content_len - offset, &bit_string, &bit_len) != 0) {
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
        while (mod_len > 1 && mod[0] == 0) {
            mod++;
            mod_len--;
        }

        if (mod_len > sizeof(cert->pubkey_modulus)) {
            return -1;
        }
        memcpy(cert->pubkey_modulus, mod, mod_len);
        cert->pubkey_modulus_len = mod_len;

        /* Skip modulus */
        uint32_t mod_header;
        uint32_t mod_content_len;
        if (asn1_read_length(rsa_content,
                             rsa_len,
                             &mod_content_len,
                             &mod_header) != 0 ||
            mod_header > rsa_len ||
            mod_content_len > rsa_len - mod_header) {
            return -1;
        }
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
        if (rsa_pubkey_init(&cert->rsa_key,
                            cert->pubkey_modulus, cert->pubkey_modulus_len,
                            cert->pubkey_exponent, cert->pubkey_exponent_len) != 0) {
            return -1;
        }
        cert->rsa_key_ready = true;
    }

    return 0;
}

/* ============================================================
 *  X.509 certificate parsing
 * ============================================================ */

static bool x509_oid_equal(const uint8_t *left,
                           uint32_t left_len,
                           const uint8_t *right,
                           uint32_t right_len)
{
    return left != NULL && right != NULL &&
           left_len == right_len &&
           memcmp(left, right, left_len) == 0;
}

static int32_t x509_parse_basic_constraints(x509_cert_t *cert,
                                            const uint8_t *data,
                                            uint32_t len)
{
    const uint8_t *content;
    uint32_t content_len;
    uint32_t offset = 0;

    if (cert == NULL ||
        asn1_parse_sequence(data, len, &content, &content_len) != 0) {
        return -1;
    }
    cert->is_ca = false;
    cert->path_len_constraint = -1;
    if (offset < content_len && content[offset] == ASN1_TAG_BOOLEAN) {
        const uint8_t *value;
        uint32_t value_len;

        if (asn1_parse_container(content + offset,
                                 content_len - offset,
                                 ASN1_TAG_BOOLEAN,
                                 &value,
                                 &value_len) != 0 ||
            value_len != 1) {
            return -1;
        }
        cert->is_ca = value[0] != 0;
        if (!asn1_skip_tlv(content, content_len, &offset)) {
            return -1;
        }
    }
    if (offset < content_len) {
        const uint8_t *path_len;
        uint32_t path_len_size;
        uint32_t path_value = 0;

        if (asn1_parse_integer(content + offset,
                               content_len - offset,
                               &path_len,
                               &path_len_size) != 0 ||
            path_len_size == 0 ||
            path_len_size > 4 ||
            (path_len_size > 1 && path_len[0] == 0 &&
             path_len[1] < 0x80) ||
            (path_len[0] & 0x80) != 0) {
            return -1;
        }
        for (uint32_t i = 0; i < path_len_size; i++) {
            path_value = (path_value << 8) | path_len[i];
        }
        cert->path_len_constraint = (int32_t) path_value;
        if (!asn1_skip_tlv(content, content_len, &offset)) {
            return -1;
        }
    }
    return offset == content_len ? 0 : -1;
}

static int32_t x509_parse_extended_key_usage(x509_cert_t *cert,
                                             const uint8_t *data,
                                             uint32_t len)
{
    const uint8_t *content;
    uint32_t content_len;
    uint32_t offset = 0;

    if (cert == NULL ||
        asn1_parse_sequence(data, len, &content, &content_len) != 0 ||
        content_len == 0) {
        return -1;
    }
    cert->eku_present = true;
    cert->has_code_signing_eku = false;
    while (offset < content_len) {
        const uint8_t *oid;
        uint32_t oid_len;

        if (asn1_parse_oid(content + offset,
                           content_len - offset,
                           &oid,
                           &oid_len) != 0) {
            return -1;
        }
        if (x509_oid_equal(oid,
                           oid_len,
                           oid_code_signing,
                           OID_CODE_SIGNING_LEN)) {
            cert->has_code_signing_eku = true;
        }
        if (!asn1_skip_tlv(content, content_len, &offset)) {
            return -1;
        }
    }
    return offset == content_len ? 0 : -1;
}

static int32_t x509_parse_key_usage(x509_cert_t *cert,
                                    const uint8_t *data,
                                    uint32_t len)
{
    uint32_t content_len;
    uint32_t header_len;
    uint8_t unused_bits;

    if (cert == NULL ||
        len < 3 ||
        data[0] != ASN1_TAG_BIT_STRING ||
        asn1_read_length(data, len, &content_len, &header_len) != 0 ||
        header_len >= len ||
        content_len == 0 ||
        content_len > len - header_len) {
        return -1;
    }
    unused_bits = data[header_len];
    if (unused_bits > 7) {
        return -1;
    }
    cert->key_usage_present = true;
    cert->key_usage = 0;
    if (content_len > 1 && (data[header_len + 1U] & 0x80) != 0) {
        cert->key_usage |= 0x0001U; /* digitalSignature */
    }
    if (content_len > 1 && (data[header_len + 1U] & 0x04) != 0) {
        cert->key_usage |= 0x0020U; /* keyCertSign */
    }
    return 0;
}

static int32_t x509_parse_extensions(x509_cert_t *cert,
                                      const uint8_t *data,
                                      uint32_t len)
{
    const uint8_t *extensions_content;
    uint32_t extensions_len;
    const uint8_t *sequence_content;
    uint32_t sequence_len;
    uint32_t offset = 0;

    if (cert == NULL ||
        asn1_parse_container(data,
                             len,
                             ASN1_TAG_CONTEXT_SPECIFIC |
                                 ASN1_TAG_CONSTRUCTED | 3U,
                             &extensions_content,
                             &extensions_len) != 0 ||
        asn1_parse_sequence(extensions_content,
                            extensions_len,
                            &sequence_content,
                            &sequence_len) != 0) {
        return -1;
    }
    while (offset < sequence_len) {
        const uint8_t *extension_content;
        uint32_t extension_len;
        uint32_t extension_offset = 0;
        const uint8_t *oid;
        uint32_t oid_len;
        const uint8_t *extension_value;
        uint32_t extension_value_len;

        if (asn1_parse_sequence(sequence_content + offset,
                                sequence_len - offset,
                                &extension_content,
                                &extension_len) != 0) {
            return -1;
        }
        if (asn1_parse_oid(extension_content,
                           extension_len,
                           &oid,
                           &oid_len) != 0 ||
            !asn1_skip_tlv(extension_content,
                           extension_len,
                           &extension_offset)) {
            return -1;
        }
        if (extension_offset < extension_len &&
            extension_content[extension_offset] == ASN1_TAG_BOOLEAN &&
            !asn1_skip_tlv(extension_content,
                           extension_len,
                           &extension_offset)) {
            return -1;
        }
        if (extension_offset >= extension_len ||
            asn1_parse_container(extension_content + extension_offset,
                                 extension_len - extension_offset,
                                 ASN1_TAG_OCTET_STRING,
                                 &extension_value,
                                 &extension_value_len) != 0 ||
            !asn1_skip_tlv(extension_content,
                           extension_len,
                           &extension_offset) ||
            extension_offset != extension_len) {
            return -1;
        }

        if (x509_oid_equal(oid,
                           oid_len,
                           oid_basic_constraints,
                           OID_BASIC_CONSTRAINTS_LEN)) {
            if (x509_parse_basic_constraints(cert,
                                             extension_value,
                                             extension_value_len) != 0) {
                return -1;
            }
        } else if (x509_oid_equal(oid,
                                  oid_len,
                                  oid_extended_key_usage,
                                  OID_EXTENDED_KEY_USAGE_LEN)) {
            if (x509_parse_extended_key_usage(cert,
                                              extension_value,
                                              extension_value_len) != 0) {
                return -1;
            }
        } else if (x509_oid_equal(oid,
                                  oid_len,
                                  oid_key_usage,
                                  OID_KEY_USAGE_LEN)) {
            if (x509_parse_key_usage(cert,
                                     extension_value,
                                     extension_value_len) != 0) {
                return -1;
            }
        }
        if (!asn1_skip_tlv(sequence_content, sequence_len, &offset)) {
            return -1;
        }
    }
    return offset == sequence_len ? 0 : -1;
}

int32_t x509_parse_cert(x509_cert_t *cert, const uint8_t *data, uint32_t len)
{
    const uint8_t *content;
    uint32_t content_len;
    uint32_t cert_total;

    if (cert == NULL || data == NULL || len == 0 || len > X509_MAX_CERT_SIZE) {
        return -1;
    }
    memset(cert, 0, sizeof(x509_cert_t));

    memcpy(cert->raw, data, len);
    cert->raw_len = len;

    /* Certificate SEQUENCE */
    if (asn1_parse_sequence(data, len, &content, &content_len) != 0 ||
        !asn1_tlv_total_length(data, len, &cert_total) ||
        cert_total != len) {
        return -1;
    }

    uint32_t offset = 0;

    /* tbsCertificate */
    const uint8_t *tbs_content;
    uint32_t tbs_len;
    uint32_t tbs_total;
    if (asn1_parse_sequence(content, content_len, &tbs_content, &tbs_len) != 0 ||
        !asn1_tlv_total_length(content, content_len, &tbs_total)) {
        return -1;
    }
    if (tbs_len == 0) {
        return -1;
    }

    /* Signature input is the complete DER-encoded TBSCertificate. */
    if (tbs_total > sizeof(cert->tbs)) {
        return -1;
    }
    memcpy(cert->tbs, content, tbs_total);
    cert->tbs_len = tbs_total;

    uint32_t tbs_offset = 0;

    /* version (optional, context-specific [0]) */
    cert->version = 1; /* default v1 */
    if (tbs_offset < tbs_len &&
        tbs_content[tbs_offset] == (ASN1_TAG_CONTEXT_SPECIFIC | ASN1_TAG_CONSTRUCTED | 0)) {
        const uint8_t *ver_content;
        uint32_t ver_len;
        if (asn1_parse_container(&tbs_content[tbs_offset],
                                 tbs_len - tbs_offset,
                                 ASN1_TAG_CONTEXT_SPECIFIC |
                                     ASN1_TAG_CONSTRUCTED | 0U,
                                 &ver_content,
                                 &ver_len) == 0) {
            const uint8_t *ver_int;
            uint32_t ver_int_len;
            if (asn1_parse_integer(ver_content, ver_len, &ver_int, &ver_int_len) == 0 && ver_int_len > 0) {
                cert->version = ver_int[0] + 1;
            }
        }
        if (!asn1_skip_tlv(tbs_content, tbs_len, &tbs_offset)) {
            return -1;
        }
    }

    /* serialNumber */
    const uint8_t *serial;
    uint32_t serial_len;
    if (tbs_offset >= tbs_len ||
        asn1_parse_integer(&tbs_content[tbs_offset], tbs_len - tbs_offset, &serial, &serial_len) != 0) {
        return -1;
    }
    if (serial_len > sizeof(cert->serial_number)) {
        serial_len = sizeof(cert->serial_number);
    }
    memcpy(cert->serial_number, serial, serial_len);
    cert->serial_len = serial_len;

    if (!asn1_skip_tlv(tbs_content, tbs_len, &tbs_offset)) {
        return -1;
    }

    /* signature (AlgorithmIdentifier) */
    const uint8_t *sig_algo_content;
    uint32_t sig_algo_len;
    if (tbs_offset >= tbs_len ||
        asn1_parse_sequence(&tbs_content[tbs_offset], tbs_len - tbs_offset, &sig_algo_content, &sig_algo_len) != 0) {
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

    if (!asn1_skip_tlv(tbs_content, tbs_len, &tbs_offset)) {
        return -1;
    }

    /* issuer */
    if (tbs_offset >= tbs_len ||
        x509_parse_name(&cert->issuer, &tbs_content[tbs_offset], tbs_len - tbs_offset) != 0 ||
        !asn1_skip_tlv(tbs_content, tbs_len, &tbs_offset)) {
        return -1;
    }

    /* validity */
    if (tbs_offset >= tbs_len ||
        x509_parse_validity(&cert->validity, &tbs_content[tbs_offset], tbs_len - tbs_offset) != 0 ||
        !asn1_skip_tlv(tbs_content, tbs_len, &tbs_offset)) {
        return -1;
    }

    /* subject */
    if (tbs_offset >= tbs_len ||
        x509_parse_name(&cert->subject, &tbs_content[tbs_offset], tbs_len - tbs_offset) != 0 ||
        !asn1_skip_tlv(tbs_content, tbs_len, &tbs_offset)) {
        return -1;
    }

    /* subjectPublicKeyInfo */
    if (tbs_offset >= tbs_len ||
        x509_parse_spki(cert, &tbs_content[tbs_offset], tbs_len - tbs_offset) != 0 ||
        !asn1_skip_tlv(tbs_content, tbs_len, &tbs_offset)) {
        return -1;
    }

    cert->path_len_constraint = -1;
    while (tbs_offset < tbs_len &&
           (tbs_content[tbs_offset] ==
                (ASN1_TAG_CONTEXT_SPECIFIC | 1U) ||
            tbs_content[tbs_offset] ==
                (ASN1_TAG_CONTEXT_SPECIFIC | 2U))) {
        if (!asn1_skip_tlv(tbs_content, tbs_len, &tbs_offset)) {
            return -1;
        }
    }
    if (tbs_offset < tbs_len &&
        tbs_content[tbs_offset] ==
            (ASN1_TAG_CONTEXT_SPECIFIC | ASN1_TAG_CONSTRUCTED | 3U)) {
        uint32_t extension_total;

        if (!asn1_tlv_total_length(tbs_content + tbs_offset,
                                   tbs_len - tbs_offset,
                                   &extension_total) ||
            x509_parse_extensions(cert,
                                  tbs_content + tbs_offset,
                                  extension_total) != 0) {
            return -1;
        }
        tbs_offset += extension_total;
    }
    if (tbs_offset != tbs_len) {
        return -1;
    }

    /* Skip to signature algorithm (outside TBS) */
    if (!asn1_tlv_total_length(content, content_len, &offset) ||
        offset >= content_len) {
        return -1;
    }

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

    if (!asn1_skip_tlv(content, content_len, &offset) || offset >= content_len) {
        return -1;
    }

    /* signatureValue BIT STRING */
    const uint8_t *sig_value;
    uint32_t sig_value_len;
    if (asn1_parse_bit_string(&content[offset], content_len - offset, &sig_value, &sig_value_len) != 0) {
        return -1;
    }
    if (sig_value_len > sizeof(cert->signature)) {
        return -1;
    }
    memcpy(cert->signature, sig_value, sig_value_len);
    cert->signature_len = sig_value_len;

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

    if (cert == NULL || issuer_cert == NULL ||
        cert->tbs_len == 0 || cert->signature_len == 0 ||
        !issuer_cert->rsa_key_ready) {
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
    if (a == NULL || b == NULL ||
        a->count == 0 ||
        b->count == 0 ||
        a->count != b->count ||
        a->count > X509_MAX_NAME_ENTRIES ||
        b->count > X509_MAX_NAME_ENTRIES) {
        return -1;
    }
    for (uint32_t i = 0; i < a->count; i++) {
        if (a->entries[i].rdn_index != b->entries[i].rdn_index ||
            a->entries[i].oid_len != b->entries[i].oid_len ||
            a->entries[i].value_len != b->entries[i].value_len ||
            memcmp(a->entries[i].oid,
                   b->entries[i].oid,
                   a->entries[i].oid_len) != 0 ||
            memcmp(a->entries[i].value,
                   b->entries[i].value,
                   a->entries[i].value_len) != 0) {
            return -1;
        }
    }
    return 0;
}

int32_t x509_check_validity(const x509_cert_t *cert, uint64_t current_time)
{
    if (cert == NULL || current_time == 0 ||
        cert->validity.not_before == 0 ||
        cert->validity.not_after < cert->validity.not_before) {
        return -1;
    }
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
    x509_cert_t issuer;
    uint64_t current_time;
    bool visited[X509_MAX_CHAIN_DEPTH];
    uint32_t current_index;

    if (chain == NULL || trust_store == NULL ||
        chain->count == 0 || chain->count > X509_MAX_CHAIN_DEPTH ||
        trust_store->count > X509_MAX_TRUSTED_ROOTS) {
        return -1;
    }
    current_time = x509_current_time();
    if (current_time == 0) {
        return -1;
    }
    memset(visited, 0, sizeof(visited));
    for (uint32_t i = 0; i < chain->count; i++) {
        if (x509_check_validity(&chain->certs[i], current_time) != 0) {
            return -1;
        }
    }

    current_index = 0;
    for (uint32_t depth = 0; depth < chain->count; depth++) {
        const x509_cert_t *current = &chain->certs[current_index];
        bool found = false;

        if (visited[current_index]) {
            return -1;
        }
        visited[current_index] = true;

        if (x509_check_name_match(&current->subject, &current->issuer) == 0) {
            if (x509_trust_contains(trust_store, current) &&
                x509_verify_signature(current, current) == 0) {
                return 0;
            }
            return -1;
        }

        for (uint32_t j = 0; j < chain->count; j++) {
            uint32_t ca_below = 0;

            if (j == current_index ||
                visited[j] ||
                x509_check_name_match(&chain->certs[j].subject,
                                       &current->issuer) != 0 ||
                x509_verify_signature(current, &chain->certs[j]) != 0) {
                continue;
            }
            if (!chain->certs[j].is_ca &&
                !x509_trust_contains(trust_store, &chain->certs[j])) {
                return -1;
            }
            if (chain->certs[j].path_len_constraint >= 0) {
                for (uint32_t k = 1; k < chain->count; k++) {
                    if (k != j && chain->certs[k].is_ca) {
                        ca_below++;
                    }
                }
                if (ca_below > (uint32_t) chain->certs[j].path_len_constraint) {
                    return -1;
                }
            }
            current_index = j;
            found = true;
            break;
        }
        if (found) {
            continue;
        }
        if (x509_find_issuer(trust_store, &current->issuer, &issuer) != 0 ||
            x509_check_validity(&issuer, current_time) != 0 ||
            x509_verify_signature(current, &issuer) != 0) {
            return -1;
        }
        return 0;
    }

    return -1;
}

/* ============================================================
 *  Trust store
 * ============================================================ */

void x509_init_trust_store(x509_trust_store_t *store)
{
    if (store == NULL) {
        return;
    }
    memset(store, 0, sizeof(x509_trust_store_t));
}

int32_t x509_add_trusted_root(x509_trust_store_t *store, const uint8_t *data, uint32_t len)
{
    if (store == NULL || data == NULL || len == 0 ||
        store->count >= X509_MAX_TRUSTED_ROOTS) {
        return -1;
    }

    if (x509_parse_cert(&store->roots[store->count], data, len) != 0) {
        return -1;
    }

    store->count++;
    return 0;
}

bool x509_trust_contains(const x509_trust_store_t *store, const x509_cert_t *cert)
{
    if (store == NULL || cert == NULL ||
        store->count > X509_MAX_TRUSTED_ROOTS) {
        return false;
    }
    for (uint32_t i = 0; i < store->count; i++) {
        if (store->roots[i].raw_len == cert->raw_len &&
            memcmp(store->roots[i].raw, cert->raw, cert->raw_len) == 0) {
            return true;
        }
    }
    return false;
}

int32_t x509_find_issuer(const x509_trust_store_t *store, const x509_name_t *issuer, x509_cert_t *out_cert)
{
    if (store == NULL || issuer == NULL || out_cert == NULL ||
        store->count > X509_MAX_TRUSTED_ROOTS) {
        return -1;
    }
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
    if (name == NULL || out == NULL || max_len == 0 ||
        name->count > X509_MAX_NAME_ENTRIES) {
        return -1;
    }
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
