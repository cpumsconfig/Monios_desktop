#include "common.h"
#include "http.h"
#include "tls.h"

static http_info_t g_http_info;

static bool http_has_prefix(const char *text, const char *prefix)
{
    uint32_t index = 0;

    if (text == NULL || prefix == NULL) {
        return false;
    }
    while (prefix[index] != '\0') {
        if (text[index] != prefix[index]) {
            return false;
        }
        index++;
    }
    return true;
}

static void http_copy(char *dst, uint32_t size, const char *src)
{
    uint32_t i = 0;

    if (size == 0) {
        return;
    }
    while (src != NULL && src[i] != '\0' && i + 1 < size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void http_init(void)
{
    memset(&g_http_info, 0, sizeof(g_http_info));
    g_http_info.initialized = true;
    g_http_info.http_enabled = true;
    g_http_info.https_enabled = true;
    g_http_info.parser_ready = true;
    g_http_info.client_ready = false;
    strcpy(g_http_info.status, "http: parser/request builder ready");
}

bool http_build_get_request(const char *host, const char *path, char *out, uint32_t out_size)
{
    const char *request_path = path;
    uint32_t need;

    if (host == NULL || host[0] == '\0' || out == NULL || out_size == 0) {
        strcpy(g_http_info.status, "http: bad request");
        return false;
    }
    if (request_path == NULL || request_path[0] == '\0') {
        request_path = "/";
    }
    need = (uint32_t) strlen("GET  HTTP/1.1\r\nHost: \r\nConnection: close\r\nUser-Agent: MoniOS/0.1\r\n\r\n") +
           (uint32_t) strlen(host) + (uint32_t) strlen(request_path);
    if (need + 1 > out_size) {
        strcpy(g_http_info.status, "http: output too small");
        return false;
    }
    out[0] = '\0';
    strcpy(out, "GET ");
    strcat(out, request_path);
    strcat(out, " HTTP/1.1\r\nHost: ");
    strcat(out, host);
    strcat(out, "\r\nConnection: close\r\nUser-Agent: MoniOS/0.1\r\n\r\n");
    g_http_info.requests_built++;
    strcpy(g_http_info.status, "http: request built");
    return true;
}

bool http_probe_url(const char *url)
{
    bool https = false;

    if (url == NULL || url[0] == '\0') {
        strcpy(g_http_info.status, "http: missing url");
        return false;
    }
    http_copy(g_http_info.last_url, sizeof(g_http_info.last_url), url);
    if (http_has_prefix(url, "https://")) {
        https = true;
        g_http_info.https_probes++;
        tls_probe_server_name(url + 8);
    }
    if (!http_has_prefix(url, "http://") && !https) {
        strcpy(g_http_info.status, "http: unsupported url scheme");
        return false;
    }
    strcpy(g_http_info.status, https ? "https: tls probe recorded" : "http: url accepted");
    return !https || tls_info()->crypto_backend_ready;
}

const http_info_t *http_info(void)
{
    return &g_http_info;
}

const char *http_status(void)
{
    return g_http_info.status;
}
