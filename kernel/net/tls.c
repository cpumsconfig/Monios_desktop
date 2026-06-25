#include "common.h"
#include "tls.h"
#include "hash.h"
#include "string.h"
#include "memory.h"

static tls_info_t g_tls_info;

/* ============================================================
 *  TLS initialization
 * ============================================================ */

void tls_init(void)
{
    memset(&g_tls_info, 0, sizeof(g_tls_info));
    g_tls_info.initialized = true;
    g_tls_info.ssl_alias = true;
    g_tls_info.x509_parser = true;
    g_tls_info.record_layer = true;
    g_tls_info.handshake_state = true;
    g_tls_info.crypto_backend_ready = true;
    strcpy(g_tls_info.status, "tls: crypto backend ready");
}

bool tls_probe_server_name(const char *server_name)
{
    g_tls_info.probes++;
    if (server_name == NULL || server_name[0] == '\0') {
        strcpy(g_tls_info.status, "tls: missing server name");
        return false;
    }
    strcpy(g_tls_info.status, "tls: handshake state prepared");
    return true;
}

const tls_info_t *tls_info(void)
{
    return &g_tls_info;
}

const char *tls_status(void)
{
    return g_tls_info.status;
}

/* ============================================================
 *  TLS context management
 * ============================================================ */

int32_t tls_ctx_init(tls_ctx_t *ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    memset(ctx, 0, sizeof(tls_ctx_t));
    ctx->state = TLS_STATE_INIT;
    ctx->version = TLS_VERSION_1_2;
    ctx->cipher_suite = TLS_RSA_WITH_AES_128_CBC_SHA;
    return 0;
}

void tls_ctx_free(tls_ctx_t *ctx)
{
    if (ctx) {
        memset(ctx, 0, sizeof(tls_ctx_t));
    }
}

int32_t tls_set_server_name(tls_ctx_t *ctx, const char *server_name)
{
    if (ctx == NULL || server_name == NULL) {
        return -1;
    }
    uint32_t len = strlen((char *) server_name);
    if (len >= sizeof(ctx->server_name)) {
        len = sizeof(ctx->server_name) - 1;
    }
    memcpy(ctx->server_name, server_name, len);
    ctx->server_name[len] = '\0';
    return 0;
}

/* ============================================================
 *  TLS record building
 * ============================================================ */

static void tls_write_uint16(uint8_t *buf, uint16_t val)
{
    buf[0] = (val >> 8) & 0xFF;
    buf[1] = val & 0xFF;
}

static void tls_write_uint24(uint8_t *buf, uint32_t val)
{
    buf[0] = (val >> 16) & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
    buf[2] = val & 0xFF;
}

static uint16_t tls_read_uint16(const uint8_t *buf)
{
    return ((uint16_t) buf[0] << 8) | buf[1];
}

static uint32_t tls_read_uint24(const uint8_t *buf)
{
    return ((uint32_t) buf[0] << 16) | ((uint32_t) buf[1] << 8) | buf[2];
}

int32_t tls_build_record(tls_ctx_t *ctx, uint8_t type,
                         const uint8_t *payload, uint32_t payload_len,
                         uint8_t *out, uint32_t max_len)
{
    if (max_len < 5 + payload_len) {
        return -1;
    }

    out[0] = type;
    tls_write_uint16(&out[1], ctx->version);
    tls_write_uint16(&out[3], payload_len);
    memcpy(&out[5], payload, payload_len);

    return 5 + payload_len;
}

/* ============================================================
 *  TLS ClientHello
 * ============================================================ */

static void tls_generate_random(uint8_t *random)
{
    /* Use a simple pseudo-random for now */
    uint32_t i;
    for (i = 0; i < 32; i++) {
        random[i] = (uint8_t) (i * 0x5F + 0x3C);
    }
}

int32_t tls_build_client_hello(tls_ctx_t *ctx, uint8_t *out, uint32_t max_len)
{
    uint8_t handshake[512];
    uint32_t hs_len = 0;
    uint32_t i;

    if (ctx == NULL || out == NULL) {
        return -1;
    }

    /* Generate client random */
    tls_generate_random(ctx->client_random);

    /* Handshake header */
    handshake[hs_len++] = TLS_HS_CLIENT_HELLO;
    /* Length placeholder (3 bytes) */
    hs_len += 3;

    /* Client version */
    tls_write_uint16(&handshake[hs_len], ctx->version);
    hs_len += 2;

    /* Random */
    memcpy(&handshake[hs_len], ctx->client_random, 32);
    hs_len += 32;

    /* Session ID (empty) */
    handshake[hs_len++] = 0;

    /* Cipher suites */
    uint16_t cipher_suites[] = {
        TLS_RSA_WITH_AES_128_CBC_SHA256,
        TLS_RSA_WITH_AES_256_CBC_SHA256,
        TLS_RSA_WITH_AES_128_CBC_SHA,
        TLS_RSA_WITH_AES_256_CBC_SHA,
    };
    uint16_t num_suites = sizeof(cipher_suites) / sizeof(cipher_suites[0]);
    tls_write_uint16(&handshake[hs_len], num_suites * 2);
    hs_len += 2;
    for (i = 0; i < num_suites; i++) {
        tls_write_uint16(&handshake[hs_len], cipher_suites[i]);
        hs_len += 2;
    }

    /* Compression methods (only null) */
    handshake[hs_len++] = 1;
    handshake[hs_len++] = 0;

    /* Extensions */
    uint32_t ext_start = hs_len;
    hs_len += 2; /* extensions length placeholder */

    /* Server Name Indication (SNI) extension */
    if (ctx->server_name[0] != '\0') {
        uint32_t name_len = strlen(ctx->server_name);
        /* Extension type: server_name (0) */
        tls_write_uint16(&handshake[hs_len], 0);
        hs_len += 2;
        /* Extension data length */
        tls_write_uint16(&handshake[hs_len], name_len + 5);
        hs_len += 2;
        /* Server name list length */
        tls_write_uint16(&handshake[hs_len], name_len + 3);
        hs_len += 2;
        /* Name type: host_name (0) */
        handshake[hs_len++] = 0;
        /* Name length */
        tls_write_uint16(&handshake[hs_len], name_len);
        hs_len += 2;
        /* Name */
        memcpy(&handshake[hs_len], ctx->server_name, name_len);
        hs_len += name_len;
    }

    /* Fill extensions length */
    tls_write_uint16(&handshake[ext_start], hs_len - ext_start - 2);

    /* Fill handshake length */
    tls_write_uint24(&handshake[1], hs_len - 4);

    /* Save handshake messages */
    memcpy(ctx->handshake_messages, handshake, hs_len);
    ctx->handshake_len = hs_len;

    /* Build TLS record */
    ctx->state = TLS_STATE_CLIENT_HELLO_SENT;
    return tls_build_record(ctx, TLS_RECORD_HANDSHAKE, handshake, hs_len, out, max_len);
}

/* ============================================================
 *  TLS record processing
 * ============================================================ */

int32_t tls_process_record(tls_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint8_t type;
    uint16_t version;
    uint16_t record_len;

    if (ctx == NULL || data == NULL || len < 5) {
        return -1;
    }

    type = data[0];
    version = tls_read_uint16(&data[1]);
    record_len = tls_read_uint16(&data[3]);

    if (5 + record_len > len) {
        return -1;
    }

    /* Decrypt if cipher is active */
    if (ctx->server_cipher_ready && type != TLS_RECORD_CHANGE_CIPHER_SPEC) {
        /* TODO: decrypt record */
    }

    /* Process based on type */
    switch (type) {
        case TLS_RECORD_HANDSHAKE:
            return tls_process_handshake(ctx, &data[5], record_len);

        case TLS_RECORD_ALERT:
            if (record_len >= 2) {
                ctx->alert_level = data[5];
                ctx->alert_description = data[6];
                ctx->state = TLS_STATE_ERROR;
            }
            return 0;

        case TLS_RECORD_CHANGE_CIPHER_SPEC:
            if (record_len == 1 && data[5] == 1) {
                ctx->state = TLS_STATE_CHANGE_CIPHER_SPEC_RECEIVED;
                /* TODO: activate server cipher */
            }
            return 0;

        case TLS_RECORD_APPLICATION_DATA:
            /* Application data - caller will handle */
            return 0;

        default:
            return -1;
    }
}

/* ============================================================
 *  TLS handshake processing
 * ============================================================ */

static int32_t tls_process_server_hello(tls_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t offset = 0;

    if (len < 38) {
        return -1;
    }

    /* Version */
    ctx->version = tls_read_uint16(data);
    offset += 2;

    /* Random */
    memcpy(ctx->server_random, &data[offset], 32);
    offset += 32;

    /* Session ID */
    uint8_t session_id_len = data[offset++];
    if (session_id_len > 32) return -1;
    memcpy(ctx->session_id, &data[offset], session_id_len);
    ctx->session_id_len = session_id_len;
    offset += session_id_len;

    /* Cipher suite */
    ctx->cipher_suite = tls_read_uint16(&data[offset]);
    offset += 2;

    /* Compression method */
    offset++;

    /* Save to handshake buffer */
    if (ctx->handshake_len + 4 + len <= sizeof(ctx->handshake_messages)) {
        ctx->handshake_messages[ctx->handshake_len++] = TLS_HS_SERVER_HELLO;
        tls_write_uint24(&ctx->handshake_messages[ctx->handshake_len], len);
        ctx->handshake_len += 3;
        memcpy(&ctx->handshake_messages[ctx->handshake_len], data, len);
        ctx->handshake_len += len;
    }

    ctx->state = TLS_STATE_SERVER_HELLO_RECEIVED;
    return 0;
}

static int32_t tls_process_certificate(tls_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t offset = 0;
    uint32_t certs_len;

    if (len < 3) {
        return -1;
    }

    certs_len = tls_read_uint24(data);
    offset += 3;

    /* Parse certificates */
    ctx->cert_chain.count = 0;

    while (offset < 3 + certs_len && ctx->cert_chain.count < X509_MAX_CHAIN_DEPTH) {
        if (offset + 3 > 3 + certs_len) break;

        uint32_t cert_len = tls_read_uint24(&data[offset]);
        offset += 3;

        if (offset + cert_len > 3 + certs_len) break;

        if (x509_parse_cert(&ctx->cert_chain.certs[ctx->cert_chain.count],
                            &data[offset], cert_len) == 0) {
            ctx->cert_chain.count++;
        }

        offset += cert_len;
    }

    /* Extract server public key from first cert */
    if (ctx->cert_chain.count > 0 && ctx->cert_chain.certs[0].rsa_key_ready) {
        memcpy(&ctx->server_pubkey, &ctx->cert_chain.certs[0].rsa_key, sizeof(rsa_pubkey_t));
        ctx->server_pubkey_ready = true;
    }

    /* Save to handshake buffer */
    if (ctx->handshake_len + 4 + len <= sizeof(ctx->handshake_messages)) {
        ctx->handshake_messages[ctx->handshake_len++] = TLS_HS_CERTIFICATE;
        tls_write_uint24(&ctx->handshake_messages[ctx->handshake_len], len);
        ctx->handshake_len += 3;
        memcpy(&ctx->handshake_messages[ctx->handshake_len], data, len);
        ctx->handshake_len += len;
    }

    ctx->state = TLS_STATE_CERTIFICATE_RECEIVED;
    return 0;
}

static int32_t tls_process_server_hello_done(tls_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    /* Save to handshake buffer */
    if (ctx->handshake_len + 4 + len <= sizeof(ctx->handshake_messages)) {
        ctx->handshake_messages[ctx->handshake_len++] = TLS_HS_SERVER_HELLO_DONE;
        tls_write_uint24(&ctx->handshake_messages[ctx->handshake_len], len);
        ctx->handshake_len += 3;
        memcpy(&ctx->handshake_messages[ctx->handshake_len], data, len);
        ctx->handshake_len += len;
    }

    ctx->state = TLS_STATE_SERVER_HELLO_DONE_RECEIVED;
    return 0;
}

int32_t tls_process_handshake(tls_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t offset = 0;

    while (offset < len) {
        if (offset + 4 > len) {
            return -1;
        }

        uint8_t hs_type = data[offset];
        uint32_t hs_len = tls_read_uint24(&data[offset + 1]);

        if (offset + 4 + hs_len > len) {
            return -1;
        }

        switch (hs_type) {
            case TLS_HS_SERVER_HELLO:
                if (tls_process_server_hello(ctx, &data[offset + 4], hs_len) != 0) {
                    return -1;
                }
                break;

            case TLS_HS_CERTIFICATE:
                if (tls_process_certificate(ctx, &data[offset + 4], hs_len) != 0) {
                    return -1;
                }
                break;

            case TLS_HS_SERVER_KEY_EXCHANGE:
                /* Save to handshake buffer and skip */
                if (ctx->handshake_len + 4 + hs_len <= sizeof(ctx->handshake_messages)) {
                    memcpy(&ctx->handshake_messages[ctx->handshake_len], &data[offset], 4 + hs_len);
                    ctx->handshake_len += 4 + hs_len;
                }
                break;

            case TLS_HS_CERTIFICATE_REQUEST:
                /* Save to handshake buffer and skip */
                if (ctx->handshake_len + 4 + hs_len <= sizeof(ctx->handshake_messages)) {
                    memcpy(&ctx->handshake_messages[ctx->handshake_len], &data[offset], 4 + hs_len);
                    ctx->handshake_len += 4 + hs_len;
                }
                break;

            case TLS_HS_SERVER_HELLO_DONE:
                if (tls_process_server_hello_done(ctx, &data[offset + 4], hs_len) != 0) {
                    return -1;
                }
                break;

            case TLS_HS_FINISHED:
                ctx->state = TLS_STATE_FINISHED_RECEIVED;
                break;

            default:
                break;
        }

        offset += 4 + hs_len;
    }

    return 0;
}

/* ============================================================
 *  TLS PRF and key derivation
 * ============================================================ */

static void tls_p_hash(const uint8_t *secret, uint32_t secret_len,
                       const uint8_t *seed, uint32_t seed_len,
                       uint8_t *output, uint32_t output_len,
                       int32_t use_sha256)
{
    uint8_t a[32];
    uint8_t tmp[32 + 64];
    uint32_t a_len = use_sha256 ? 32 : 20;
    uint32_t hash_len = use_sha256 ? 32 : 20;
    uint32_t offset = 0;

    /* A(0) = seed */
    memcpy(a, seed, seed_len > a_len ? a_len : seed_len);

    while (offset < output_len) {
        /* A(i) = HMAC_hash(secret, A(i-1)) */
        if (use_sha256) {
            hmac_sha256(secret, secret_len, a, a_len, a);
        } else {
            hmac_sha1(secret, secret_len, a, a_len, a);
        }

        /* P_hash = HMAC_hash(secret, A(i) + seed) + ... */
        memcpy(tmp, a, a_len);
        memcpy(&tmp[a_len], seed, seed_len);

        uint8_t digest[32];
        if (use_sha256) {
            hmac_sha256(secret, secret_len, tmp, a_len + seed_len, digest);
        } else {
            hmac_sha1(secret, secret_len, tmp, a_len + seed_len, digest);
        }

        uint32_t copy_len = hash_len;
        if (offset + copy_len > output_len) {
            copy_len = output_len - offset;
        }
        memcpy(&output[offset], digest, copy_len);
        offset += copy_len;
    }
}

int32_t tls_prf(const uint8_t *secret, uint32_t secret_len,
                const char *label, const uint8_t *seed, uint32_t seed_len,
                uint8_t *output, uint32_t output_len)
{
    /* TLS 1.2 PRF uses SHA-256 */
    /* For simplicity, use single hash (SHA-256) */
    uint8_t combined_seed[256];
    uint32_t label_len = strlen(label);

    if (label_len + seed_len > sizeof(combined_seed)) {
        return -1;
    }

    memcpy(combined_seed, label, label_len);
    memcpy(&combined_seed[label_len], seed, seed_len);

    tls_p_hash(secret, secret_len, combined_seed, label_len + seed_len,
               output, output_len, 1);

    return 0;
}

int32_t tls_derive_keys(tls_ctx_t *ctx, const uint8_t *pre_master_secret, uint32_t pms_len)
{
    uint8_t seed[64];
    uint8_t key_block[128];

    if (ctx == NULL || pre_master_secret == NULL) {
        return -1;
    }

    /* seed = client_random + server_random */
    memcpy(seed, ctx->client_random, 32);
    memcpy(&seed[32], ctx->server_random, 32);

    /* master_secret = PRF(premaster_secret, "master secret", client_random + server_random) */
    if (tls_prf(pre_master_secret, pms_len, "master secret",
                seed, 64, ctx->master_secret, 48) != 0) {
        return -1;
    }
    ctx->master_secret_ready = true;

    /* key_block = PRF(master_secret, "key expansion", server_random + client_random) */
    uint8_t expansion_seed[64];
    memcpy(expansion_seed, ctx->server_random, 32);
    memcpy(&expansion_seed[32], ctx->client_random, 32);

    if (tls_prf(ctx->master_secret, 48, "key expansion",
                expansion_seed, 64, key_block, sizeof(key_block)) != 0) {
        return -1;
    }

    /* Derive keys from key_block */
    /* client_write_MAC_secret, server_write_MAC_secret,
       client_write_key, server_write_key,
       client_write_IV, server_write_IV */
    uint32_t mac_key_len = 20; /* SHA-1 */
    uint32_t enc_key_len = 16; /* AES-128 */
    uint32_t iv_len = 16;     /* AES block size */

    uint32_t offset = 0;

    memcpy(ctx->client_mac_key, &key_block[offset], mac_key_len);
    offset += mac_key_len;
    memcpy(ctx->server_mac_key, &key_block[offset], mac_key_len);
    offset += mac_key_len;
    memcpy(ctx->client_write_key, &key_block[offset], enc_key_len);
    offset += enc_key_len;
    memcpy(ctx->server_write_key, &key_block[offset], enc_key_len);
    offset += enc_key_len;
    memcpy(ctx->client_iv, &key_block[offset], iv_len);
    offset += iv_len;
    memcpy(ctx->server_iv, &key_block[offset], iv_len);
    offset += iv_len;

    /* Initialize AES contexts */
    aes_init(&ctx->client_aes, ctx->client_write_key, enc_key_len);
    aes_set_iv(&ctx->client_aes, ctx->client_iv);
    ctx->client_cipher_ready = true;

    aes_init(&ctx->server_aes, ctx->server_write_key, enc_key_len);
    aes_set_iv(&ctx->server_aes, ctx->server_iv);
    ctx->server_cipher_ready = true;

    return 0;
}

/* ============================================================
 *  Certificate verification
 * ============================================================ */

int32_t tls_verify_certificate(tls_ctx_t *ctx, const x509_trust_store_t *trust_store)
{
    if (ctx == NULL || trust_store == NULL) {
        return -1;
    }

    if (ctx->cert_chain.count == 0) {
        return -1;
    }

    ctx->cert_verified = (x509_verify_chain(&ctx->cert_chain, trust_store) == 0);
    return ctx->cert_verified ? 0 : -1;
}

/* ============================================================
 *  Application data
 * ============================================================ */

int32_t tls_encrypt_record(tls_ctx_t *ctx, uint8_t type,
                           const uint8_t *input, uint32_t input_len,
                           uint8_t *output, uint32_t max_output)
{
    if (ctx == NULL || input == NULL || output == NULL) {
        return -1;
    }

    if (!tls_is_connected(ctx)) {
        return -1;
    }

    /* For now, just build plaintext record */
    return tls_build_record(ctx, type, input, input_len, output, max_output);
}

int32_t tls_decrypt_record(tls_ctx_t *ctx, const uint8_t *input, uint32_t input_len,
                           uint8_t *output, uint32_t max_output)
{
    if (ctx == NULL || input == NULL || output == NULL || input_len < 5) {
        return -1;
    }

    uint8_t type = input[0];
    uint16_t record_len = tls_read_uint16(&input[3]);

    if (type != TLS_RECORD_APPLICATION_DATA) {
        return -1;
    }

    if (5 + record_len > input_len) {
        return -1;
    }

    if (record_len > max_output) {
        return -1;
    }

    /* For now, just copy plaintext */
    memcpy(output, &input[5], record_len);
    return record_len;
}

/* ============================================================
 *  State checks
 * ============================================================ */

bool tls_is_connected(const tls_ctx_t *ctx)
{
    return ctx && ctx->state == TLS_STATE_HANDSHAKE_DONE;
}

bool tls_has_error(const tls_ctx_t *ctx)
{
    return ctx && ctx->state == TLS_STATE_ERROR;
}

/* ============================================================
 *  High-level API (integrated with socket)
 * ============================================================ */

#include "socket.h"

tls_ctx_t *tls_create(void)
{
    tls_ctx_t *ctx = (tls_ctx_t *) kmalloc(sizeof(tls_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }

    if (tls_ctx_init(ctx) != 0) {
        kfree(ctx);
        return NULL;
    }

    ctx->socket = -1;
    ctx->socket_owned = false;

    return ctx;
}

void tls_free(tls_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->socket_owned && ctx->socket >= 0) {
        socket_close(ctx->socket);
    }

    tls_ctx_free(ctx);
    kfree(ctx);
}

static bool tls_send_raw(tls_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    if (ctx == NULL || ctx->socket < 0) {
        return false;
    }

    int32_t sent = 0;
    uint32_t offset = 0;

    while (offset < len) {
        int32_t ret = socket_tcp_send(ctx->socket, data + offset, (uint16_t) (len - offset));
        if (ret < 0) {
            return false;
        }
        if (ret == 0) {
            break;
        }
        offset += ret;
        sent += ret;
    }

    return sent == (int32_t) len;
}

static int32_t tls_recv_raw(tls_ctx_t *ctx, uint8_t *buffer, uint32_t buffer_size)
{
    if (ctx == NULL || ctx->socket < 0) {
        return -1;
    }

    if (!socket_tcp_has_data(ctx->socket)) {
        return 0;
    }

    return socket_tcp_recv(ctx->socket, buffer, (uint16_t) buffer_size);
}

bool tls_connect(tls_ctx_t *ctx, const char *host, uint16_t port)
{
    uint8_t hello_buf[1024];
    int32_t hello_len;

    if (ctx == NULL || host == NULL) {
        return false;
    }

    /* 打开 socket */
    ctx->socket = socket_tcp_open(0);
    if (ctx->socket < 0) {
        return false;
    }
    ctx->socket_owned = true;

    /* 连接 */
    if (!socket_tcp_connect(ctx->socket, host, port)) {
        return false;
    }

    /* 等待 TCP 连接建立 */
    uint32_t timeout = 0;
    while (!socket_tcp_is_connected(ctx->socket) && timeout < 500000) {
        timeout++;
    }

    if (!socket_tcp_is_connected(ctx->socket)) {
        return false;
    }

    /* 构建 ClientHello */
    hello_len = tls_build_client_hello(ctx, hello_buf, sizeof(hello_buf));
    if (hello_len <= 0) {
        return false;
    }

    /* 发送 ClientHello */
    if (!tls_send_raw(ctx, hello_buf, (uint32_t) hello_len)) {
        return false;
    }

    ctx->state = TLS_STATE_CLIENT_HELLO_SENT;

    return true;
}

static bool tls_process_rx_data(tls_ctx_t *ctx)
{
    uint8_t tmp_buf[4096];
    int32_t ret;

    /* 从 socket 读取数据到 rx_buffer */
    ret = tls_recv_raw(ctx, tmp_buf, sizeof(tmp_buf));
    if (ret > 0) {
        uint32_t space = sizeof(ctx->rx_buffer) - ctx->rx_len;
        if ((uint32_t) ret > space) {
            ret = (int32_t) space;
        }
        if (ret > 0) {
            memcpy(ctx->rx_buffer + ctx->rx_len, tmp_buf, ret);
            ctx->rx_len += ret;
        }
    }

    /* 处理完整的 TLS 记录 */
    while (ctx->rx_len - ctx->rx_read_pos >= 5) {
        const uint8_t *record = ctx->rx_buffer + ctx->rx_read_pos;
        uint16_t record_len = ((uint16_t) record[3] << 8) | record[4];

        if (ctx->rx_len - ctx->rx_read_pos < 5 + record_len) {
            /* 记录不完整，等待更多数据 */
            break;
        }

        /* 处理这条记录 */
        int32_t processed = tls_process_record(ctx, record, 5 + record_len);
        if (processed < 0) {
            return false;
        }

        ctx->rx_read_pos += 5 + record_len;

        /* 如果是应用数据，解密并放到 app_buffer */
        if (record[0] == TLS_RECORD_APPLICATION_DATA && ctx->server_cipher_ready) {
            uint8_t decrypted[4096];
            int32_t dec_len = tls_decrypt_record(ctx, record, 5 + record_len,
                                        decrypted, sizeof(decrypted));
            if (dec_len > 0) {
                uint32_t space = sizeof(ctx->app_buffer) - ctx->app_len;
                if ((uint32_t) dec_len > space) {
                    dec_len = (int32_t) space;
                }
                if (dec_len > 0) {
                    memcpy(ctx->app_buffer + ctx->app_len, decrypted, dec_len);
                    ctx->app_len += dec_len;
                }
            }
        }
    }

    /* 清理已处理的数据 */
    if (ctx->rx_read_pos > 0) {
        uint32_t remaining = ctx->rx_len - ctx->rx_read_pos;
        if (remaining > 0) {
            memmove(ctx->rx_buffer, ctx->rx_buffer + ctx->rx_read_pos, remaining);
        }
        ctx->rx_len = remaining;
        ctx->rx_read_pos = 0;
    }

    return true;
}

bool tls_poll(tls_ctx_t *ctx)
{
    if (ctx == NULL) {
        return false;
    }

    if (ctx->state == TLS_STATE_ERROR) {
        return false;
    }

    return tls_process_rx_data(ctx);
}

int32_t tls_write(tls_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint8_t record_buf[4096];
    int32_t record_len;

    if (ctx == NULL || data == NULL || len == 0) {
        return -1;
    }

    if (!tls_is_connected(ctx)) {
        return -1;
    }

    /* 构建加密记录 */
    record_len = tls_encrypt_record(ctx, TLS_RECORD_APPLICATION_DATA, data, len,
                                    record_buf, sizeof(record_buf));
    if (record_len <= 0) {
        return -1;
    }

    /* 发送 */
    if (!tls_send_raw(ctx, record_buf, (uint32_t) record_len)) {
        return -1;
    }

    return (int32_t) len;
}

int32_t tls_read(tls_ctx_t *ctx, uint8_t *buffer, uint32_t buffer_size)
{
    if (ctx == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }

    /* 先尝试从 app_buffer 读取 */
    if (ctx->app_read_pos < ctx->app_len) {
        uint32_t available = ctx->app_len - ctx->app_read_pos;
        if (available > buffer_size) {
            available = buffer_size;
        }
        memcpy(buffer, ctx->app_buffer + ctx->app_read_pos, available);
        ctx->app_read_pos += available;

        /* 如果读完了，重置 */
        if (ctx->app_read_pos >= ctx->app_len) {
            ctx->app_len = 0;
            ctx->app_read_pos = 0;
        }

        return (int32_t) available;
    }

    /* 尝试读取更多数据 */
    tls_poll(ctx);

    /* 再试一次 */
    if (ctx->app_read_pos < ctx->app_len) {
        uint32_t available = ctx->app_len - ctx->app_read_pos;
        if (available > buffer_size) {
            available = buffer_size;
        }
        memcpy(buffer, ctx->app_buffer + ctx->app_read_pos, available);
        ctx->app_read_pos += available;

        if (ctx->app_read_pos >= ctx->app_len) {
            ctx->app_len = 0;
            ctx->app_read_pos = 0;
        }

        return (int32_t) available;
    }

    return 0;
}

void tls_close(tls_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->socket_owned && ctx->socket >= 0) {
        socket_close(ctx->socket);
        ctx->socket = -1;
        ctx->socket_owned = false;
    }

    ctx->state = TLS_STATE_INIT;
}
