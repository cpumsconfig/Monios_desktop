#include "common.h"
#include "http.h"
#include "tls.h"
#include "socket.h"
#include "dns.h"
#include "string.h"

static http_info_t g_http_info;

/* ============================================================
 *  工具函数
 * ============================================================ */

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

static int http_strcasecmp(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return -1;
    }
    while (*a != '\0' && *b != '\0') {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) {
            return ca - cb;
        }
        a++;
        b++;
    }
    return *a - *b;
}

/* ============================================================
 *  URL 解析
 * ============================================================ */

bool http_parse_url(const char *url, http_url_t *parsed)
{
    const char *p;
    const char *host_start;
    const char *path_start;
    const char *port_start;
    uint32_t i;

    if (url == NULL || parsed == NULL) {
        return false;
    }

    memset(parsed, 0, sizeof(*parsed));

    /* 解析 scheme */
    if (http_has_prefix(url, "https://")) {
        strcpy(parsed->scheme, "https");
        parsed->port = 443;
        p = url + 8;
    } else if (http_has_prefix(url, "http://")) {
        strcpy(parsed->scheme, "http");
        parsed->port = 80;
        p = url + 7;
    } else {
        /* 默认 http */
        strcpy(parsed->scheme, "http");
        parsed->port = 80;
        p = url;
    }

    /* 解析 host 和 port */
    host_start = p;
    path_start = NULL;
    port_start = NULL;

    while (*p != '\0') {
        if (*p == '/') {
            path_start = p;
            break;
        }
        if (*p == ':') {
            port_start = p + 1;
        }
        p++;
    }

    /* 复制 host */
    i = 0;
    while (host_start < p && host_start != port_start - 1 && i + 1 < HTTP_MAX_HOST) {
        if (port_start != NULL && host_start >= port_start) {
            break;
        }
        parsed->host[i++] = *host_start++;
    }
    parsed->host[i] = '\0';

    /* 解析 port */
    if (port_start != NULL && path_start != NULL && port_start < path_start) {
        uint16_t port = 0;
        const char *pp = port_start;
        while (pp < path_start && *pp >= '0' && *pp <= '9') {
            port = port * 10 + (*pp - '0');
            pp++;
        }
        if (port > 0) {
            parsed->port = port;
        }
    }

    /* 解析 path */
    if (path_start != NULL) {
        http_copy(parsed->path, HTTP_MAX_PATH, path_start);
    } else {
        strcpy(parsed->path, "/");
    }

    return parsed->host[0] != '\0';
}

/* ============================================================
 *  请求构建
 * ============================================================ */

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

bool http_build_post_request(const char *host, const char *path,
                             const char *content_type, const uint8_t *body, uint32_t body_len,
                             char *out, uint32_t out_size)
{
    const char *request_path = path;
    char content_len_str[16];
    uint32_t need;

    if (host == NULL || host[0] == '\0' || out == NULL || out_size == 0) {
        return false;
    }
    if (request_path == NULL || request_path[0] == '\0') {
        request_path = "/";
    }

    /* 计算 Content-Length 字符串 */
    {
        uint32_t len = body_len;
        char buf[16];
        int digits = 0;
        if (len == 0) {
            content_len_str[0] = '0';
            content_len_str[1] = '\0';
        } else {
            while (len > 0) {
                buf[digits++] = '0' + (len % 10);
                len /= 10;
            }
            for (int i = 0; i < digits; i++) {
                content_len_str[i] = buf[digits - 1 - i];
            }
            content_len_str[digits] = '\0';
        }
    }

    need = (uint32_t) strlen("POST  HTTP/1.1\r\nHost: \r\nContent-Type: \r\nContent-Length: \r\nConnection: close\r\nUser-Agent: MoniOS/0.1\r\n\r\n") +
           (uint32_t) strlen(host) + (uint32_t) strlen(request_path) +
           (uint32_t) strlen(content_type ? content_type : "application/octet-stream") +
           (uint32_t) strlen(content_len_str) + body_len;

    if (need + 1 > out_size) {
        return false;
    }

    out[0] = '\0';
    strcpy(out, "POST ");
    strcat(out, request_path);
    strcat(out, " HTTP/1.1\r\nHost: ");
    strcat(out, host);
    strcat(out, "\r\nContent-Type: ");
    strcat(out, content_type ? content_type : "application/octet-stream");
    strcat(out, "\r\nContent-Length: ");
    strcat(out, content_len_str);
    strcat(out, "\r\nConnection: close\r\nUser-Agent: MoniOS/0.1\r\n\r\n");

    if (body != NULL && body_len > 0) {
        memcpy(out + strlen(out), body, body_len);
    }

    g_http_info.requests_built++;
    return true;
}

/* ============================================================
 *  响应解析
 * ============================================================ */

static const char *http_find_line_end(const char *data, uint32_t length)
{
    for (uint32_t i = 0; i + 1 < length; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            return data + i;
        }
    }
    return NULL;
}

static int32_t http_parse_status_code(const char *line)
{
    const char *space1;
    const char *space2;
    int32_t code = 0;

    if (line == NULL) {
        return -1;
    }

    space1 = strchr(line, ' ');
    if (space1 == NULL) {
        return -1;
    }
    space1++;

    space2 = strchr(space1, ' ');
    if (space2 == NULL) {
        return -1;
    }

    while (space1 < space2) {
        if (*space1 >= '0' && *space1 <= '9') {
            code = code * 10 + (*space1 - '0');
        } else {
            break;
        }
        space1++;
    }

    return code;
}

bool http_parse_response(const uint8_t *data, uint32_t length, http_response_t *response)
{
    const char *p = (const char *) data;
    const char *end = (const char *) data + length;
    const char *line_end;
    uint32_t line_len;

    if (data == NULL || length == 0 || response == NULL) {
        return false;
    }

    memset(response, 0, sizeof(*response));

    /* 解析状态行 */
    line_end = http_find_line_end(p, (uint32_t) (end - p));
    if (line_end == NULL) {
        return false;
    }

    line_len = (uint32_t) (line_end - p);

    /* 解析版本 */
    {
        const char *space = strchr(p, ' ');
        if (space != NULL && space < line_end) {
            uint32_t ver_len = (uint32_t) (space - p);
            if (ver_len >= sizeof(response->version)) {
                ver_len = sizeof(response->version) - 1;
            }
            memcpy(response->version, p, ver_len);
            response->version[ver_len] = '\0';
        }
    }

    /* 解析状态码 */
    response->status_code = http_parse_status_code(p);

    /* 解析状态文本 */
    {
        const char *space1 = strchr(p, ' ');
        if (space1 != NULL) {
            const char *space2 = strchr(space1 + 1, ' ');
            if (space2 != NULL && space2 < line_end) {
                const char *text_start = space2 + 1;
                uint32_t text_len = (uint32_t) (line_end - text_start);
                if (text_len >= sizeof(response->status_text)) {
                    text_len = sizeof(response->status_text) - 1;
                }
                memcpy(response->status_text, text_start, text_len);
                response->status_text[text_len] = '\0';
            }
        }
    }

    p = line_end + 2;

    /* 解析响应头 */
    response->header_count = 0;
    response->content_length = 0;
    response->chunked = false;
    response->keep_alive = false;

    while (p < end && response->header_count < HTTP_MAX_HEADERS) {
        line_end = http_find_line_end(p, (uint32_t) (end - p));
        if (line_end == NULL) {
            break;
        }

        line_len = (uint32_t) (line_end - p);

        /* 空行表示头部结束 */
        if (line_len == 0) {
            p = line_end + 2;
            break;
        }

        /* 解析 header name 和 value */
        {
            const char *colon = strchr(p, ':');
            if (colon != NULL && colon < line_end) {
                uint32_t name_len = (uint32_t) (colon - p);
                const char *value_start = colon + 1;

                /* 跳过 value 前的空格 */
                while (value_start < line_end && *value_start == ' ') {
                    value_start++;
                }

                uint32_t value_len = (uint32_t) (line_end - value_start);

                if (name_len >= HTTP_MAX_HEADER_NAME) {
                    name_len = HTTP_MAX_HEADER_NAME - 1;
                }
                if (value_len >= HTTP_MAX_HEADER_VALUE) {
                    value_len = HTTP_MAX_HEADER_VALUE - 1;
                }

                memcpy(response->headers[response->header_count].name, p, name_len);
                response->headers[response->header_count].name[name_len] = '\0';
                memcpy(response->headers[response->header_count].value, value_start, value_len);
                response->headers[response->header_count].value[value_len] = '\0';

                /* 检查特殊 header */
                if (http_strcasecmp(response->headers[response->header_count].name, "Content-Length") == 0) {
                    const char *v = response->headers[response->header_count].value;
                    uint32_t cl = 0;
                    while (*v >= '0' && *v <= '9') {
                        cl = cl * 10 + (*v - '0');
                        v++;
                    }
                    response->content_length = cl;
                }
                if (http_strcasecmp(response->headers[response->header_count].name, "Transfer-Encoding") == 0) {
                    if (http_strcasecmp(response->headers[response->header_count].value, "chunked") == 0) {
                        response->chunked = true;
                    }
                }
                if (http_strcasecmp(response->headers[response->header_count].name, "Connection") == 0) {
                    if (http_strcasecmp(response->headers[response->header_count].value, "keep-alive") == 0) {
                        response->keep_alive = true;
                    }
                }

                response->header_count++;
            }
        }

        p = line_end + 2;
    }

    /* 响应体 */
    if (p < end) {
        response->body = (const uint8_t *) p;
        response->body_length = (uint32_t) (end - p);
    }

    return response->status_code > 0;
}

const char *http_get_header(const http_response_t *response, const char *name)
{
    if (response == NULL || name == NULL) {
        return NULL;
    }

    for (uint32_t i = 0; i < response->header_count; i++) {
        if (http_strcasecmp(response->headers[i].name, name) == 0) {
            return response->headers[i].value;
        }
    }

    return NULL;
}

int32_t http_get_content_length(const http_response_t *response)
{
    if (response == NULL) {
        return -1;
    }
    return (int32_t) response->content_length;
}

bool http_is_chunked(const http_response_t *response)
{
    if (response == NULL) {
        return false;
    }
    return response->chunked;
}

/* ============================================================
 *  Chunked 解码
 * ============================================================ */

static uint32_t http_decode_chunked(const uint8_t *input, uint32_t input_len,
                                     uint8_t *output, uint32_t output_size)
{
    const uint8_t *p = input;
    const uint8_t *end = input + input_len;
    uint32_t output_len = 0;

    while (p < end) {
        /* 解析 chunk size */
        uint32_t chunk_size = 0;
        while (p < end) {
            char c = *p;
            if (c >= '0' && c <= '9') {
                chunk_size = chunk_size * 16 + (c - '0');
            } else if (c >= 'a' && c <= 'f') {
                chunk_size = chunk_size * 16 + (c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                chunk_size = chunk_size * 16 + (c - 'A' + 10);
            } else {
                break;
            }
            p++;
        }

        /* 跳过 \r\n */
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;

        if (chunk_size == 0) {
            break;
        }

        /* 复制 chunk 数据 */
        if (output_len + chunk_size > output_size) {
            chunk_size = output_size - output_len;
        }
        if (chunk_size > 0 && p + chunk_size <= end) {
            memcpy(output + output_len, p, chunk_size);
            output_len += chunk_size;
        }

        p += chunk_size;

        /* 跳过 \r\n */
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;
    }

    return output_len;
}

/* ============================================================
 *  HTTP 客户端
 * ============================================================ */

void http_init(void)
{
    memset(&g_http_info, 0, sizeof(g_http_info));
    g_http_info.initialized = true;
    g_http_info.http_enabled = true;
    g_http_info.https_enabled = true;
    g_http_info.parser_ready = true;
    g_http_info.client_ready = true;
    g_http_info.last_status_code = 0;
    strcpy(g_http_info.status, "http: client ready");
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

int32_t http_get(const char *host, const char *path, char *response_buffer, uint32_t buffer_size)
{
    char request[1024];
    int32_t sock;
    int32_t total = 0;
    int32_t ret;
    uint32_t timeout;

    if (host == NULL || host[0] == '\0' || response_buffer == NULL || buffer_size == 0) {
        strcpy(g_http_info.status, "http: bad parameters");
        return -1;
    }

    http_copy(g_http_info.last_host, sizeof(g_http_info.last_host), host);
    http_copy(g_http_info.last_path, sizeof(g_http_info.last_path), path ? path : "/");

    if (!http_build_get_request(host, path, request, sizeof(request))) {
        return -1;
    }

    sock = socket_tcp_open(0);
    if (sock < 0) {
        strcpy(g_http_info.status, "http: socket open failed");
        return -1;
    }

    if (!socket_tcp_connect(sock, host, 80)) {
        socket_close(sock);
        strcpy(g_http_info.status, "http: connect failed");
        return -1;
    }

    /* 等待连接建立 */
    timeout = 0;
    while (!socket_tcp_is_connected(sock) && timeout < 500000) {
        timeout++;
    }

    if (!socket_tcp_is_connected(sock)) {
        socket_close(sock);
        strcpy(g_http_info.status, "http: connect timeout");
        return -1;
    }

    g_http_info.requests_sent++;

    ret = socket_tcp_send(sock, (const uint8_t *) request, (uint16_t) strlen(request));
    if (ret < 0) {
        socket_close(sock);
        strcpy(g_http_info.status, "http: send failed");
        return -1;
    }

    /* 接收响应 */
    total = 0;
    timeout = 0;
    while (timeout < 1000000) {
        if (socket_tcp_has_data(sock)) {
            ret = socket_tcp_recv(sock, (uint8_t *) (response_buffer + total),
                                  (uint16_t) (buffer_size - total - 1));
            if (ret > 0) {
                total += ret;
                timeout = 0;
            }
        }

        if (!socket_tcp_is_connected(sock)) {
            /* 连接关闭，再尝试接收一次 */
            if (socket_tcp_has_data(sock)) {
                ret = socket_tcp_recv(sock, (uint8_t *) (response_buffer + total),
                                      (uint16_t) (buffer_size - total - 1));
                if (ret > 0) {
                    total += ret;
                }
            }
            break;
        }

        timeout++;
    }

    response_buffer[total] = '\0';

    if (total > 0) {
        g_http_info.responses_received++;
        g_http_info.last_status_code = http_parse_status_code(response_buffer);
        strcpy(g_http_info.status, "http: response received");
    } else {
        strcpy(g_http_info.status, "http: no response");
    }

    socket_close(sock);

    return total;
}

/* ============================================================
 *  HTTPS 客户端
 * ============================================================ */

int32_t https_get(const char *host, const char *path, char *response_buffer, uint32_t buffer_size)
{
    char request[1024];
    tls_ctx_t *tls;
    int32_t total = 0;
    int32_t ret;
    uint32_t timeout;

    if (host == NULL || host[0] == '\0' || response_buffer == NULL || buffer_size == 0) {
        strcpy(g_http_info.status, "https: bad parameters");
        return -1;
    }

    http_copy(g_http_info.last_host, sizeof(g_http_info.last_host), host);
    http_copy(g_http_info.last_path, sizeof(g_http_info.last_path), path ? path : "/");

    if (!http_build_get_request(host, path, request, sizeof(request))) {
        return -1;
    }

    /* 创建 TLS 上下文 */
    tls = tls_create();
    if (tls == NULL) {
        strcpy(g_http_info.status, "https: tls create failed");
        return -1;
    }

    /* 设置服务器名称（SNI） */
    tls_set_server_name(tls, host);

    /* 连接并握手 */
    if (!tls_connect(tls, host, 443)) {
        strcpy(g_http_info.status, "https: connect failed");
        tls_free(tls);
        return -1;
    }

    /* 等待握手完成 */
    timeout = 0;
    while (!tls_is_connected(tls) && !tls_has_error(tls) && timeout < 1000000) {
        tls_poll(tls);
        timeout++;
    }

    if (!tls_is_connected(tls)) {
        strcpy(g_http_info.status, "https: handshake failed");
        tls_free(tls);
        return -1;
    }

    g_http_info.requests_sent++;
    g_http_info.https_probes++;

    /* 发送 HTTP 请求 */
    ret = tls_write(tls, (const uint8_t *) request, (uint32_t) strlen(request));
    if (ret < 0) {
        strcpy(g_http_info.status, "https: write failed");
        tls_free(tls);
        return -1;
    }

    /* 接收响应 */
    total = 0;
    timeout = 0;
    while (timeout < 2000000) {
        ret = tls_read(tls, (uint8_t *) (response_buffer + total),
                       buffer_size - total - 1);
        if (ret > 0) {
            total += ret;
            timeout = 0;
        } else if (ret < 0) {
            break;
        }

        if (!tls_is_connected(tls)) {
            break;
        }

        tls_poll(tls);
        timeout++;
    }

    response_buffer[total] = '\0';

    if (total > 0) {
        g_http_info.responses_received++;
        g_http_info.last_status_code = http_parse_status_code(response_buffer);
        strcpy(g_http_info.status, "https: response received");
    } else {
        strcpy(g_http_info.status, "https: no response");
    }

    tls_close(tls);
    tls_free(tls);

    return total;
}

/* ============================================================
 *  统一 URL 接口
 * ============================================================ */

int32_t http_get_url(const char *url, char *response_buffer, uint32_t buffer_size)
{
    http_url_t parsed;

    if (url == NULL || response_buffer == NULL || buffer_size == 0) {
        return -1;
    }

    if (!http_parse_url(url, &parsed)) {
        strcpy(g_http_info.status, "http: invalid url");
        return -1;
    }

    http_copy(g_http_info.last_url, sizeof(g_http_info.last_url), url);

    if (strcmp(parsed.scheme, "https") == 0) {
        return https_get(parsed.host, parsed.path, response_buffer, buffer_size);
    } else {
        return http_get(parsed.host, parsed.path, response_buffer, buffer_size);
    }
}

/* ============================================================
 *  信息查询
 * ============================================================ */

const http_info_t *http_info(void)
{
    return &g_http_info;
}

const char *http_status(void)
{
    return g_http_info.status;
}
