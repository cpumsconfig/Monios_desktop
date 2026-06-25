#ifndef _BROWSER_H_
#define _BROWSER_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool initialized;
    bool html_parser_ready;
    bool http_client_ready;
    bool https_ready;
    uint32_t pages_requested;
    char last_url[96];
    char status[64];
} browser_info_t;

void browser_init(void);
bool browser_open_url(const char *url);
const browser_info_t *browser_info(void);
const char *browser_status(void);

#endif
