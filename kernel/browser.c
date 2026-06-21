#include "browser.h"
#include "common.h"
#include "http.h"

static browser_info_t g_browser_info;

static void browser_copy_url(const char *url)
{
    uint32_t i = 0;

    while (url != NULL && url[i] != '\0' && i + 1 < sizeof(g_browser_info.last_url)) {
        g_browser_info.last_url[i] = url[i];
        i++;
    }
    g_browser_info.last_url[i] = '\0';
}

void browser_init(void)
{
    memset(&g_browser_info, 0, sizeof(g_browser_info));
    g_browser_info.initialized = true;
    g_browser_info.html_parser_ready = true;
    g_browser_info.http_client_ready = true;
    g_browser_info.https_ready = true;
    strcpy(g_browser_info.status, "browser: basic html/http shell ready");
}

bool browser_open_url(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        strcpy(g_browser_info.status, "browser: missing url");
        return false;
    }
    browser_copy_url(url);
    g_browser_info.pages_requested++;
    if (!http_probe_url(url)) {
        strcpy(g_browser_info.status, http_status());
        return false;
    }
    strcpy(g_browser_info.status, "browser: url accepted");
    return true;
}

const browser_info_t *browser_info(void)
{
    return &g_browser_info;
}

const char *browser_status(void)
{
    return g_browser_info.status;
}
