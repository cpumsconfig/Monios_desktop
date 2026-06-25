#ifndef _TLS_H_
#define _TLS_H_

#include "stdbool.h"
#include "stdint.h"
#include "aes.h"
#include "x509.h"

/* TLS versions */
#define TLS_VERSION_1_0 0x0301
#define TLS_VERSION_1_1 0x0302
#define TLS_VERSION_1_2 0x0303

/* TLS record types */
#define TLS_RECORD_CHANGE_CIPHER_SPEC 20
#define TLS_RECORD_ALERT              21
#define TLS_RECORD_HANDSHAKE          22
#define TLS_RECORD_APPLICATION_DATA   23

/* TLS handshake types */
#define TLS_HS_HELLO_REQUEST        0
#define TLS_HS_CLIENT_HELLO         1
#define TLS_HS_SERVER_HELLO         2
#define TLS_HS_CERTIFICATE         11
#define TLS_HS_SERVER_KEY_EXCHANGE 12
#define TLS_HS_CERTIFICATE_REQUEST 13
#define TLS_HS_SERVER_HELLO_DONE   14
#define TLS_HS_CERTIFICATE_VERIFY  15
#define TLS_HS_CLIENT_KEY_EXCHANGE 16
#define TLS_HS_FINISHED            20

/* TLS cipher suites (common ones) */
#define TLS_RSA_WITH_AES_128_CBC_SHA      0x002F
#define TLS_RSA_WITH_AES_256_CBC_SHA      0x0035
#define TLS_RSA_WITH_AES_128_CBC_SHA256   0x003C
#define TLS_RSA_WITH_AES_256_CBC_SHA256   0x003D

/* TLS alert levels */
#define TLS_ALERT_WARNING 1
#define TLS_ALERT_FATAL   2

/* TLS alert descriptions */
#define TLS_ALERT_CLOSE_NOTIFY            0
#define TLS_ALERT_UNEXPECTED_MESSAGE     10
#define TLS_ALERT_BAD_RECORD_MAC         20
#define TLS_ALERT_DECRYPTION_FAILED      21
#define TLS_ALERT_RECORD_OVERFLOW        22
#define TLS_ALERT_DECOMPRESSION_FAILURE  30
#define TLS_ALERT_HANDSHAKE_FAILURE      40
#define TLS_ALERT_BAD_CERTIFICATE        42
#define TLS_ALERT_UNSUPPORTED_CERT       43
#define TLS_ALERT_CERTIFICATE_REVOKED    44
#define TLS_ALERT_CERTIFICATE_EXPIRED    45
#define TLS_ALERT_CERTIFICATE_UNKNOWN    46
#define TLS_ALERT_ILLEGAL_PARAMETER      47
#define TLS_ALERT_UNKNOWN_CA             48
#define TLS_ALERT_ACCESS_DENIED          49
#define TLS_ALERT_DECODE_ERROR           50
#define TLS_ALERT_DECRYPT_ERROR          51
#define TLS_ALERT_PROTOCOL_VERSION       70
#define TLS_ALERT_INSUFFICIENT_SECURITY  71
#define TLS_ALERT_INTERNAL_ERROR         80
#define TLS_ALERT_USER_CANCELED          90
#define TLS_ALERT_NO_RENEGOTIATION      100

/* TLS states */
typedef enum {
    TLS_STATE_INIT = 0,
    TLS_STATE_CLIENT_HELLO_SENT,
    TLS_STATE_SERVER_HELLO_RECEIVED,
    TLS_STATE_CERTIFICATE_RECEIVED,
    TLS_STATE_SERVER_HELLO_DONE_RECEIVED,
    TLS_STATE_CLIENT_KEY_EXCHANGE_SENT,
    TLS_STATE_CHANGE_CIPHER_SPEC_SENT,
    TLS_STATE_FINISHED_SENT,
    TLS_STATE_CHANGE_CIPHER_SPEC_RECEIVED,
    TLS_STATE_FINISHED_RECEIVED,
    TLS_STATE_HANDSHAKE_DONE,
    TLS_STATE_ERROR
} tls_state_t;

/* TLS connection context */
typedef struct {
    tls_state_t state;
    uint16_t version;

    /* Socket handle */
    int32_t socket;
    bool socket_owned;

    /* Random values */
    uint8_t client_random[32];
    uint8_t server_random[32];

    /* Session ID */
    uint8_t session_id[32];
    uint32_t session_id_len;

    /* Cipher suite */
    uint16_t cipher_suite;

    /* Keys */
    uint8_t master_secret[48];
    bool master_secret_ready;

    uint8_t client_write_key[32];
    uint8_t server_write_key[32];
    uint8_t client_mac_key[32];
    uint8_t server_mac_key[32];
    uint8_t client_iv[16];
    uint8_t server_iv[16];

    /* Sequence numbers */
    uint64_t client_seq;
    uint64_t server_seq;

    /* AES contexts */
    aes_ctx_t client_aes;
    aes_ctx_t server_aes;
    bool client_cipher_ready;
    bool server_cipher_ready;

    /* Certificate chain */
    x509_chain_t cert_chain;
    bool cert_verified;

    /* Server public key */
    rsa_pubkey_t server_pubkey;
    bool server_pubkey_ready;

    /* Handshake messages buffer (for Finished verify) */
    uint8_t handshake_messages[4096];
    uint32_t handshake_len;

    /* Receive buffer */
    uint8_t rx_buffer[16384];
    uint32_t rx_len;
    uint32_t rx_read_pos;

    /* Application data buffer */
    uint8_t app_buffer[8192];
    uint32_t app_len;
    uint32_t app_read_pos;

    /* Error */
    char error_msg[128];
    uint8_t alert_level;
    uint8_t alert_description;

    /* Server name */
    char server_name[256];
} tls_ctx_t;

/* TLS info structure */
typedef struct {
    bool initialized;
    bool ssl_alias;
    bool x509_parser;
    bool record_layer;
    bool handshake_state;
    bool crypto_backend_ready;
    uint32_t probes;
    char status[64];
} tls_info_t;

/* TLS initialization */
void tls_init(void);
const tls_info_t *tls_info(void);
const char *tls_status(void);

/* TLS connection */
int32_t tls_ctx_init(tls_ctx_t *ctx);
void tls_ctx_free(tls_ctx_t *ctx);
int32_t tls_set_server_name(tls_ctx_t *ctx, const char *server_name);
bool tls_probe_server_name(const char *server_name);

/* TLS handshake */
int32_t tls_build_client_hello(tls_ctx_t *ctx, uint8_t *out, uint32_t max_len);
int32_t tls_process_record(tls_ctx_t *ctx, const uint8_t *data, uint32_t len);
int32_t tls_process_handshake(tls_ctx_t *ctx, const uint8_t *data, uint32_t len);

/* TLS record layer */
int32_t tls_build_record(tls_ctx_t *ctx, uint8_t type,
                         const uint8_t *payload, uint32_t payload_len,
                         uint8_t *out, uint32_t max_len);
int32_t tls_decrypt_record(tls_ctx_t *ctx, const uint8_t *input, uint32_t input_len,
                           uint8_t *output, uint32_t max_output);
int32_t tls_encrypt_record(tls_ctx_t *ctx, uint8_t type,
                           const uint8_t *input, uint32_t input_len,
                           uint8_t *output, uint32_t max_output);

/* TLS key derivation */
int32_t tls_derive_keys(tls_ctx_t *ctx, const uint8_t *pre_master_secret, uint32_t pms_len);
int32_t tls_prf(const uint8_t *secret, uint32_t secret_len,
                const char *label, const uint8_t *seed, uint32_t seed_len,
                uint8_t *output, uint32_t output_len);

/* Certificate verification */
int32_t tls_verify_certificate(tls_ctx_t *ctx, const x509_trust_store_t *trust_store);

/* State check */
bool tls_is_connected(const tls_ctx_t *ctx);
bool tls_has_error(const tls_ctx_t *ctx);

/* High-level API (integrated with socket) */
tls_ctx_t *tls_create(void);
void tls_free(tls_ctx_t *ctx);
int32_t tls_set_server_name(tls_ctx_t *ctx, const char *server_name);
bool tls_connect(tls_ctx_t *ctx, const char *host, uint16_t port);
int32_t tls_write(tls_ctx_t *ctx, const uint8_t *data, uint32_t len);
int32_t tls_read(tls_ctx_t *ctx, uint8_t *buffer, uint32_t buffer_size);
bool tls_poll(tls_ctx_t *ctx);
void tls_close(tls_ctx_t *ctx);

#endif
