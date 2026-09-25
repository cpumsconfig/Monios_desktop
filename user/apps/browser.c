/*
 * browser.c — MoniOS Browser with an enhanced built-in HTML/CSS renderer.
 *
 * Rendering model
 *  -------------
 *  The renderer flattens a page into a list of styled "lines" plus a small set
 *  of clickable hit-rects.  Each line carries its own text colour, background
 *  swatch, border, left offset (margin/padding), vertical alignment, bold /
 *  underline decoration and an optional hyperlink target index.  This is a
 *  deliberate simplification of the CSS box model: block-level elements break
 *  lines and own a background / border swatch, inline elements flow together
 *  and inherit the current style.
 *
 * Supported HTML
 *  -------------
 *  Text   : p, div, span, h1-h6, br, hr, center
 *  Lists  : ul, ol, li (depth-aware bullets)
 *  Inline : a, strong/b, em/i, u, font, code
 *  Blocks : blockquote (left bar + tint), pre (whitespace preserved)
 *  Tables : table, tr, td, th (cell-separated rows)
 *  Media  : img (sized placeholder box with alt text; no pixel decoder in the
 *           user runtime, so images render as bordered boxes)
 *  Forms  : input, button, textarea, select (visual widgets only)
 *
 * CSS
 *  ---
 *  - inline style="..." attributes:
 *      color, background(-color), font-size, font-weight, text-align,
 *      margin, padding, border, width, height, display:block|inline|none
 *  - a minimal <style> block parser: tag selectors "tag { prop: value; ... }"
 *
 * Navigation
 *  ---------
 *  - Back / Forward stacks, Refresh, address bar (Enter to edit, Esc cancels)
 *  - mouse left-click on a blue underlined link navigates; relative URLs are
 *    resolved against the current page base.
 */

#include "appsys.h"
#include "monios_dll.h"
#include "osui_dll.h"
#include "windows_dll.h"
#include "stdio.h"
#include "string.h"
#include "stdint.h"
#include "syscall.h"

#define BROWSER_DEFAULT_URL "https://example.com"
#define BROWSER_URL_MAX     160
#define BROWSER_TEXT_MAX    118
#define BROWSER_MAX_LINES   600
#define BROWSER_HISTORY_MAX 8
#define BROWSER_MAX_HREFS   64
#define BROWSER_HREF_LEN    160
#define BROWSER_MAX_RULES   32
#define BROWSER_MAX_HITS    64

/* Rendering geometry inside the content card. */
#define CARD_X      250
#define CARD_TOP    384
#define CARD_BOTTOM 660
#define CARD_WIDTH  696
#define CHARS_PER_LINE 86u

/* 0x00RRGGBB palette. */
#define COL_BODY     0x202428u
#define COL_HEADING  0x0B57D0u
#define COL_SUB      0x323F4Bu
#define COL_LINK     0x0563C1u
#define COL_MUTED    0x6A737Du
#define COL_RULE     0x9AA4AFu
#define COL_IMG      0x8A6D3Bu
#define COL_CODE_BG  0xEFEEF0u
#define COL_BQ_BG    0xF3F4F6u
#define COL_BQ_BAR   0x9AA4AFu
#define COL_WIDGET   0xD0D7DEu
#define COL_INPUT_BG 0xFFFFFFu

typedef enum {
    BR_LN_BODY = 0,
    BR_LN_H1, BR_LN_H2, BR_LN_H3,
    BR_LN_LIST,
    BR_LN_LINK,
    BR_LN_RULE,
    BR_LN_IMG,
    BR_LN_BLOCKQUOTE,
    BR_LN_CODE,
    BR_LN_PRE,
    BR_LN_INPUT,
    BR_LN_BUTTON
} br_line_kind_t;

typedef struct {
    char text[BROWSER_TEXT_MAX];
    uint8_t kind;
    uint16_t xoff;        /* left offset in px (margin / indent) */
    uint32_t color;
    uint32_t bg;          /* background swatch, 0 = none */
    uint32_t border;      /* border colour, 0 = none */
    uint16_t height;      /* line height in px */
    uint8_t align;        /* 0 left, 1 center, 2 right */
    uint8_t bold;
    uint8_t underline;
    int16_t href;         /* index into g_urls, -1 = none */
    uint16_t gap_before;  /* extra px gap above the line */
} br_line_t;

/* a clickable rect recorded at render time */
typedef struct {
    uint16_t x, y, w, h;
    int16_t href;
} br_hit_t;

/* a very small stylesheet rule from a <style> block */
typedef struct {
    char sel[16];
    uint8_t has_color, has_bg, has_align, has_bold, has_height;
    uint32_t color, bg;
    uint8_t align, bold;
    uint16_t height;
} br_rule_t;

typedef struct {
    br_line_t lines[BROWSER_MAX_LINES];
    uint32_t line_count;
    char title[96];
    /* current accumulation buffer */
    char cur[BROWSER_TEXT_MAX * 2];
    uint32_t cur_len;
    uint8_t cur_kind;
    uint32_t cur_color;
    uint32_t cur_bg;
    uint8_t cur_align;
    uint8_t cur_bold;
    uint8_t cur_underline;
    uint16_t cur_xoff;
    uint16_t cur_height;
    int16_t cur_href;
    /* parser state */
    bool in_title, in_script, in_style, in_pre;
    uint8_t list_depth;
    bool in_link;
    bool in_center;
    uint8_t bq_depth;
} br_doc_t;

static char g_http_response[MONIOS_HTTP_RESPONSE_MAX];
static br_doc_t g_doc;
static uint32_t g_scroll;

/* navigation history */
static char g_back_stack[BROWSER_HISTORY_MAX][BROWSER_URL_MAX];
static char g_fwd_stack[BROWSER_HISTORY_MAX][BROWSER_URL_MAX];
static uint32_t g_back_n;
static uint32_t g_fwd_n;

/* hyperlink table + click hit list */
static char g_urls[BROWSER_MAX_HREFS][BROWSER_HREF_LEN];
static uint32_t g_url_n;
static br_hit_t g_hits[BROWSER_MAX_HITS];
static uint32_t g_hit_n;
static char g_current_url[BROWSER_URL_MAX];

/* collected <style> text, then parsed rules */
static char g_style_buf[1024];
static uint32_t g_style_len;
static br_rule_t g_rules[BROWSER_MAX_RULES];
static uint32_t g_rule_n;

static const char *g_browser_nav_items[] = {
    "Home", "Reading", "Downloads", "Settings"
};
static const char *g_browser_actions[] = {
    "Back", "Forward", "Refresh"
};

/* ------------------------------------------------------------------ */
/* small helpers (freestanding: no strstr/atoi in this libc)          */
/* ------------------------------------------------------------------ */
static char br_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char) (c + 32);
    }
    return c;
}

static bool br_eqn(const char *a, const char *b)
{
    uint32_t i = 0;
    if (a == 0 || b == 0) return false;
    while (b[i] != '\0') {
        if (br_lower(a[i]) != b[i]) return false;
        i++;
    }
    return a[i] == '\0';
}

static bool br_has_prefix(const char *s, const char *p)
{
    uint32_t i = 0;
    if (s == 0 || p == 0) return false;
    while (p[i] != '\0') {
        if (s[i] == '\0' || s[i] != p[i]) return false;
        i++;
    }
    return true;
}

/* bounded append (no strlcat in this libc) */
static void br_cat(char *dst, const char *src, uint32_t dst_size)
{
    uint32_t n = (uint32_t) strlen(dst);
    uint32_t i = 0;
    while (src[i] != '\0' && n + i + 1 < dst_size) {
        dst[n + i] = src[i];
        i++;
    }
    dst[n + i] = '\0';
}

static int br_parse_uint(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    if (s == 0 || *s < '0' || *s > '9') return -1;
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (uint32_t) (*s - '0');
        s++;
    }
    *out = v;
    return 0;
}

/* skip whitespace */
static const char *br_skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    return s;
}

/* ------------------------------------------------------------------ */
/* colour parsing                                                      */
/* ------------------------------------------------------------------ */
static uint32_t br_named_color(const char *name)
{
    if (br_eqn(name, "red"))     return 0xC0392Bu;
    if (br_eqn(name, "blue"))    return 0x0B57D0u;
    if (br_eqn(name, "green"))   return 0x1E7E34u;
    if (br_eqn(name, "black"))   return 0x1A1A1Au;
    if (br_eqn(name, "white"))   return 0xFFFFFFu;
    if (br_eqn(name, "gray") || br_eqn(name, "grey")) return 0x6A737Du;
    if (br_eqn(name, "yellow"))  return 0xB8860Bu;
    if (br_eqn(name, "orange"))  return 0xD97706u;
    if (br_eqn(name, "purple"))  return 0x6F42C1u;
    if (br_eqn(name, "cyan"))    return 0x0C8D94u;
    if (br_eqn(name, "silver"))  return 0xC0C0C0u;
    if (br_eqn(name, "lime"))    return 0x32CD32u;
    if (br_eqn(name, "navy"))    return 0x000080u;
    return 0xFFFFFFFFu;
}

/* parse "red" / "#RGB" / "#RRGGBB" into a 0x00RRGGBB color; default on fail */
static uint32_t br_parse_color(const char *s, uint32_t dflt)
{
    char tmp[24];
    uint32_t i = 0;

    if (s == 0) return dflt;
    s = br_skip_ws(s);
    if (s[0] == '#') {
        uint32_t hex[8];
        uint32_t n = 0;
        s++;
        while (*s != '\0' && *s != ';' && *s != '"' && *s != ' ' &&
               *s != ')' && n < 8) {
            char c = br_lower(*s);
            uint8_t d;
            if (c >= '0' && c <= '9') d = (uint8_t) (c - '0');
            else if (c >= 'a' && c <= 'f') d = (uint8_t) (c - 'a' + 10);
            else break;
            hex[n++] = d;
            s++;
        }
        if (n == 3) {
            uint32_t r = (hex[0] << 4) | hex[0];
            uint32_t g = (hex[1] << 4) | hex[1];
            uint32_t b = (hex[2] << 4) | hex[2];
            return (r << 16) | (g << 8) | b;
        }
        if (n >= 6) {
            uint32_t r = (hex[0] << 4) | hex[1];
            uint32_t g = (hex[2] << 4) | hex[3];
            uint32_t b = (hex[4] << 4) | hex[5];
            return (r << 16) | (g << 8) | b;
        }
        return dflt;
    }
    /* rgb(r,g,b) */
    if ((s[0] == 'r' || s[0] == 'R') && (s[1] == 'g' || s[1] == 'G') &&
        (s[2] == 'b' || s[2] == 'B')) {
        uint32_t r = 0, g = 0, b = 0;
        uint32_t vals[3];
        int k = 0;
        const char *p = s + 3;
        while (*p != '\0' && *p != ')' && k < 3) {
            uint32_t v = 0;
            p = br_skip_ws(p);
            while (*p >= '0' && *p <= '9') { v = v * 10u + (uint32_t) (*p - '0'); p++; }
            vals[k++] = v;
            p = br_skip_ws(p);
            if (*p == ',') p++;
        }
        if (k == 3) { r = vals[0]; g = vals[1]; b = vals[2]; }
        return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
    }
    /* named color: copy until delimiter */
    i = 0;
    while (*s != '\0' && *s != ';' && *s != '"' && *s != ' ' &&
           *s != '\'' && *s != ')' && i + 1 < sizeof(tmp)) {
        tmp[i++] = *s++;
    }
    tmp[i] = '\0';
    {
        uint32_t c = br_named_color(tmp);
        return c == 0xFFFFFFFFu ? dflt : c;
    }
}

/* parse a length value like "12px" / "1.2em" into px (int) */
static int br_parse_len(const char *s, int dflt)
{
    uint32_t v = 0;
    s = br_skip_ws(s);
    if (*s < '0' || *s > '9') return dflt;
    while (*s >= '0' && *s <= '9') { v = v * 10u + (uint32_t) (*s - '0'); s++; }
    return (int) v;
}

/* ------------------------------------------------------------------ */
/* line emission                                                       */
/* ------------------------------------------------------------------ */
static void br_reset_current(br_doc_t *d)
{
    d->cur_len = 0;
    d->cur[0] = '\0';
}

static void br_emit_line(br_doc_t *d, const char *text, uint8_t kind,
                         uint32_t color, uint32_t bg, uint16_t height,
                         uint16_t xoff, uint8_t bold, uint8_t underline,
                         int16_t href, uint8_t align)
{
    br_line_t *ln;
    uint32_t i;

    if (d->line_count >= BROWSER_MAX_LINES) return;
    ln = &d->lines[d->line_count];
    for (i = 0; text[i] != '\0' && i + 1 < sizeof(ln->text); i++) {
        ln->text[i] = text[i];
    }
    ln->text[i] = '\0';
    ln->kind = kind;
    ln->color = color;
    ln->bg = bg;
    ln->border = 0;
    ln->height = height;
    ln->xoff = xoff;
    ln->bold = bold;
    ln->underline = underline;
    ln->href = href;
    ln->align = align;
    ln->gap_before = 0;
    d->line_count++;
}

static uint16_t br_kind_height(uint8_t kind)
{
    switch (kind) {
    case BR_LN_H1: return 30;
    case BR_LN_H2: return 26;
    case BR_LN_H3: return 22;
    case BR_LN_BLOCKQUOTE: return 18;
    default:       return 17;
    }
}

/* wrap the accumulated current buffer into display lines and flush it */
static void br_flush_current(br_doc_t *d)
{
    uint32_t len = d->cur_len;
    uint32_t i = 0;
    uint16_t xoff = d->cur_xoff;

    while (len > 0) {
        uint32_t take = len;
        uint32_t space;
        uint32_t maxchars = (uint32_t) CHARS_PER_LINE;

        if (take > maxchars) {
            take = maxchars;
            /* try to break on a space */
            space = take;
            while (space > 0 && d->cur[space] != ' ') space--;
            if (space > 0) take = space + 1;
        }
        {
            char chunk[BROWSER_TEXT_MAX];
            uint32_t j = 0;
            while (j < take && i + j < len) {
                chunk[j] = d->cur[i + j];
                j++;
            }
            /* trim trailing space */
            while (j > 0 && chunk[j - 1] == ' ') j--;
            chunk[j] = '\0';
            if (j > 0) {
                br_emit_line(d, chunk, d->cur_kind, d->cur_color, d->cur_bg,
                             d->cur_height != 0 ? d->cur_height : br_kind_height(d->cur_kind),
                             xoff, d->cur_bold, d->cur_underline, d->cur_href,
                             d->cur_align);
                xoff = 0; /* continuation lines have no extra indent */
            }
        }
        i += take;
        len -= take;
    }
    br_reset_current(d);
}

static void br_append_char(br_doc_t *d, char c)
{
    if (d->cur_len + 2 >= sizeof(d->cur)) {
        br_flush_current(d);
    }
    d->cur[d->cur_len++] = c;
    d->cur[d->cur_len] = '\0';
}

static void br_append_space(br_doc_t *d)
{
    if (d->cur_len > 0 && d->cur[d->cur_len - 1] != ' ') {
        br_append_char(d, ' ');
    }
}

static void br_append_title(br_doc_t *d, char c)
{
    uint32_t n = (uint32_t) strlen(d->title);
    if (n + 1 < sizeof(d->title)) {
        d->title[n] = c;
        d->title[n + 1] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* entity decoding                                                     */
/* ------------------------------------------------------------------ */
static uint32_t br_decode_entity(const char *t, char *out)
{
    if (t == 0 || out == 0 || t[0] != '&') return 0;
    if (strncmp(t, "&lt;", 4) == 0)    { *out = '<'; return 4; }
    if (strncmp(t, "&gt;", 4) == 0)    { *out = '>'; return 4; }
    if (strncmp(t, "&amp;", 5) == 0)   { *out = '&'; return 5; }
    if (strncmp(t, "&quot;", 6) == 0)  { *out = '"'; return 6; }
    if (strncmp(t, "&#39;", 5) == 0)   { *out = '\''; return 5; }
    if (strncmp(t, "&nbsp;", 6) == 0)  { *out = ' '; return 6; }
    if (strncmp(t, "&copy;", 6) == 0)  { *out = '*'; return 6; }
    if (strncmp(t, "&ndash;", 7) == 0) { *out = '-'; return 7; }
    if (strncmp(t, "&mdash;", 7) == 0) { *out = '-'; return 7; }
    return 0;
}

/* ------------------------------------------------------------------ */
/* attribute extraction within a tag body                              */
/* ------------------------------------------------------------------ */
static bool br_attr(const char *start, const char *end, const char *name,
                    char *out, uint32_t out_size)
{
    uint32_t n = (uint32_t) strlen(name);
    const char *p = start;
    out[0] = '\0';
    while (p + n <= end) {
        if (br_lower(p[0]) == name[0]) {
            uint32_t k = 1;
            while (k < n && p + k < end && br_lower(p[k]) == name[k]) k++;
            if (k == n) {
                const char *q = p + n;
                /* must be boundary (space, /, >, =) */
                if (q < end && !(*q == ' ' || *q == '\t' || *q == '=' ||
                                 *q == '/' || *q == '>')) {
                    p++;
                    continue;
                }
                while (q < end && (*q == ' ' || *q == '\t')) q++;
                if (q < end && *q == '=') {
                    q++;
                    while (q < end && (*q == ' ' || *q == '\t')) q++;
                    if (q < end && (*q == '"' || *q == '\'')) {
                        char quote = *q++;
                        uint32_t i = 0;
                        while (q < end && *q != quote && i + 1 < out_size) {
                            out[i++] = *q++;
                        }
                        out[i] = '\0';
                        return true;
                    }
                }
            }
        }
        p++;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* inline style="..." application                                      */
/* ------------------------------------------------------------------ */
static void br_apply_style(br_doc_t *d, const char *style)
{
    const char *p = style;

    if (style == 0 || style[0] == '\0') return;
    while (*p != '\0') {
        char prop[24];
        uint32_t pi = 0;
        const char *val;

        /* read property name */
        while (*p != '\0' && *p != ':' && *p != ';' && pi + 1 < sizeof(prop)) {
            prop[pi++] = br_lower(*p++);
        }
        prop[pi] = '\0';
        while (*p != '\0' && *p != ':' && *p != ';') p++;
        if (*p == ':') p++;
        val = p;
        /* advance to end of this declaration */
        while (*p != '\0' && *p != ';') p++;

        if (br_eqn(prop, "color")) {
            d->cur_color = br_parse_color(val, d->cur_color);
        } else if (br_eqn(prop, "background-color") || br_eqn(prop, "background")) {
            d->cur_bg = br_parse_color(val, d->cur_bg);
        } else if (br_eqn(prop, "font-size")) {
            int px = br_parse_len(val, 0);
            if (px >= 28)      d->cur_height = 34;
            else if (px >= 20) d->cur_height = 28;
            else if (px >= 16) d->cur_height = 22;
            else if (px >= 12) d->cur_height = 18;
            else if (px > 0)   d->cur_height = 15;
        } else if (br_eqn(prop, "font-weight")) {
            const char *q = br_skip_ws(val);
            if (br_has_prefix(q, "bold") || br_has_prefix(q, "bolder") ||
                br_has_prefix(q, "700") || br_has_prefix(q, "800") ||
                br_has_prefix(q, "900")) {
                d->cur_bold = 1;
            } else if (br_has_prefix(q, "normal")) {
                d->cur_bold = 0;
            }
        } else if (br_eqn(prop, "text-align") || br_eqn(prop, "align")) {
            const char *q = br_skip_ws(val);
            if (br_has_prefix(q, "center")) d->cur_align = 1;
            else if (br_has_prefix(q, "right")) d->cur_align = 2;
            else if (br_has_prefix(q, "left")) d->cur_align = 0;
        } else if (br_eqn(prop, "margin") || br_eqn(prop, "padding")) {
            int px = br_parse_len(val, 0);
            d->cur_xoff = (uint16_t) (d->cur_xoff + px);
        } else if (br_eqn(prop, "margin-left") || br_eqn(prop, "padding-left")) {
            d->cur_xoff = (uint16_t) (d->cur_xoff + br_parse_len(val, 0));
        } else if (br_eqn(prop, "border")) {
            /* approximate: any border => tinted outline colour */
            d->cur_bg = br_parse_color(val, 0xFFFFFFu);
        } else if (br_eqn(prop, "display")) {
            const char *q = br_skip_ws(val);
            if (br_has_prefix(q, "none")) {
                br_flush_current(d);
            }
            /* block / inline are approximated by the tag default */
        }
        if (*p == ';') p++;
    }
}

/* apply a parsed stylesheet rule matching a tag */
static void br_apply_rule(br_doc_t *d, const char *tag)
{
    uint32_t i;
    for (i = 0; i < g_rule_n; i++) {
        br_rule_t *r = &g_rules[i];
        if (r->sel[0] != '\0' && br_eqn(r->sel, tag)) {
            if (r->has_color)  d->cur_color = r->color;
            if (r->has_bg)     d->cur_bg = r->bg;
            if (r->has_bold)   d->cur_bold = r->bold;
            if (r->has_align)  d->cur_align = r->align;
            if (r->has_height) d->cur_height = r->height;
        }
    }
}

/* ------------------------------------------------------------------ */
/* minimal <style> block parser:  sel { decl; decl; }                   */
/* ------------------------------------------------------------------ */
static uint32_t br_css_decl_len(const char *p)
{
    uint32_t n = 0;
    while (p[n] != '\0' && p[n] != '}') n++;
    return n;
}

static void br_parse_style_block(void)
{
    const char *p = g_style_buf;

    g_rule_n = 0;
    while (*p != '\0' && g_rule_n < BROWSER_MAX_RULES) {
        br_rule_t *r;
        char sel[16];
        uint32_t si = 0;
        const char *ob;

        /* skip whitespace and comments-ish junk */
        while (*p != '\0' && (*p == ' ' || *p == '\t' || *p == '\r' ||
               *p == '\n' || *p == '}')) p++;
        if (*p == '\0') break;

        /* read selector (simple tag name, ignore .class/#id/,#) */
        while (*p != '\0' && *p != '{' && *p != ' ' && *p != ',' &&
               si + 1 < sizeof(sel)) {
            sel[si++] = br_lower(*p++);
        }
        sel[si] = '\0';
        while (*p != '\0' && *p != '{') p++;
        if (*p != '{') break;
        p++;
        ob = p;
        {
            uint32_t blen = br_css_decl_len(ob);
            /* parse declarations in [ob, ob+blen) */
            const char *q = ob;
            char decl[128];
            uint32_t di = 0;

            r = &g_rules[g_rule_n];
            memset(r, 0, sizeof(*r));
            strlcpy(r->sel, sel, sizeof(r->sel));

            while (q < ob + blen) {
                char prop[24];
                uint32_t pi = 0;
                const char *val;
                di = 0;
                while (q < ob + blen && *q != ';' && di + 1 < sizeof(decl)) {
                    decl[di++] = *q++;
                }
                decl[di] = '\0';
                if (q < ob + blen && *q == ';') q++;
                /* split prop:val */
                {
                    const char *c = decl;
                    while (*c != '\0' && *c != ':' && pi + 1 < sizeof(prop)) {
                        prop[pi++] = br_lower(*c++);
                    }
                    prop[pi] = '\0';
                    if (*c == ':') c++;
                    val = c;
                }
                if (br_eqn(prop, "color")) {
                    r->color = br_parse_color(val, r->color);
                    r->has_color = 1;
                } else if (br_eqn(prop, "background-color") ||
                           br_eqn(prop, "background")) {
                    r->bg = br_parse_color(val, r->bg);
                    r->has_bg = 1;
                } else if (br_eqn(prop, "font-weight") &&
                           (br_has_prefix(val, "bold") || br_has_prefix(val, "7"))) {
                    r->bold = 1; r->has_bold = 1;
                } else if (br_eqn(prop, "text-align")) {
                    if (br_has_prefix(br_skip_ws(val), "center")) r->align = 1;
                    else if (br_has_prefix(br_skip_ws(val), "right")) r->align = 2;
                    else r->align = 0;
                    r->has_align = 1;
                } else if (br_eqn(prop, "font-size")) {
                    int px = br_parse_len(val, 0);
                    if (px >= 28)      r->height = 34;
                    else if (px >= 20) r->height = 28;
                    else if (px >= 16) r->height = 22;
                    else if (px > 0)   r->height = 18;
                    r->has_height = 1;
                }
            }
            g_rule_n++;
        }
        p = ob + br_css_decl_len(ob);
        if (*p == '}') p++;
    }
}

/* ------------------------------------------------------------------ */
/* tag handling                                                        */
/* ------------------------------------------------------------------ */
static bool br_is_block(const char *tag)
{
    return br_eqn(tag, "p") || br_eqn(tag, "div") || br_eqn(tag, "body") ||
           br_eqn(tag, "ul") || br_eqn(tag, "ol") || br_eqn(tag, "li") ||
           br_eqn(tag, "tr") || br_eqn(tag, "section") || br_eqn(tag, "article") ||
           br_eqn(tag, "header") || br_eqn(tag, "footer") || br_eqn(tag, "table") ||
           br_eqn(tag, "form") || br_eqn(tag, "fieldset") ||
           br_eqn(tag, "h1") || br_eqn(tag, "h2") || br_eqn(tag, "h3") ||
           br_eqn(tag, "h4") || br_eqn(tag, "h5") || br_eqn(tag, "h6") ||
           br_eqn(tag, "center") || br_eqn(tag, "pre") ||
           br_eqn(tag, "blockquote");
}

static void br_set_default_style(br_doc_t *d)
{
    d->cur_kind = BR_LN_BODY;
    d->cur_color = COL_BODY;
    d->cur_bg = 0;
    d->cur_bold = 0;
    d->cur_underline = 0;
    d->cur_align = 0;
    d->cur_height = 0;
}

static int16_t br_add_url(const char *url)
{
    if (g_url_n >= BROWSER_MAX_HREFS) return -1;
    strlcpy(g_urls[g_url_n], url, BROWSER_HREF_LEN);
    return (int16_t) g_url_n++;
}

/* emit a self-contained widget line (input / button / box) */
static void br_emit_widget(br_doc_t *d, uint8_t kind, const char *text,
                           uint16_t height)
{
    br_flush_current(d);
    if (d->line_count >= BROWSER_MAX_LINES) return;
    {
        br_line_t *ln = &d->lines[d->line_count];
        uint32_t i = 0;
        while (text != 0 && text[i] != '\0' && i + 1 < sizeof(ln->text)) {
            ln->text[i] = text[i]; i++;
        }
        ln->text[i] = '\0';
        ln->kind = kind;
        ln->color = COL_SUB;
        ln->bg = COL_INPUT_BG;
        ln->border = COL_WIDGET;
        ln->height = height;
        ln->xoff = d->cur_xoff;
        ln->bold = 0; ln->underline = 0; ln->href = -1; ln->align = 0;
        ln->gap_before = 4;
        d->line_count++;
    }
}

static void br_open_tag(br_doc_t *d, const char *start, const char *end)
{
    char name[24];
    uint32_t i = 0;
    const char *p = start;
    char val[128];

    while (p < end && (*p == ' ' || *p == '\t')) p++;
    while (p < end && i + 1 < sizeof(name) && *p != ' ' && *p != '/' && *p != '>') {
        name[i++] = br_lower(*p++);
    }
    name[i] = '\0';

    /* element-level stylesheet rule applies to opening tags */
    if (!br_eqn(name, "script") && !br_eqn(name, "style") && !br_eqn(name, "title")) {
        br_apply_rule(d, name);
    }

    if (br_eqn(name, "script")) { d->in_script = true; return; }
    if (br_eqn(name, "style"))  { d->in_style = true; g_style_len = 0; g_style_buf[0] = '\0'; return; }
    if (br_eqn(name, "title"))  { d->in_title = true; return; }

    if (br_eqn(name, "br")) {
        br_flush_current(d);
        return;
    }
    if (br_eqn(name, "hr")) {
        br_flush_current(d);
        br_emit_line(d, "", BR_LN_RULE, COL_RULE, 0, 8, d->cur_xoff, 0, 0, -1, 0);
        return;
    }
    if (br_eqn(name, "img")) {
        uint16_t h = 28;
        br_flush_current(d);
        if (br_attr(start, end, "height", val, sizeof(val))) {
            int px = br_parse_len(val, 0);
            if (px >= 8 && px <= 200) h = (uint16_t) px;
        }
        if (br_attr(start, end, "alt", val, sizeof(val)) && val[0]) {
            char box[BROWSER_TEXT_MAX];
            strlcpy(box, "[img: ", sizeof(box));
            br_cat(box, val, sizeof(box));
            br_cat(box, "]", sizeof(box));
            br_emit_line(d, box, BR_LN_IMG, COL_IMG, 0xE8E2D2, h, d->cur_xoff, 0, 0, -1, 0);
        } else {
            br_emit_line(d, "[image]", BR_LN_IMG, COL_IMG, 0xE8E2D2, h, d->cur_xoff, 0, 0, -1, 0);
        }
        return;
    }
    if (br_eqn(name, "h1")) { br_flush_current(d); d->cur_kind = BR_LN_H1; d->cur_color = COL_HEADING; d->cur_height = 30; return; }
    if (br_eqn(name, "h2")) { br_flush_current(d); d->cur_kind = BR_LN_H2; d->cur_color = COL_HEADING; d->cur_height = 26; return; }
    if (br_eqn(name, "h3") || br_eqn(name, "h4") || br_eqn(name, "h5") || br_eqn(name, "h6")) {
        br_flush_current(d); d->cur_kind = BR_LN_H3; d->cur_color = COL_SUB; d->cur_height = 22; return;
    }
    if (br_eqn(name, "ul")) { d->list_depth++; br_flush_current(d); return; }
    if (br_eqn(name, "ol")) { d->list_depth++; br_flush_current(d); return; }
    if (br_eqn(name, "li")) {
        br_flush_current(d);
        d->cur_kind = BR_LN_LIST;
        d->cur_color = COL_BODY;
        d->cur_xoff = (uint16_t) (6u * d->list_depth);
        br_append_char(d, '*');
        br_append_char(d, ' ');
        return;
    }
    if (br_eqn(name, "a")) {
        br_flush_current(d);
        d->in_link = true;
        d->cur_kind = BR_LN_LINK;
        d->cur_color = COL_LINK;
        d->cur_underline = 1;
        if (br_attr(start, end, "href", val, sizeof(val))) {
            d->cur_href = br_add_url(val);
        }
        return;
    }
    if (br_eqn(name, "strong") || br_eqn(name, "b")) {
        d->cur_bold = 1;
        d->cur_color = 0x101418u;
        return;
    }
    if (br_eqn(name, "em") || br_eqn(name, "i")) {
        d->cur_color = COL_SUB;
        return;
    }
    if (br_eqn(name, "u")) {
        d->cur_underline = 1;
        return;
    }
    if (br_eqn(name, "blockquote")) {
        br_flush_current(d);
        d->bq_depth++;
        d->cur_kind = BR_LN_BLOCKQUOTE;
        d->cur_bg = COL_BQ_BG;
        d->cur_xoff = (uint16_t) (d->cur_xoff + 18u);
        return;
    }
    if (br_eqn(name, "pre")) {
        br_flush_current(d);
        d->in_pre = true;
        d->cur_kind = BR_LN_PRE;
        d->cur_bg = COL_CODE_BG;
        return;
    }
    if (br_eqn(name, "code")) {
        d->cur_bg = COL_CODE_BG;
        return;
    }
    if (br_eqn(name, "center")) { d->cur_align = 1; return; }
    if (br_eqn(name, "font")) {
        if (br_attr(start, end, "color", val, sizeof(val))) {
            d->cur_color = br_parse_color(val, d->cur_color);
        }
        return;
    }
    if (br_eqn(name, "input")) {
        char ph[80];
        ph[0] = '\0';
        br_attr(start, end, "placeholder", ph, sizeof(ph));
        if (ph[0] == '\0') strlcpy(ph, "", sizeof(ph));
        br_emit_widget(d, BR_LN_INPUT, ph, 20);
        return;
    }
    if (br_eqn(name, "button")) {
        /* text inside arrives later; mark so </button> can close the widget */
        br_flush_current(d);
        d->cur_kind = BR_LN_BUTTON;
        d->cur_color = 0xFFFFFFu;
        d->cur_bg = COL_LINK;
        return;
    }
    if (br_eqn(name, "textarea") || br_eqn(name, "select")) {
        br_emit_widget(d, BR_LN_INPUT, "", 40);
        return;
    }
    if (br_eqn(name, "span") || br_eqn(name, "div") || br_eqn(name, "p")) {
        if (br_attr(start, end, "style", val, sizeof(val))) {
            br_apply_style(d, val);
        }
    }
    if (br_eqn(name, "td") || br_eqn(name, "th")) {
        br_append_space(d);
        return;
    }
    if (br_is_block(name)) {
        br_flush_current(d);
        br_set_default_style(d);
    }
}

static void br_close_tag(br_doc_t *d, const char *tag)
{
    if (br_eqn(tag, "script")) { d->in_script = false; return; }
    if (br_eqn(tag, "style"))  {
        d->in_style = false;
        br_parse_style_block();
        return;
    }
    if (br_eqn(tag, "title"))  { d->in_title = false; return; }
    if (br_eqn(tag, "h1") || br_eqn(tag, "h2") || br_eqn(tag, "h3") ||
        br_eqn(tag, "h4") || br_eqn(tag, "h5") || br_eqn(tag, "h6")) {
        br_flush_current(d);
        br_set_default_style(d);
        return;
    }
    if (br_eqn(tag, "ul") || br_eqn(tag, "ol")) {
        if (d->list_depth > 0) d->list_depth--;
        br_flush_current(d);
        return;
    }
    if (br_eqn(tag, "li")) { br_flush_current(d); br_set_default_style(d); return; }
    if (br_eqn(tag, "a")) {
        br_flush_current(d);
        d->in_link = false;
        d->cur_href = -1;
        br_set_default_style(d);
        return;
    }
    if (br_eqn(tag, "center")) { d->cur_align = 0; return; }
    if (br_eqn(tag, "blockquote")) {
        br_flush_current(d);
        if (d->bq_depth > 0) d->bq_depth--;
        br_set_default_style(d);
        return;
    }
    if (br_eqn(tag, "pre")) {
        br_flush_current(d);
        d->in_pre = false;
        br_set_default_style(d);
        return;
    }
    if (br_eqn(tag, "code")) {
        d->cur_bg = 0;
        return;
    }
    if (br_eqn(tag, "button")) {
        br_flush_current(d);
        br_set_default_style(d);
        return;
    }
    if (br_eqn(tag, "p") || br_eqn(tag, "div") || br_eqn(tag, "tr") ||
        br_eqn(tag, "table") || br_eqn(tag, "form")) {
        br_flush_current(d);
        br_set_default_style(d);
        return;
    }
    if (br_eqn(tag, "strong") || br_eqn(tag, "b")) {
        d->cur_bold = 0;
        d->cur_color = d->in_link ? COL_LINK : COL_BODY;
        return;
    }
    if (br_eqn(tag, "em") || br_eqn(tag, "i")) {
        d->cur_color = d->in_link ? COL_LINK : COL_BODY;
        return;
    }
    if (br_eqn(tag, "u") || br_eqn(tag, "font") || br_eqn(tag, "span")) {
        d->cur_color = d->in_link ? COL_LINK : COL_BODY;
        d->cur_bg = 0;
        d->cur_underline = d->in_link ? 1 : 0;
    }
}

static void br_append_text(br_doc_t *d, const char *text, uint32_t length)
{
    uint32_t i = 0;
    while (i < length) {
        char c = text[i];
        char decoded;
        uint32_t elen = br_decode_entity(text + i, &decoded);

        if (d->in_pre) {
            /* preserve whitespace / newlines inside <pre> */
            if (c == '\r') { i++; continue; }
            if (c == '\n') { br_flush_current(d); i++; continue; }
            if (c == '\t') { br_append_char(d, ' '); br_append_char(d, ' '); i++; continue; }
            if (elen > 0) { c = decoded; i += elen; } else { i++; }
            if (!d->in_script && !d->in_style) br_append_char(d, c);
            continue;
        }

        if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
            br_append_space(d);
            i++;
            continue;
        }
        if (elen > 0) {
            c = decoded;
            i += elen;
        } else {
            i++;
        }
        if (d->in_title) {
            br_append_title(d, c);
        } else if (!d->in_script && !d->in_style) {
            br_append_char(d, c);
        } else if (d->in_style) {
            /* collect raw CSS text */
            if (g_style_len + 1 < sizeof(g_style_buf)) {
                g_style_buf[g_style_len++] = c;
                g_style_buf[g_style_len] = '\0';
            }
        }
    }
}

static void br_parse(br_doc_t *d, const char *html)
{
    const char *cursor = html;

    memset(d, 0, sizeof(*d));
    d->cur_color = COL_BODY;
    d->cur_kind = BR_LN_BODY;
    g_url_n = 0;
    g_style_len = 0;
    g_style_buf[0] = '\0';
    while (*cursor != '\0') {
        const char *te, *te_end;
        if (*cursor != '<') {
            const char *ts = cursor;
            while (*cursor != '\0' && *cursor != '<') cursor++;
            br_append_text(d, ts, (uint32_t) (cursor - ts));
            continue;
        }
        if (strncmp(cursor, "<!--", 4) == 0) {
            const char *ce = cursor + 4;
            while (*ce != '\0' && !(ce[0] == '-' && ce[1] == '-' && ce[2] == '>')) ce++;
            cursor = *ce == '\0' ? ce : ce + 3;
            continue;
        }
        te = cursor + 1;
        te_end = te;
        while (*te_end != '\0' && *te_end != '>') te_end++;
        {
            bool closing = false;
            const char *tp = te;
            if (*tp == '/') { closing = true; tp++; }
            if (closing) {
                char tag[24];
                uint32_t k = 0;
                while (tp < te_end && *tp != ' ' && *tp != '/' && k + 1 < sizeof(tag)) {
                    tag[k++] = br_lower(*tp++);
                }
                tag[k] = '\0';
                br_close_tag(d, tag);
            } else {
                br_open_tag(d, tp, te_end);
            }
        }
        cursor = *te_end == '\0' ? te_end : te_end + 1;
    }
    br_flush_current(d);
    if (d->title[0] == '\0') {
        strlcpy(d->title, "Untitled page", sizeof(d->title));
    }
}

/* ------------------------------------------------------------------ */
/* url helpers                                                         */
/* ------------------------------------------------------------------ */
static const char *br_body_start(const char *r)
{
    const char *c = r;
    if (r == 0) return r;
    while (*c != '\0') {
        if (c[0] == '\r' && c[1] == '\n' && c[2] == '\r' && c[3] == '\n') return c + 4;
        if (c[0] == '\n' && c[1] == '\n') return c + 2;
        c++;
    }
    return r;
}

static int32_t br_status_code(const char *r)
{
    const char *c = r;
    int32_t v = 0;
    if (r == 0 || !br_has_prefix(r, "HTTP/")) return 0;
    while (*c != '\0' && *c != ' ') c++;
    while (*c == ' ') c++;
    while (*c >= '0' && *c <= '9') { v = v * 10 + (*c - '0'); c++; }
    return v;
}

static void br_normalize_url(const char *in, char *out, uint32_t out_size)
{
    uint32_t n;
    if (out == 0 || out_size == 0) return;
    out[0] = '\0';
    if (in == 0 || in[0] == '\0') { strlcpy(out, BROWSER_DEFAULT_URL, out_size); return; }
    if (!br_has_prefix(in, "http://") && !br_has_prefix(in, "https://")) {
        strlcpy(out, "https://", out_size);
        n = (uint32_t) strlen(out);
        strlcpy(out + n, in, out_size - n);
    } else {
        strlcpy(out, in, out_size);
    }
}

/* resolve a possibly-relative href against the current page URL */
static void br_resolve_url(const char *base, const char *href,
                           char *out, uint32_t out_size)
{
    if (href == 0 || href[0] == '\0') {
        strlcpy(out, base, out_size);
        return;
    }
    if (br_has_prefix(href, "http://") || br_has_prefix(href, "https://")) {
        strlcpy(out, href, out_size);
        return;
    }
    if (href[0] == '#') {           /* fragment only: stay on page */
        strlcpy(out, base, out_size);
        return;
    }
    if (href[0] == '/') {
        /* scheme://authority + href */
        char auth[96];
        uint32_t n = 0;
        const char *p = base;
        while (p[n] != '\0' && p[n] != '/') n++;      /* scheme: */
        if (br_has_prefix(base, "https://")) n = 8;
        else if (br_has_prefix(base, "http://")) n = 7;
        /* skip to end of authority */
        {
            uint32_t a = n;
            while (base[a] != '\0' && base[a] != '/') a++;
            if (a >= sizeof(auth)) a = sizeof(auth) - 1;
            strlcpy(auth, base, a + 1);
            auth[a] = '\0';
        }
        strlcpy(out, auth, out_size);
        br_cat(out, href, out_size);
        return;
    }
    /* relative: base up to last '/' + href */
    {
        char dir[BROWSER_URL_MAX];
        uint32_t cut = 0, k = 0;
        const char *slash = strrchr(base, '/');
        if (slash != 0) cut = (uint32_t) (slash - base) + 1;
        while (k + 1 < sizeof(dir) && k < cut) { dir[k] = base[k]; k++; }
        dir[k] = '\0';
        strlcpy(out, dir, out_size);
        br_cat(out, href, out_size);
    }
}

/* ------------------------------------------------------------------ */
/* rendering                                                           */
/* ------------------------------------------------------------------ */
static uint16_t br_text_w(const char *s)
{
    return (uint16_t) ((uint16_t) strlen(s) * 8u);
}

static void br_render_page(const char *url, int32_t status, bool failed, const char *err)
{
    const char *tabs[2] = { g_doc.title, "New tab" };
    uint16_t y = CARD_TOP;
    uint32_t i;
    char status_text[24];

    g_hit_n = 0;

    osui_canvas(0x00EAF0F6);
    osui_panel((osui_rect_t) { 32, 24, 960, 720 });
    osui_titlebar((osui_rect_t) { 32, 24, 960, 36 }, "MoniOS Browser", true);

    osui_navrail((osui_rect_t) { 52, 80, 156, 262 },
                 g_browser_nav_items, 4, 0);
    osui_card((osui_rect_t) { 52, 360, 156, 216 });
    osui_label(66, 380, "Connection", false);
    osui_chip((osui_rect_t) { 66, 408, 128, 24 },
              failed ? "Limited" : "Online",
              failed ? OSUI_STATE_WARNING : OSUI_STATE_SUCCESS);
    osui_callout((osui_rect_t) { 66, 446, 128, 70 },
                 "Network", failed ? "Request failed." : "Driver ready.",
                 failed ? OSUI_STATE_WARNING : OSUI_STATE_SUCCESS);

    osui_label(228, 80, "Web", false);
    osui_label(228, 102, "Native HTML/CSS surface", true);
    osui_commandbar((osui_rect_t) { 228, 132, 740, 38 },
                    "Browser workspace", g_browser_actions, 3,
                    (g_back_n > 0) ? 0 : 1);
    osui_tabbar((osui_rect_t) { 228, 182, 740, 36 }, tabs, 2, 0);
    osui_input((osui_rect_t) { 228, 234, 740, 34 }, url, "", false, false);

    status_text[0] = '\0';
    if (failed) strlcpy(status_text, "Error", sizeof(status_text));
    else if (status > 0) {
        uint32_t v = (uint32_t) status;
        char dg[8]; uint32_t dn = 0;
        while (v > 0 && dn + 1 < sizeof(dg)) { dg[dn++] = (char) ('0' + v % 10u); v /= 10u; }
        strlcpy(status_text, "HTTP ", sizeof(status_text));
        while (dn > 0 && strlen(status_text) + 1 < sizeof(status_text)) {
            char d2[2] = { dg[--dn], '\0' };
            strcat(status_text, d2);
        }
    } else strlcpy(status_text, "HTML", sizeof(status_text));

    osui_statusbar((osui_rect_t) { 228, 278, 740, 28 },
                   failed ? "The network request could not be completed" :
                            "Rendered with built-in HTML/CSS engine",
                   status_text,
                   failed ? OSUI_STATE_DANGER :
                            (status >= 400 ? OSUI_STATE_WARNING : OSUI_STATE_SUCCESS));

    /* content card */
    osui_card((osui_rect_t) { 228, 322, 740, 344 });
    /* white content backdrop so colored text is legible */
    windows_fill_rect(CARD_X, CARD_TOP, CARD_WIDTH, (CARD_BOTTOM - CARD_TOP), 0xFFFFFFu);

    if (failed) {
        osui_callout((osui_rect_t) { 250, 398, 696, 76 },
                     "Unable to load page", err != 0 ? err : "Network request failed.",
                     OSUI_STATE_DANGER);
        osui_present();
        return;
    }

    for (i = g_scroll; i < g_doc.line_count && y < CARD_BOTTOM; i++) {
        const br_line_t *ln = &g_doc.lines[i];
        uint16_t x = (uint16_t) (CARD_X + ln->xoff);
        uint16_t tw = br_text_w(ln->text);
        uint16_t rowh = ln->height;

        if (ln->gap_before) y = (uint16_t) (y + ln->gap_before);

        /* horizontal alignment */
        if (ln->align == 1) {
            int16_t mid = (int16_t) (CARD_X + CARD_WIDTH / 2u);
            x = (uint16_t) (mid - (int16_t) tw / 2);
        } else if (ln->align == 2) {
            x = (uint16_t) (CARD_X + CARD_WIDTH - tw);
        }

        /* block-level background swatch / border */
        if (ln->kind == BR_LN_RULE) {
            windows_fill_rect(CARD_X, (uint16_t) (y + 6), CARD_WIDTH, 1, COL_RULE);
            y = (uint16_t) (y + 10);
            continue;
        }
        if (ln->bg != 0 && ln->kind != BR_LN_IMG) {
            uint16_t bx = (ln->kind == BR_LN_BLOCKQUOTE) ? (uint16_t) (CARD_X + 4) : x;
            uint16_t bw = (ln->kind == BR_LN_BLOCKQUOTE) ?
                          (uint16_t) (CARD_WIDTH - 8u) :
                          (uint16_t) (tw + 8u);
            windows_fill_rect(bx, y, bw, rowh - 2u, ln->bg);
        }
        if (ln->kind == BR_LN_BLOCKQUOTE) {
            windows_fill_rect((uint16_t) (CARD_X + 6), y, 3, rowh - 2u, COL_BQ_BAR);
        }
        if (ln->kind == BR_LN_IMG) {
            windows_fill_rect(x, y, (uint16_t) (tw + 12u), rowh, 0xE8E2D2);
            windows_fill_rect(x, y, (uint16_t) (tw + 12u), 1, COL_IMG);
            windows_fill_rect(x, (uint16_t) (y + rowh - 1u), (uint16_t) (tw + 12u), 1, COL_IMG);
        }
        if (ln->kind == BR_LN_INPUT || ln->kind == BR_LN_BUTTON) {
            uint16_t bw = (uint16_t) (tw + 24u);
            if (bw < 80) bw = 80;
            windows_fill_rect(x, y, bw, rowh, ln->bg);
            windows_fill_rect(x, y, bw, 1, COL_WIDGET);
            windows_fill_rect(x, (uint16_t) (y + rowh - 1u), bw, 1, COL_WIDGET);
            windows_fill_rect(x, y, 1, rowh, COL_WIDGET);
            windows_fill_rect((uint16_t) (x + bw - 1u), y, 1, rowh, COL_WIDGET);
            /* draw centered-ish label */
            windows_draw_text((uint16_t) (x + 8u), (uint16_t) (y + 4u),
                              ln->text, ln->color);
            y = (uint16_t) (y + rowh + 4u);
            continue;
        }

        if (ln->text[0] != '\0') {
            uint16_t ty = (uint16_t) (y + 4u);
            windows_draw_text(x, ty, ln->text, ln->color);
            if (ln->bold) {
                windows_draw_text((uint16_t) (x + 1u), ty, ln->text, ln->color);
            }
            if (ln->underline || ln->kind == BR_LN_LINK) {
                windows_fill_rect(x, (uint16_t) (ty + 11u), tw, 1,
                                  ln->kind == BR_LN_LINK ? COL_LINK : ln->color);
            }
            /* register click target for links */
            if (ln->kind == BR_LN_LINK && ln->href >= 0 &&
                g_hit_n < BROWSER_MAX_HITS) {
                g_hits[g_hit_n].x = x;
                g_hits[g_hit_n].y = y;
                g_hits[g_hit_n].w = (uint16_t) (tw + 2u);
                g_hits[g_hit_n].h = rowh;
                g_hits[g_hit_n].href = ln->href;
                g_hit_n++;
            }
        }
        y = (uint16_t) (y + rowh);
    }
    osui_present();
}

/* ------------------------------------------------------------------ */
/* keyboard event (SYS_KEYBOARD_READ_EVENT = 72)                       */
/* ------------------------------------------------------------------ */
static int br_read_event(uint32_t *type, char *ch, uint8_t *mods)
{
    uint8_t buf[8];
    int64_t r;
    r = (int64_t) syscall3(SYS_KEYBOARD_READ_EVENT, (uint64_t) buf, 0, 0);
    if (r != 1) return 0;
    *type = (uint32_t) buf[0] | ((uint32_t) buf[1] << 8) |
            ((uint32_t) buf[2] << 16) | ((uint32_t) buf[3] << 24);
    *ch = (char) buf[4];
    *mods = buf[5];
    return 1;
}

/* ------------------------------------------------------------------ */
/* loading + navigation                                                */
/* ------------------------------------------------------------------ */
static int br_load(const char *url)
{
    char norm[BROWSER_URL_MAX];
    const char *body;
    int32_t n, st;

    br_normalize_url(url, norm, sizeof(norm));
    strlcpy(g_current_url, norm, sizeof(g_current_url));
    n = app_http_get_url(norm, g_http_response, sizeof(g_http_response));
    g_scroll = 0;
    if (n <= 0) {
        br_render_page(norm, 0, true, "The network driver did not return an HTTP response.");
        return -1;
    }
    g_http_response[n] = '\0';
    st = br_status_code(g_http_response);
    body = br_body_start(g_http_response);
    br_parse(&g_doc, body);
    br_render_page(norm, st, false, 0);
    return 0;
}

static void br_push_history(char stack[][BROWSER_URL_MAX], uint32_t *n, const char *url)
{
    uint32_t i;
    if (*n >= BROWSER_HISTORY_MAX) {
        for (i = 1; i < BROWSER_HISTORY_MAX; i++) {
            strlcpy(stack[i - 1], stack[i], BROWSER_URL_MAX);
        }
        (*n)--;
    }
    strlcpy(stack[*n], url, BROWSER_URL_MAX);
    (*n)++;
}

/* mouse left-click on a link navigates */
static void br_handle_click(int32_t mx, int32_t my, char current[BROWSER_URL_MAX])
{
    uint32_t i;
    for (i = 0; i < g_hit_n; i++) {
        br_hit_t *h = &g_hits[i];
        if (mx >= (int32_t) h->x && mx <= (int32_t) (h->x + h->w) &&
            my >= (int32_t) h->y && my <= (int32_t) (h->y + h->h) &&
            h->href >= 0 && h->href < (int16_t) g_url_n) {
            char resolved[BROWSER_URL_MAX];
            br_resolve_url(g_current_url, g_urls[h->href], resolved, sizeof(resolved));
            if (br_has_prefix(resolved, "javascript:")) return;
            br_push_history(g_back_stack, &g_back_n, current);
            g_fwd_n = 0;
            strlcpy(current, resolved, BROWSER_URL_MAX);
            br_load(current);
            return;
        }
    }
}

int main(int argc, char **argv)
{
    char current[BROWSER_URL_MAX];
    char editbuf[BROWSER_URL_MAX];
    uint32_t edit_len = 0;
    bool editing = false;
    int prev_buttons = 0;

    fputs("MoniOS Browser (enhanced HTML/CSS renderer)\r\n");

    br_normalize_url(argc >= 2 ? argv[1] : BROWSER_DEFAULT_URL,
                      current, sizeof(current));
    g_back_n = g_fwd_n = 0;

    app_enter_graphics_mode();
    if (br_load(current) != 0) {
        /* error page rendered inside br_load; loop still allows scroll/quit */
    }

    /* interactive loop: scroll, history, address editing, mouse clicks */
    for (;;) {
        uint32_t type = 0;
        char ch = 0;
        uint8_t mods = 0;
        uint32_t visible;
        app_mouse_snapshot_t ms;

        app_sleep_ticks(2);

        /* mouse polling: edge-detect left button */
        if (app_get_mouse(&ms) == 0) {
            int down = ms.buttons & 1;
            if (down && !prev_buttons) {
                br_handle_click(ms.x_pixels, ms.y_pixels, current);
            }
            prev_buttons = down;
        }

        if (!br_read_event(&type, &ch, &mods)) {
            continue;
        }

        if (type == 30 /* KEY_EVENT_ESC */) {
            if (editing) {
                editing = false;
                edit_len = 0; editbuf[0] = '\0';
                br_render_page(current, 0, false, 0);
            } else {
                return 0;
            }
            continue;
        }

        if (editing) {
            if (type == 1 /* CHAR */) {
                if (ch == '\r' || ch == '\n') {
                    editing = false;
                    if (edit_len > 0) {
                        br_push_history(g_back_stack, &g_back_n, current);
                        g_fwd_n = 0;
                        br_normalize_url(editbuf, current, sizeof(current));
                        br_load(current);
                    }
                    edit_len = 0; editbuf[0] = '\0';
                } else if (ch == 8 /* backspace */) {
                    if (edit_len > 0) {
                        edit_len--; editbuf[edit_len] = '\0';
                    }
                } else if (ch >= 32 && ch < 127 && edit_len + 1 < sizeof(editbuf)) {
                    editbuf[edit_len++] = ch;
                    editbuf[edit_len] = '\0';
                }
            }
            continue;
        }

        /* non-edit keys */
        visible = (g_doc.line_count > 12) ? (uint32_t) (g_doc.line_count - 12) : 0;
        if (type == 2 /* UP */) {
            if (g_scroll > 0) g_scroll--;
            br_render_page(current, 0, false, 0);
        } else if (type == 3 /* DOWN */) {
            if (g_scroll < visible) g_scroll++;
            br_render_page(current, 0, false, 0);
        } else if (type == 51 /* PAGE_UP */) {
            if (g_scroll > 12) g_scroll -= 12; else g_scroll = 0;
            br_render_page(current, 0, false, 0);
        } else if (type == 52 /* PAGE_DOWN */) {
            g_scroll += 12;
            if (g_scroll > visible) g_scroll = visible;
            br_render_page(current, 0, false, 0);
        } else if (type == 1 && (ch == '\r' || ch == '\n')) {
            editing = true;
            edit_len = 0; editbuf[0] = '\0';
        } else if (type == 1 && (ch == 'b' || ch == 'B')) {
            if (g_back_n > 0) {
                g_back_n--;
                br_push_history(g_fwd_stack, &g_fwd_n, current);
                strlcpy(current, g_back_stack[g_back_n], sizeof(current));
                br_load(current);
            }
        } else if (type == 1 && (ch == 'f' || ch == 'F')) {
            if (g_fwd_n > 0) {
                g_fwd_n--;
                br_push_history(g_back_stack, &g_back_n, current);
                strlcpy(current, g_fwd_stack[g_fwd_n], sizeof(current));
                br_load(current);
            }
        } else if (type == 1 && (ch == 'r' || ch == 'R')) {
            br_load(current);
        } else if (type == 1 && (ch == 'q' || ch == 'Q')) {
            return 0;
        }
    }
}
