#ifndef _BROWSER_H_
#define _BROWSER_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool initialized;
    bool html_parser_ready;
    bool http_client_ready;
    bool https_ready;
    bool page_loaded;
    uint32_t pages_requested;
    int32_t last_status_code;
    uint32_t last_body_length;
    char last_url[96];
    char last_title[96];
    char content_type[64];
    char status[64];
    char page_text[4096];
} browser_info_t;

void browser_init(void);
bool browser_open_url(const char *url);
const browser_info_t *browser_info(void);
const char *browser_status(void);
const char *browser_page_text(void);
const char *browser_page_title(void);
const char *browser_page_url(void);

#endif
