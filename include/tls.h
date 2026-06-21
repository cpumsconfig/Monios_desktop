#ifndef _TLS_H_
#define _TLS_H_

#include "stdbool.h"
#include "stdint.h"

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

void tls_init(void);
bool tls_probe_server_name(const char *server_name);
const tls_info_t *tls_info(void);
const char *tls_status(void);

#endif
