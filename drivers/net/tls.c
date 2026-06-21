#include "common.h"
#include "tls.h"

static tls_info_t g_tls_info;

void tls_init(void)
{
    memset(&g_tls_info, 0, sizeof(g_tls_info));
    g_tls_info.initialized = true;
    g_tls_info.ssl_alias = true;
    g_tls_info.x509_parser = true;
    g_tls_info.record_layer = true;
    g_tls_info.handshake_state = true;
    g_tls_info.crypto_backend_ready = false;
    strcpy(g_tls_info.status, "tls: record/x509 framework ready, crypto pending");
}

bool tls_probe_server_name(const char *server_name)
{
    g_tls_info.probes++;
    if (server_name == NULL || server_name[0] == '\0') {
        strcpy(g_tls_info.status, "tls: missing server name");
        return false;
    }
    if (!g_tls_info.crypto_backend_ready) {
        strcpy(g_tls_info.status, "tls: handshake blocked until crypto backend");
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
