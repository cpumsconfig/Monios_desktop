#include "authenticode.h"
#include "common.h"
#include "hash.h"
#include "kernel.h"
#include "memory.h"
#include "rsa.h"
#include "trust_store.h"
#include "x509.h"

#define PE_DOS_MAGIC 0x5A4D
#define PE_NT_SIGNATURE 0x00004550U
#define PE_OPTIONAL_MAGIC_PE32_PLUS 0x20B
#define PE_DIRECTORY_SECURITY 4U
#define PE_WIN_CERT_REVISION_1 0x0100
#define PE_WIN_CERT_REVISION_2 0x0200
#define PE_WIN_CERT_TYPE_PKCS_SIGNED_DATA 0x0002
#define AUTHENTICODE_MAX_CMS_SIZE (256U * 1024U)
#define AUTHENTICODE_MAX_AUTH_ATTRS 8192U

static const uint8_t oid_signed_data[] = {
    0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x07, 0x02
};
static const uint8_t oid_spc_indirect_data[] = {
    0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x01, 0x04
};
static const uint8_t oid_sha256[] = {
    0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01
};
static const uint8_t oid_rsa_encryption[] = {
    0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01
};
static const uint8_t oid_content_type[] = {
    0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x09, 0x03
};
static const uint8_t oid_message_digest[] = {
    0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x09, 0x04
};

typedef struct {
    uint8_t tag;
    const uint8_t *encoded;
    const uint8_t *content;
    uint32_t header_len;
    uint32_t content_len;
    uint32_t total_len;
} authenticode_der_node_t;

static uint16_t authenticode_read_u16(const uint8_t *data, uint32_t offset)
{
    return (uint16_t) data[offset] |
           ((uint16_t) data[offset + 1U] << 8);
}

static uint32_t authenticode_read_u32(const uint8_t *data, uint32_t offset)
{
    return (uint32_t) data[offset] |
           ((uint32_t) data[offset + 1U] << 8) |
           ((uint32_t) data[offset + 2U] << 16) |
           ((uint32_t) data[offset + 3U] << 24);
}

static bool authenticode_range_ok(uint32_t size,
                                  uint32_t offset,
                                  uint32_t length)
{
    return offset <= size && length <= size - offset;
}

static bool authenticode_der_read(const uint8_t *data,
                                  uint32_t size,
                                  uint32_t offset,
                                  authenticode_der_node_t *node)
{
    uint32_t content_len;
    uint32_t header_len;

    if (data == NULL || node == NULL || offset > size ||
        size - offset < 2U ||
        (data[offset] & 0x1FU) == 0x1FU ||
        asn1_read_length(data + offset,
                         size - offset,
                         &content_len,
                         &header_len) != 0 ||
        header_len > size - offset ||
        content_len > size - offset - header_len) {
        return false;
    }
    node->tag = data[offset];
    node->encoded = data + offset;
    node->content = data + offset + header_len;
    node->header_len = header_len;
    node->content_len = content_len;
    node->total_len = header_len + content_len;
    return true;
}

static bool authenticode_der_next(const authenticode_der_node_t *container,
                                  uint32_t *offset,
                                  authenticode_der_node_t *node)
{
    if (container == NULL || offset == NULL || node == NULL ||
        *offset > container->content_len ||
        *offset == container->content_len ||
        !authenticode_der_read(container->content,
                                container->content_len,
                                *offset,
                                node)) {
        return false;
    }
    *offset += node->total_len;
    return true;
}

static bool authenticode_der_tag(const authenticode_der_node_t *node, uint8_t tag)
{
    return node != NULL && node->tag == tag;
}

static bool authenticode_der_oid(const authenticode_der_node_t *node,
                                 const uint8_t *oid,
                                 uint32_t oid_len)
{
    return node != NULL &&
           node->tag == ASN1_TAG_OID &&
           node->content_len == oid_len &&
           memcmp(node->content, oid, oid_len) == 0;
}

static bool authenticode_der_algorithm(const authenticode_der_node_t *node,
                                       const uint8_t *oid,
                                       uint32_t oid_len)
{
    authenticode_der_node_t algorithm;
    authenticode_der_node_t child;
    uint32_t offset = 0;

    if (node == NULL || !authenticode_der_tag(node, ASN1_TAG_SEQUENCE) ||
        !authenticode_der_read(node->encoded,
                               node->total_len,
                               0,
                               &algorithm) ||
        !authenticode_der_next(&algorithm, &offset, &child) ||
        !authenticode_der_oid(&child, oid, oid_len)) {
        return false;
    }
    while (offset < algorithm.content_len) {
        if (!authenticode_der_next(&algorithm, &offset, &child)) {
            return false;
        }
        if (!authenticode_der_tag(&child, ASN1_TAG_NULL)) {
            return false;
        }
    }
    return offset == algorithm.content_len;
}

static bool authenticode_compute_digest(const uint8_t *data,
                                        uint32_t size,
                                        uint32_t optional_offset,
                                        uint32_t security_dir_offset,
                                        uint32_t cert_offset,
                                        uint32_t cert_size,
                                        uint8_t digest[SHA256_DIGEST_SIZE])
{
    uint64_t cert_end;
    uint32_t checksum_offset = optional_offset + 64U;
    sha256_ctx_t context;

    if (data == NULL || digest == NULL ||
        !authenticode_range_ok(size, checksum_offset, 4U) ||
        !authenticode_range_ok(size, security_dir_offset, 8U) ||
        !authenticode_range_ok(size, cert_offset, cert_size) ||
        cert_offset < security_dir_offset + 8U ||
        checksum_offset + 4U > security_dir_offset) {
        return false;
    }
    cert_end = (uint64_t) cert_offset + cert_size;
    if (cert_end > size) {
        return false;
    }

    sha256_init(&context);
    sha256_update(&context, data, checksum_offset);
    sha256_update(&context,
                  data + checksum_offset + 4U,
                  security_dir_offset - checksum_offset - 4U);
    sha256_update(&context,
                  data + security_dir_offset + 8U,
                  cert_offset - security_dir_offset - 8U);
    if (cert_end < size) {
        sha256_update(&context,
                      data + (uint32_t) cert_end,
                      size - (uint32_t) cert_end);
    }
    sha256_final(&context, digest);
    return true;
}

static bool authenticode_parse_spc_digest(const authenticode_der_node_t *encap,
                                          const uint8_t file_digest[SHA256_DIGEST_SIZE],
                                          uint8_t content_digest[SHA256_DIGEST_SIZE])
{
    authenticode_der_node_t child;
    authenticode_der_node_t indirect;
    authenticode_der_node_t indirect_child;
    authenticode_der_node_t digest_info;
    authenticode_der_node_t digest_info_child;
    uint32_t offset = 0;
    uint32_t indirect_offset = 0;
    uint32_t digest_info_offset = 0;
    uint8_t digest[SHA256_DIGEST_SIZE];
    bool found_digest_info = false;

    if (encap == NULL || file_digest == NULL || content_digest == NULL ||
        !authenticode_der_tag(encap, ASN1_TAG_SEQUENCE) ||
        !authenticode_der_next(encap, &offset, &child) ||
        !authenticode_der_oid(&child, oid_spc_indirect_data,
                              sizeof(oid_spc_indirect_data)) ||
        !authenticode_der_next(encap, &offset, &child) ||
        !authenticode_der_tag(&child,
                              ASN1_TAG_CONTEXT_SPECIFIC |
                                  ASN1_TAG_CONSTRUCTED | 0U) ||
        !authenticode_der_read(child.content,
                               child.content_len,
                               0,
                               &indirect) ||
        !authenticode_der_tag(&indirect, ASN1_TAG_SEQUENCE)) {
        return false;
    }
    if (offset != encap->content_len) {
        return false;
    }

    while (indirect_offset < indirect.content_len) {
        if (!authenticode_der_next(&indirect,
                                   &indirect_offset,
                                   &indirect_child)) {
            return false;
        }
        if (indirect_child.tag == ASN1_TAG_SEQUENCE) {
            if (found_digest_info) {
                return false;
            }
            /*
             * SpcIndirectDataContent contains a Data value followed by
             * DigestInfo. The first sequence is the Data value.
             */
            if (indirect_offset < indirect.content_len) {
                uint32_t probe = indirect_offset;
                authenticode_der_node_t next;

                if (!authenticode_der_read(indirect.content,
                                           indirect.content_len,
                                           probe,
                                           &next)) {
                    return false;
                }
                if (next.tag == ASN1_TAG_SEQUENCE) {
                    digest_info = next;
                    found_digest_info = true;
                    indirect_offset += next.total_len;
                }
            }
        }
    }
    if (!found_digest_info || indirect_offset != indirect.content_len) {
        return false;
    }

    offset = 0;
    if (!authenticode_der_next(&digest_info,
                               &digest_info_offset,
                               &digest_info_child) ||
        !authenticode_der_algorithm(&digest_info_child,
                                    oid_sha256,
                                    sizeof(oid_sha256)) ||
        !authenticode_der_next(&digest_info,
                               &digest_info_offset,
                               &digest_info_child) ||
        !authenticode_der_tag(&digest_info_child, ASN1_TAG_OCTET_STRING) ||
        digest_info_child.content_len != SHA256_DIGEST_SIZE ||
        memcmp(digest_info_child.content,
               file_digest,
               SHA256_DIGEST_SIZE) != 0 ||
        digest_info_offset != digest_info.content_len) {
        return false;
    }
    sha256(indirect.content, indirect.content_len, digest);
    memcpy(content_digest, digest, SHA256_DIGEST_SIZE);
    return true;
}

static bool authenticode_parse_authenticated_attributes(
    const authenticode_der_node_t *signed_attrs,
    const uint8_t content_digest[SHA256_DIGEST_SIZE])
{
    authenticode_der_node_t attribute;
    authenticode_der_node_t attribute_child;
    authenticode_der_node_t value_set;
    authenticode_der_node_t value;
    uint32_t offset = 0;
    bool found_content_type = false;
    bool found_message_digest = false;

    if (signed_attrs == NULL || content_digest == NULL ||
        !authenticode_der_tag(signed_attrs,
                              ASN1_TAG_CONTEXT_SPECIFIC |
                                  ASN1_TAG_CONSTRUCTED | 0U)) {
        return false;
    }
    while (offset < signed_attrs->content_len) {
        uint32_t attribute_offset = 0;

        if (!authenticode_der_next(signed_attrs, &offset, &attribute) ||
            !authenticode_der_tag(&attribute, ASN1_TAG_SEQUENCE) ||
            !authenticode_der_next(&attribute,
                                   &attribute_offset,
                                   &attribute_child) ||
            !authenticode_der_tag(&attribute_child, ASN1_TAG_OID) ||
            !authenticode_der_next(&attribute,
                                   &attribute_offset,
                                   &value_set) ||
            !authenticode_der_tag(&value_set, ASN1_TAG_SET) ||
            attribute_offset != attribute.content_len) {
            return false;
        }

        uint32_t value_offset = 0;
        if (!authenticode_der_next(&value_set, &value_offset, &value) ||
            value_offset != value_set.content_len) {
            return false;
        }
        if (authenticode_der_oid(&attribute_child,
                                 oid_message_digest,
                                 sizeof(oid_message_digest))) {
            if (!authenticode_der_tag(&value, ASN1_TAG_OCTET_STRING) ||
                value.content_len != SHA256_DIGEST_SIZE ||
                memcmp(value.content,
                       content_digest,
                       SHA256_DIGEST_SIZE) != 0) {
                return false;
            }
            found_message_digest = true;
        } else if (authenticode_der_oid(&attribute_child,
                                        oid_content_type,
                                        sizeof(oid_content_type))) {
            if (!authenticode_der_oid(&value,
                                      oid_spc_indirect_data,
                                      sizeof(oid_spc_indirect_data))) {
                return false;
            }
            found_content_type = true;
        }
    }
    return found_content_type && found_message_digest;
}

static bool authenticode_parse_signer_info(
    const authenticode_der_node_t *signer_info,
    x509_name_t *issuer_name,
    uint8_t serial[64],
    uint32_t *serial_len,
    authenticode_der_node_t *signed_attrs,
    authenticode_der_node_t *signature,
    const uint8_t content_digest[SHA256_DIGEST_SIZE])
{
    authenticode_der_node_t child;
    authenticode_der_node_t issuer_serial;
    authenticode_der_node_t issuer_serial_child;
    authenticode_der_node_t digest_algorithm;
    authenticode_der_node_t signature_algorithm;
    uint32_t offset = 0;
    uint32_t issuer_serial_offset = 0;

    if (signer_info == NULL || issuer_name == NULL || serial == NULL ||
        serial_len == NULL || signed_attrs == NULL || signature == NULL ||
        content_digest == NULL ||
        !authenticode_der_tag(signer_info, ASN1_TAG_SEQUENCE) ||
        !authenticode_der_next(signer_info, &offset, &child) ||
        !authenticode_der_tag(&child, ASN1_TAG_INTEGER) ||
        !authenticode_der_next(signer_info, &offset, &issuer_serial) ||
        !authenticode_der_tag(&issuer_serial, ASN1_TAG_SEQUENCE) ||
        !authenticode_der_next(&issuer_serial,
                               &issuer_serial_offset,
                               &issuer_serial_child) ||
        !authenticode_der_tag(&issuer_serial_child, ASN1_TAG_SEQUENCE) ||
        x509_parse_name(issuer_name,
                        issuer_serial_child.encoded,
                        issuer_serial_child.total_len) != 0 ||
        !authenticode_der_next(&issuer_serial,
                               &issuer_serial_offset,
                               &issuer_serial_child) ||
        !authenticode_der_tag(&issuer_serial_child, ASN1_TAG_INTEGER) ||
        issuer_serial_child.content_len == 0 ||
        issuer_serial_child.content_len > 64U ||
        issuer_serial_offset != issuer_serial.content_len) {
        return false;
    }
    memcpy(serial,
           issuer_serial_child.content,
           issuer_serial_child.content_len);
    *serial_len = issuer_serial_child.content_len;

    if (!authenticode_der_next(signer_info, &offset, &digest_algorithm) ||
        !authenticode_der_algorithm(&digest_algorithm,
                                    oid_sha256,
                                    sizeof(oid_sha256)) ||
        !authenticode_der_next(signer_info, &offset, signed_attrs) ||
        !authenticode_parse_authenticated_attributes(signed_attrs,
                                                      content_digest) ||
        !authenticode_der_next(signer_info, &offset, &signature_algorithm) ||
        !authenticode_der_algorithm(&signature_algorithm,
                                    oid_rsa_encryption,
                                    sizeof(oid_rsa_encryption)) ||
        !authenticode_der_next(signer_info, &offset, signature) ||
        !authenticode_der_tag(signature, ASN1_TAG_OCTET_STRING) ||
        signature->content_len == 0 ||
        signature->content_len > RSA_MAX_MODULUS_BYTES) {
        return false;
    }
    while (offset < signer_info->content_len) {
        authenticode_der_node_t unsigned_attributes;

        if (!authenticode_der_next(signer_info,
                                   &offset,
                                   &unsigned_attributes) ||
            unsigned_attributes.tag !=
                (ASN1_TAG_CONTEXT_SPECIFIC |
                 ASN1_TAG_CONSTRUCTED | 1U)) {
            return false;
        }
    }
    return offset == signer_info->content_len;
}

static bool authenticode_verify_cms(const uint8_t *cms,
                                    uint32_t cms_size,
                                    const uint8_t file_digest[SHA256_DIGEST_SIZE],
                                    uint8_t signer_id[AUTHENTICODE_SIGNER_ID_SIZE])
{
    authenticode_der_node_t content_info;
    authenticode_der_node_t child;
    authenticode_der_node_t signed_data;
    authenticode_der_node_t digest_algorithms;
    authenticode_der_node_t encap;
    authenticode_der_node_t certificates;
    authenticode_der_node_t signer_infos;
    authenticode_der_node_t signer_info;
    authenticode_der_node_t signed_attrs;
    authenticode_der_node_t signature;
    x509_name_t signer_issuer;
    x509_chain_t *chain = NULL;
    x509_cert_t *temporary = NULL;
    const x509_trust_store_t *trust_store;
    uint8_t content_digest[SHA256_DIGEST_SIZE];
    uint8_t serial[64];
    uint32_t serial_len = 0;
    uint32_t offset = 0;
    uint32_t certificate_offset;
    uint32_t signer_index = 0;
    bool signer_found = false;
    bool verified = false;
    uint8_t signed_attrs_copy[AUTHENTICODE_MAX_AUTH_ATTRS];
    uint8_t attrs_digest[SHA256_DIGEST_SIZE];

    memset(&signer_infos, 0, sizeof(signer_infos));
    if (cms == NULL || file_digest == NULL || signer_id == NULL ||
        cms_size == 0 || cms_size > AUTHENTICODE_MAX_CMS_SIZE ||
        !authenticode_der_read(cms, cms_size, 0, &content_info) ||
        content_info.total_len != cms_size ||
        !authenticode_der_tag(&content_info, ASN1_TAG_SEQUENCE) ||
        !authenticode_der_next(&content_info, &offset, &child) ||
        !authenticode_der_oid(&child,
                              oid_signed_data,
                              sizeof(oid_signed_data)) ||
        !authenticode_der_next(&content_info, &offset, &child) ||
        !authenticode_der_tag(&child,
                              ASN1_TAG_CONTEXT_SPECIFIC |
                                  ASN1_TAG_CONSTRUCTED | 0U) ||
        !authenticode_der_read(child.content,
                               child.content_len,
                               0,
                               &signed_data) ||
        !authenticode_der_tag(&signed_data, ASN1_TAG_SEQUENCE)) {
        return false;
    }
    if (offset != content_info.content_len) {
        return false;
    }

    chain = (x509_chain_t *) kmalloc(sizeof(*chain));
    if (chain == NULL) {
        return false;
    }
    memset(chain, 0, sizeof(*chain));

    offset = 0;
    if (!authenticode_der_next(&signed_data, &offset, &child) ||
        !authenticode_der_tag(&child, ASN1_TAG_INTEGER) ||
        !authenticode_der_next(&signed_data, &offset, &digest_algorithms) ||
        !authenticode_der_tag(&digest_algorithms, ASN1_TAG_SET) ||
        !authenticode_der_next(&signed_data, &offset, &encap) ||
        !authenticode_parse_spc_digest(&encap,
                                       file_digest,
                                       content_digest)) {
        goto cleanup;
    }
    while (offset < signed_data.content_len) {
        if (!authenticode_der_next(&signed_data, &offset, &child)) {
            goto cleanup;
        }
        if (child.tag ==
            (ASN1_TAG_CONTEXT_SPECIFIC | ASN1_TAG_CONSTRUCTED | 0U)) {
            if (chain->count != 0) {
                goto cleanup;
            }
            certificates = child;
            certificate_offset = 0;
            while (certificate_offset < certificates.content_len) {
                authenticode_der_node_t certificate;

                if (chain->count >= X509_MAX_CHAIN_DEPTH ||
                    !authenticode_der_next(&certificates,
                                           &certificate_offset,
                                           &certificate) ||
                    !authenticode_der_tag(&certificate, ASN1_TAG_SEQUENCE) ||
                    x509_parse_cert(&chain->certs[chain->count],
                                    certificate.encoded,
                                    certificate.total_len) != 0) {
                    goto cleanup;
                }
                chain->count++;
            }
        } else if (child.tag ==
                   (ASN1_TAG_CONTEXT_SPECIFIC |
                    ASN1_TAG_CONSTRUCTED | 1U)) {
            /* Certificate revocation lists are not supported yet. */
        } else if (child.tag == ASN1_TAG_SET) {
            if (signer_infos.encoded != NULL) {
                goto cleanup;
            }
            signer_infos = child;
        } else {
            goto cleanup;
        }
    }
    if (chain->count == 0 || signer_infos.encoded == NULL) {
        goto cleanup;
    }

    offset = 0;
    if (!authenticode_der_next(&signer_infos, &offset, &signer_info) ||
        !authenticode_der_tag(&signer_info, ASN1_TAG_SEQUENCE) ||
        offset != signer_infos.content_len ||
        !authenticode_parse_signer_info(&signer_info,
                                        &signer_issuer,
                                        serial,
                                        &serial_len,
                                        &signed_attrs,
                                        &signature,
                                        content_digest) ||
        signed_attrs.total_len > AUTHENTICODE_MAX_AUTH_ATTRS) {
        goto cleanup;
    }

    for (uint32_t i = 0; i < chain->count; i++) {
        x509_cert_t *certificate = &chain->certs[i];

        if (certificate->serial_len == serial_len &&
            memcmp(certificate->serial_number,
                   serial,
                   serial_len) == 0 &&
            x509_check_name_match(&certificate->issuer,
                                  &signer_issuer) == 0) {
            if (!certificate->eku_present ||
                !certificate->has_code_signing_eku ||
                (certificate->key_usage_present &&
                 (certificate->key_usage & 0x0001U) == 0)) {
                goto cleanup;
            }
            signer_index = i;
            signer_found = true;
            break;
        }
    }
    if (!signer_found) {
        goto cleanup;
    }

    memcpy(signed_attrs_copy,
           signed_attrs.encoded,
           signed_attrs.total_len);
    signed_attrs_copy[0] = ASN1_TAG_SET;
    sha256(signed_attrs_copy, signed_attrs.total_len, attrs_digest);
    log_write("authenticode: verifying CMS RSA signature");
    if (rsa_verify_pkcs1_v15(&chain->certs[signer_index].rsa_key,
                             signature.content,
                             signature.content_len,
                             attrs_digest,
                             SHA256_DIGEST_SIZE,
                             RSA_HASH_SHA256) != 0) {
        goto cleanup;
    }
    log_write("authenticode: CMS RSA signature valid");

    if (signer_index != 0) {
        temporary = (x509_cert_t *) kmalloc(sizeof(*temporary));
        if (temporary == NULL) {
            goto cleanup;
        }
        memcpy(temporary,
               &chain->certs[signer_index],
               sizeof(*temporary));
        for (uint32_t i = signer_index; i > 0; i--) {
            memcpy(&chain->certs[i],
                   &chain->certs[i - 1U],
                   sizeof(chain->certs[i]));
        }
        memcpy(&chain->certs[0], temporary, sizeof(*temporary));
        kfree(temporary);
        temporary = NULL;
    }

    trust_store = trust_store_get();
    if (trust_store == NULL ||
        x509_verify_chain(chain, trust_store) != 0) {
        goto cleanup;
    }
    log_write("authenticode: certificate chain valid");
    sha256(chain->certs[0].raw,
           chain->certs[0].raw_len,
           signer_id);
    verified = true;

cleanup:
    if (temporary != NULL) {
        kfree(temporary);
    }
    if (chain != NULL) {
        kfree(chain);
    }
    return verified;
}

bool authenticode_verify_pe(const uint8_t *data,
                            uint32_t size,
                            uint8_t signer_id[AUTHENTICODE_SIGNER_ID_SIZE])
{
    uint32_t pe_offset;
    uint32_t optional_offset;
    uint32_t security_dir_offset;
    uint32_t cert_offset;
    uint32_t cert_size;
    uint32_t cert_length;
    uint16_t optional_size;
    uint16_t revision;
    uint16_t cert_type;
    uint32_t number_of_rva_and_sizes;
    uint8_t file_digest[SHA256_DIGEST_SIZE];

    if (data == NULL || signer_id == NULL || size < 0x100U ||
        authenticode_read_u16(data, 0) != PE_DOS_MAGIC) {
        return false;
    }
    pe_offset = authenticode_read_u32(data, 0x3CU);
    if (pe_offset > size || size - pe_offset < 24U ||
        authenticode_read_u32(data, pe_offset) != PE_NT_SIGNATURE) {
        return false;
    }
    optional_size = authenticode_read_u16(data, pe_offset + 20U);
    optional_offset = pe_offset + 24U;
    if (optional_size < 120U ||
        optional_offset > size ||
        optional_size > size - optional_offset ||
        authenticode_read_u16(data, optional_offset) !=
            PE_OPTIONAL_MAGIC_PE32_PLUS) {
        return false;
    }
    number_of_rva_and_sizes = authenticode_read_u32(data,
                                                     optional_offset + 108U);
    if (number_of_rva_and_sizes <= PE_DIRECTORY_SECURITY ||
        optional_size < 112U + (PE_DIRECTORY_SECURITY + 1U) * 8U) {
        return false;
    }
    security_dir_offset = optional_offset + 112U +
                          PE_DIRECTORY_SECURITY * 8U;
    cert_offset = authenticode_read_u32(data, security_dir_offset);
    cert_size = authenticode_read_u32(data, security_dir_offset + 4U);
    if (cert_offset == 0 ||
        cert_size < 8U ||
        cert_offset > size ||
        cert_size > size - cert_offset ||
        cert_offset < security_dir_offset + 8U) {
        return false;
    }
    cert_length = authenticode_read_u32(data, cert_offset);
    revision = authenticode_read_u16(data, cert_offset + 4U);
    cert_type = authenticode_read_u16(data, cert_offset + 6U);
    if (cert_length < 8U ||
        cert_length > cert_size ||
        (revision != PE_WIN_CERT_REVISION_1 &&
         revision != PE_WIN_CERT_REVISION_2) ||
        cert_type != PE_WIN_CERT_TYPE_PKCS_SIGNED_DATA ||
        cert_length - 8U > AUTHENTICODE_MAX_CMS_SIZE ||
        !authenticode_compute_digest(data,
                                     size,
                                     optional_offset,
                                     security_dir_offset,
                                     cert_offset,
                                     cert_size,
                                     file_digest)) {
        return false;
    }
    return authenticode_verify_cms(data + cert_offset + 8U,
                                   cert_length - 8U,
                                   file_digest,
                                   signer_id);
}
