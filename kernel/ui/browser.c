#include "browser.h"
#include "common.h"
#include "http.h"

static browser_info_t g_browser_info;
static http_response_t g_browser_response;
static char g_browser_raw[16384];
static char g_browser_decoded[16384];

static void browser_copy_url(const char *url)
{
    uint32_t i = 0;

    while (url != NULL && url[i] != '\0' && i + 1 < sizeof(g_browser_info.last_url)) {
        g_browser_info.last_url[i] = url[i];
        i++;
    }
    g_browser_info.last_url[i] = '\0';
}

static void browser_copy_text(char *dst, uint32_t size, const char *src)
{
    uint32_t i = 0;

    if (dst == NULL || size == 0) {
        return;
    }
    while (src != NULL && src[i] != '\0' && i + 1 < size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static char browser_lower_char(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char) (ch + 32);
    }
    return ch;
}

static bool browser_match_ci(const char *text, const char *needle)
{
    uint32_t i = 0;

    if (text == NULL || needle == NULL) {
        return false;
    }
    while (needle[i] != '\0') {
        if (text[i] == '\0' || browser_lower_char(text[i]) != browser_lower_char(needle[i])) {
            return false;
        }
        i++;
    }
    return true;
}

static const char *browser_find_ci(const char *text, const char *needle)
{
    if (text == NULL || needle == NULL || needle[0] == '\0') {
        return NULL;
    }
    while (*text != '\0') {
        if (browser_match_ci(text, needle)) {
            return text;
        }
        text++;
    }
    return NULL;
}

static bool browser_has_scheme(const char *url)
{
    return browser_match_ci(url, "http://") || browser_match_ci(url, "https://");
}

static void browser_normalize_url(const char *url, char *out, uint32_t out_size)
{
    uint32_t pos = 0;
    uint32_t i = 0;
    const char *prefix = "https://";

    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (url == NULL) {
        return;
    }
    if (!browser_has_scheme(url)) {
        while (prefix[pos] != '\0' && pos + 1 < out_size) {
            out[pos] = prefix[pos];
            pos++;
        }
    }
    while (url[i] != '\0' && pos + 1 < out_size) {
        out[pos++] = url[i++];
    }
    if (pos > 0 && out[pos - 1] == '.') {
        pos--;
    }
    out[pos] = '\0';
}

static void browser_reset_page(void)
{
    g_browser_info.page_loaded = false;
    g_browser_info.last_status_code = 0;
    g_browser_info.last_body_length = 0;
    g_browser_info.last_title[0] = '\0';
    g_browser_info.content_type[0] = '\0';
    g_browser_info.page_text[0] = '\0';
    g_browser_raw[0] = '\0';
}

static void browser_write_text(char *out, uint32_t out_size, const char *text)
{
    uint32_t i = 0;
    uint32_t j = 0;
    bool last_space = false;
    bool in_tag = false;
    bool in_script = false;
    bool in_style = false;

    if (out == NULL || out_size == 0) {
        return;
    }

    while (text != NULL && text[i] != '\0' && j + 1 < out_size) {
        char c = text[i];

        if (!in_tag && c == '<') {
            in_tag = true;
            if (text[i + 1] == '/') {
                if (browser_match_ci(text + i + 2, "script")) {
                    in_script = false;
                }
                if (browser_match_ci(text + i + 2, "style")) {
                    in_style = false;
                }
            } else {
                if (browser_match_ci(text + i + 1, "script")) {
                    in_script = true;
                }
                if (browser_match_ci(text + i + 1, "style")) {
                    in_style = true;
                }
            }
            i++;
            continue;
        }

        if (in_tag) {
            if (c == '>') {
                in_tag = false;
            }
            i++;
            continue;
        }

        if (in_script || in_style) {
            i++;
            continue;
        }

        if (c == '\r' || c == '\n' || c == '\t') {
            if (!last_space && j + 1 < out_size) {
                out[j++] = '\n';
                last_space = true;
            }
            i++;
            continue;
        }

        if (c == '&') {
            if (text[i + 1] == 'l' && text[i + 2] == 't' && text[i + 3] == ';') {
                out[j++] = '<';
                i += 4;
                last_space = false;
                continue;
            }
            if (text[i + 1] == 'g' && text[i + 2] == 't' && text[i + 3] == ';') {
                out[j++] = '>';
                i += 4;
                last_space = false;
                continue;
            }
            if (text[i + 1] == 'a' && text[i + 2] == 'm' && text[i + 3] == 'p' && text[i + 4] == ';') {
                out[j++] = '&';
                i += 5;
                last_space = false;
                continue;
            }
        }

        if (c == ' ') {
            if (!last_space && j + 1 < out_size) {
                out[j++] = ' ';
                last_space = true;
            }
            i++;
            continue;
        }

        out[j++] = c;
        last_space = false;
        i++;
    }

    out[j] = '\0';
}

static void browser_extract_meta(const char *html, const http_response_t *response)
{
    const char *title_start;
    const char *title_end;
    const char *content_type;

    title_start = browser_find_ci(html, "<title>");
    if (title_start != NULL) {
        title_start += 7;
        title_end = browser_find_ci(title_start, "</title>");
        if (title_end != NULL) {
            uint32_t len = (uint32_t) (title_end - title_start);
            if (len >= sizeof(g_browser_info.last_title)) {
                len = sizeof(g_browser_info.last_title) - 1;
            }
            memcpy(g_browser_info.last_title, title_start, len);
            g_browser_info.last_title[len] = '\0';
        }
    }

    content_type = http_get_header(response, "Content-Type");
    if (content_type != NULL) {
        browser_copy_text(g_browser_info.content_type, sizeof(g_browser_info.content_type), content_type);
    }
}

void browser_init(void)
{
    memset(&g_browser_info, 0, sizeof(g_browser_info));
    memset(g_browser_raw, 0, sizeof(g_browser_raw));
    memset(g_browser_decoded, 0, sizeof(g_browser_decoded));
    g_browser_info.initialized = true;
    g_browser_info.html_parser_ready = true;
    g_browser_info.http_client_ready = true;
    g_browser_info.https_ready = true;
    strcpy(g_browser_info.status, "browser: ready");
}

bool browser_open_url(const char *url)
{
    char normalized_url[160];
    int32_t length;
    http_response_t *response = &g_browser_response;

    if (url == NULL || url[0] == '\0') {
        strcpy(g_browser_info.status, "browser: missing url");
        browser_reset_page();
        return false;
    }

    browser_normalize_url(url, normalized_url, sizeof(normalized_url));
    browser_copy_url(normalized_url);
    g_browser_info.pages_requested++;

    browser_reset_page();

    if (!http_probe_url(normalized_url)) {
        browser_copy_text(g_browser_info.status, sizeof(g_browser_info.status), http_status());
        return false;
    }

    length = http_get_url(normalized_url, g_browser_raw, sizeof(g_browser_raw));
    if (length <= 0) {
        browser_copy_text(g_browser_info.status, sizeof(g_browser_info.status), http_status());
        return false;
    }

    g_browser_raw[length] = '\0';
    if (http_parse_response((const uint8_t *) g_browser_raw, (uint32_t) length, response)) {
        const char *content_type;

        g_browser_info.last_status_code = response->status_code;
        g_browser_info.last_body_length = response->body_length;
        content_type = http_get_header(response, "Content-Type");
        if (content_type != NULL) {
            browser_copy_text(g_browser_info.content_type, sizeof(g_browser_info.content_type), content_type);
        }
        if (response->body != NULL && response->body_length > 0) {
            uint32_t body_len = response->body_length;
            if (body_len >= sizeof(g_browser_raw)) {
                body_len = sizeof(g_browser_raw) - 1;
            }
            if (http_is_chunked(response)) {
                body_len = http_decode_chunked_body(response->body, body_len, (uint8_t *) g_browser_decoded, sizeof(g_browser_decoded) - 1);
                g_browser_decoded[body_len] = '\0';
                memcpy(g_browser_raw, g_browser_decoded, body_len);
            } else {
                memmove(g_browser_raw, response->body, body_len);
            }
            g_browser_raw[body_len] = '\0';
            g_browser_info.last_body_length = body_len;
        }
        browser_write_text(g_browser_info.page_text, sizeof(g_browser_info.page_text), g_browser_raw);
        browser_extract_meta(g_browser_raw, response);
        g_browser_info.page_loaded = true;
        strcpy(g_browser_info.status, "browser: page loaded");
        return true;
    }

    browser_copy_text(g_browser_info.status, sizeof(g_browser_info.status), http_status());
    return false;
}

const browser_info_t *browser_info(void)
{
    return &g_browser_info;
}

const char *browser_status(void)
{
    return g_browser_info.status;
}

const char *browser_page_text(void)
{
    return g_browser_info.page_text;
}

const char *browser_page_title(void)
{
    return g_browser_info.last_title;
}

const char *browser_page_url(void)
{
    return g_browser_info.last_url;
}
