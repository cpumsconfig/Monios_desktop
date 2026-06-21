#ifndef _HTTP_H_
#define _HTTP_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool initialized;
    bool http_enabled;
    bool https_enabled;
    bool parser_ready;
    bool client_ready;
    uint32_t requests_built;
    uint32_t https_probes;
    char last_url[96];
    char status[64];
} http_info_t;

void http_init(void);
bool http_build_get_request(const char *host, const char *path, char *out, uint32_t out_size);
bool http_probe_url(const char *url);
const http_info_t *http_info(void);
const char *http_status(void);

#endif
