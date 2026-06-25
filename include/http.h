#ifndef _HTTP_H_
#define _HTTP_H_

#include "stdbool.h"
#include "stdint.h"

#define HTTP_MAX_HEADERS      32
#define HTTP_MAX_HEADER_NAME  64
#define HTTP_MAX_HEADER_VALUE 256
#define HTTP_MAX_URL          256
#define HTTP_MAX_HOST         128
#define HTTP_MAX_PATH         256

typedef struct {
    char name[HTTP_MAX_HEADER_NAME];
    char value[HTTP_MAX_HEADER_VALUE];
} http_header_t;

typedef struct {
    int32_t status_code;
    char status_text[64];
    char version[16];
    http_header_t headers[HTTP_MAX_HEADERS];
    uint32_t header_count;
    const uint8_t *body;
    uint32_t body_length;
    uint32_t content_length;
    bool chunked;
    bool keep_alive;
} http_response_t;

typedef struct {
    char scheme[8];
    char host[HTTP_MAX_HOST];
    uint16_t port;
    char path[HTTP_MAX_PATH];
} http_url_t;

typedef struct {
    bool initialized;
    bool http_enabled;
    bool https_enabled;
    bool parser_ready;
    bool client_ready;
    uint32_t requests_built;
    uint32_t requests_sent;
    uint32_t responses_received;
    uint32_t https_probes;
    char last_url[HTTP_MAX_URL];
    char last_host[HTTP_MAX_HOST];
    char last_path[HTTP_MAX_PATH];
    int32_t last_status_code;
    char status[64];
} http_info_t;

void http_init(void);

/* URL 解析 */
bool http_parse_url(const char *url, http_url_t *parsed);

/* 请求构建 */
bool http_build_get_request(const char *host, const char *path, char *out, uint32_t out_size);
bool http_build_post_request(const char *host, const char *path, 
                             const char *content_type, const uint8_t *body, uint32_t body_len,
                             char *out, uint32_t out_size);

/* 响应解析 */
bool http_parse_response(const uint8_t *data, uint32_t length, http_response_t *response);
const char *http_get_header(const http_response_t *response, const char *name);
int32_t http_get_content_length(const http_response_t *response);
bool http_is_chunked(const http_response_t *response);

/* 客户端 */
bool http_probe_url(const char *url);
int32_t http_get(const char *host, const char *path, char *response_buffer, uint32_t buffer_size);
int32_t https_get(const char *host, const char *path, char *response_buffer, uint32_t buffer_size);
int32_t http_get_url(const char *url, char *response_buffer, uint32_t buffer_size);

/* 信息查询 */
const http_info_t *http_info(void);
const char *http_status(void);

#endif
