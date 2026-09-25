/*
 * pdfview.c - Monios x64 simplified PDF viewer.
 *
 * Builds to pdfview.exe.  Usage: pdfview <file.pdf>
 *
 * This is a *text-extraction* PDF reader, not a full rendering engine:
 *   - Parses the PDF body by scanning for indirect objects ("N G obj").
 *   - Enumerates pages by locating "/Type /Page" dictionaries.
 *   - Resolves /Contents (single ref or array), reads the raw stream,
 *     applies FlateDecode via zlib_inflate() (shared from lib/zip.c).
 *   - Extracts text-showing operators (BT/ET, Tm, Td, TD, T*, Tf, Tj, TJ)
 *     and literal / hex strings, keeping each span's (x,y) position.
 *   - Lays spans out as lines and draws them with windows_draw_text().
 *
 * Navigation: Left/Right arrows or PgUp/PgDn keys, Prev/Next toolbar
 * buttons, Esc quits.  No malloc: everything lives in static buffers, and
 * the whole file is capped (1 MB) to respect the 64 MB process budget.
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "osui_dll.h"
#include "monios_dll.h"
#include "syscall.h"
#include "string.h"
#include "stdint.h"
#include "stdbool.h"
#include "zip.h"
#include "windows_dll.h"

/* ------------------------------------------------------------------ */
/* key event types                                                    */
/* ------------------------------------------------------------------ */
#define EV_CHAR   1
#define EV_UP     2
#define EV_DOWN   3
#define EV_LEFT   4
#define EV_RIGHT  5
#define EV_PGUP   6
#define EV_PGDN   7
#define EV_ESC    29

typedef struct {
    uint32_t type;
    char ch;
    uint8_t mods;
    uint8_t pad[3];
} app_key_event_t;

/* ------------------------------------------------------------------ */
/* static buffers (no malloc)                                        */
/* ------------------------------------------------------------------ */
#define FILE_BUF_SZ    (1024u * 1024u)   /* 1 MB whole-file cap   */
#define STREAM_SZ      (256u * 1024u)    /* decompressed content  */
#define MAX_OBJECTS    1024u
#define MAX_PAGES      256u
#define MAX_SPANS      600u
#define SPAN_STR_SZ    48u

static uint8_t  g_filebuf[FILE_BUF_SZ];
static uint32_t g_file_size;

static uint8_t  g_stream[STREAM_SZ];

/*
 * Text is laid out as a flowing list of lines (a robust simplification
 * for readers that do not model the full text matrix).  Each decoded text
 * object becomes a line; operators BT / T* / Td / ET start a new line.
 */
#define MAX_LINES     80u
#define LINE_SZ       128u
static char     g_lines[MAX_LINES][LINE_SZ];
static uint32_t g_line_count;
static uint32_t g_line_len;

typedef struct { uint32_t num; uint32_t offset; } obj_entry_t;
static obj_entry_t g_objs[MAX_OBJECTS];
static uint32_t g_obj_count;

static uint32_t g_page_offsets[MAX_PAGES];
static uint32_t g_page_count;

static double g_media_x0, g_media_y0, g_media_x1, g_media_y1;

static uint32_t g_cur_page;
static bool     g_loaded;
static char     g_status[80];
static char     g_file_name[64];

static uint32_t g_screen_w;
static uint32_t g_screen_h;
static bool     g_dirty = true;

/* clickable controls */
#define MAX_CTRLS 16
typedef struct { osui_rect_t rect; uint32_t action; } ctrl_t;
static ctrl_t g_ctrls[MAX_CTRLS];
static uint32_t g_ctrl_count;

enum { ACT_PREV, ACT_NEXT, ACT_OPEN, ACT_ZOOM_OUT, ACT_ZOOM_IN };
static double g_zoom = 1.0;

static void ctrl_add(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t a)
{
    if (g_ctrl_count >= MAX_CTRLS) return;
    g_ctrls[g_ctrl_count].rect.x = x;
    g_ctrls[g_ctrl_count].rect.y = y;
    g_ctrls[g_ctrl_count].rect.width = w;
    g_ctrls[g_ctrl_count].rect.height = h;
    g_ctrls[g_ctrl_count].action = a;
    g_ctrl_count++;
}

/* ------------------------------------------------------------------ */
/* small helpers                                                      */
/* ------------------------------------------------------------------ */
static uint32_t xlen(const char *s){ uint32_t n=0; while(s[n])n++; return n; }

static bool is_digit(char c){ return c>='0'&&c<='9'; }

/* parse an unsigned integer starting at *pp, advancing *pp */
static uint32_t read_u32(const uint8_t *d, uint32_t *pp)
{
    uint32_t v = 0;
    while (d[*pp] >= '0' && d[*pp] <= '9') { v = v*10 + (d[*pp]-'0'); (*pp)++; }
    return v;
}

/* find substring needle (len nlen) in buf[0..size); returns offset or -1 */
static int32_t find_bytes(const uint8_t *buf, uint32_t size, const char *needle, uint32_t nlen)
{
    uint32_t i;
    if (nlen == 0 || nlen > size) return -1;
    for (i = 0; i <= size - nlen; i++) {
        uint32_t k = 0;
        while (k < nlen && buf[i+k] == (uint8_t)needle[k]) k++;
        if (k == nlen) return (int32_t) i;
    }
    return -1;
}

/* skip spaces and PDF comments "%...\n" starting at *pp within size */
static void skip_ws(const uint8_t *d, uint32_t size, uint32_t *pp)
{
    while (*pp < size) {
        uint8_t c = d[*pp];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') (*pp)++;
        else if (c == '%') {
            while (*pp < size && d[*pp] != '\n') (*pp)++;
        } else break;
    }
}

/* ------------------------------------------------------------------ */
/* object table                                                       */
/* ------------------------------------------------------------------ */
static void build_object_table(void)
{
    uint32_t i = 0;
    g_obj_count = 0;
    while (i < g_file_size && g_obj_count < MAX_OBJECTS) {
        uint32_t start = i;
        if (is_digit((char)g_filebuf[i])) {
            uint32_t num = read_u32(g_filebuf, &i);
            if (g_filebuf[i] == ' ') {
                i++;
                if (is_digit((char)g_filebuf[i])) {
                    uint32_t gen = read_u32(g_filebuf, &i);
                    (void) gen;
                    if (g_filebuf[i] == ' ' && g_filebuf[i+1]=='o' &&
                        g_filebuf[i+2]=='b' && g_filebuf[i+3]=='j' &&
                        (g_filebuf[i+4]==' ' || g_filebuf[i+4]=='\r' ||
                         g_filebuf[i+4]=='\n' || g_filebuf[i+4]=='<')) {
                        g_objs[g_obj_count].num = num;
                        g_objs[g_obj_count].offset = start;
                        g_obj_count++;
                    }
                }
            }
        }
        i = start + 1;
    }
}

static int32_t find_object(uint32_t num)
{
    for (uint32_t i = 0; i < g_obj_count; i++)
        if (g_objs[i].num == num) return (int32_t) g_objs[i].offset;
    return -1;
}

/* end offset of an object (position of "endobj") */
static int32_t obj_end(uint32_t off)
{
    int32_t e = find_bytes(g_filebuf + off, g_file_size - off, "endobj", 6);
    return e < 0 ? (int32_t) g_file_size - (int32_t) off : off + e;
}

/* ------------------------------------------------------------------ */
/* stream extraction (with optional FlateDecode)                     */
/* ------------------------------------------------------------------ */
/*
 * Locate the content stream belonging to a page object.
 * Writes decompressed bytes into g_stream, returns length (0 on error).
 */
static uint32_t extract_stream(uint32_t page_off)
{
    int32_t pend = obj_end(page_off);
    uint32_t plen = (uint32_t)(pend - (int32_t)page_off);
    const uint8_t *p = g_filebuf + page_off;
    uint32_t total = 0;

    /* locate /Contents */
    int32_t cpos = find_bytes(p, plen, "/Contents", 9);
    if (cpos < 0) return 0;
    uint32_t i = (uint32_t)cpos + 9;
    skip_ws(p, plen, &i);

    /* collect reference numbers (single ref or array of refs) */
    uint32_t refs[8];
    uint32_t ref_count = 0;
    if (i < plen && p[i] == '[') {
        i++;
        while (i < plen && ref_count < 8) {
            skip_ws(p, plen, &i);
            if (i >= plen || p[i] == ']') break;
            if (is_digit((char)p[i])) {
                uint32_t n = read_u32(p, &i);
                skip_ws(p, plen, &i);
                if (is_digit((char)p[i])) read_u32(p, &i); /* gen */
                skip_ws(p, plen, &i);
                if (i+1 < plen && p[i]=='0' && p[i+1]==' ') {}
                /* consume "R" */
                if (i < plen && p[i] == 'R') i++;
                refs[ref_count++] = n;
            } else break;
        }
    } else {
        if (is_digit((char)p[i])) {
            refs[ref_count++] = read_u32(p, &i);
            skip_ws(p, plen, &i);
            if (is_digit((char)p[i])) read_u32(p, &i);
            skip_ws(p, plen, &i);
            if (i < plen && p[i] == 'R') i++;
        } else return 0;
    }

    for (uint32_t r = 0; r < ref_count; r++) {
        int32_t so = find_object(refs[r]);
        if (so < 0) continue;
        int32_t se = obj_end((uint32_t)so);
        uint32_t slen = (uint32_t)(se - so);
        const uint8_t *sp = g_filebuf + so;

        int32_t st = find_bytes(sp, slen, "stream", 6);
        if (st < 0) continue;
        uint32_t di = (uint32_t)st + 6;
        /* skip EOL after "stream" */
        if (di < slen && sp[di] == '\r') di++;
        if (di < slen && sp[di] == '\n') di++;

        /* find "endstream" */
        int32_t ee = find_bytes(sp + di, slen - di, "endstream", 9);
        uint32_t raw_len = (ee < 0) ? (slen - di) : (uint32_t) ee;
        const uint8_t *raw = sp + di;

        /* FlateDecode? search in the dict before "stream" */
        bool flate = find_bytes(sp, (uint32_t)st, "/FlateDecode", 12) >= 0;

        if (flate) {
            int got = zlib_inflate(raw, raw_len, g_stream + total,
                                   STREAM_SZ - total);
            if (got < 0) continue;
            total += (uint32_t) got;
        } else {
            uint32_t cp = raw_len;
            if (total + cp > STREAM_SZ) cp = STREAM_SZ - total;
            for (uint32_t k = 0; k < cp; k++) g_stream[total + k] = raw[k];
            total += cp;
        }
    }
    return total;
}

/* parse /MediaBox [ x0 y0 x1 y1 ] from page object */
static void parse_mediabox(uint32_t page_off)
{
    int32_t pend = obj_end(page_off);
    uint32_t plen = (uint32_t)(pend - (int32_t)page_off);
    const uint8_t *p = g_filebuf + page_off;
    int32_t mb = find_bytes(p, plen, "/MediaBox", 9);
    g_media_x0 = 0; g_media_y0 = 0; g_media_x1 = 612; g_media_y1 = 792;
    if (mb < 0) return;
    uint32_t i = (uint32_t)mb + 9;
    skip_ws(p, plen, &i);
    if (i < plen && p[i] == '[') i++;
    double v[4];
    for (int k = 0; k < 4; k++) {
        skip_ws(p, plen, &i);
        v[k] = 0.0;
        /* strtod-ish */
        uint32_t start = i;
        while (i < plen && (p[i]=='-'||p[i]=='+'||p[i]=='.'||is_digit((char)p[i]))) i++;
        /* convert */
        {
            char tmp[24]; uint32_t t=0;
            while (start < i && t < sizeof(tmp)-1) tmp[t++]=p[start++];
            tmp[t]=0;
            /* manual atof */
            double sign=1,val=0,frac=1; bool hasfrac=false; uint32_t q=0;
            if (tmp[0]=='-'){sign=-1;q=1;} else if(tmp[0]=='+')q=1;
            while (tmp[q]){
                if (tmp[q]=='.'){hasfrac=true;q++;continue;}
                if (hasfrac){frac*=0.1; val+=(tmp[q]-'0')*frac;}
                else val=val*10+(tmp[q]-'0');
                q++;
            }
            v[k]=sign*val;
        }
    }
    g_media_x0=v[0]; g_media_y0=v[1]; g_media_x1=v[2]; g_media_y1=v[3];
}

/* ------------------------------------------------------------------ */
/* literal / hex string decoding                                      */
/* ------------------------------------------------------------------ */
static uint32_t decode_literal(const uint8_t *s, uint32_t len, uint32_t *pi,
                               char *out, uint32_t outcap)
{
    uint32_t o = 0;
    int depth = 1;
    (*pi)++; /* skip '(' */
    while (*pi < len && depth > 0 && o < outcap - 1) {
        char c = (char) s[*pi];
        if (c == '\\') {
            (*pi)++;
            char e = (char) s[*pi];
            switch (e) {
                case 'n': out[o++]='\n'; break;
                case 'r': out[o++]='\r'; break;
                case 't': out[o++]='\t'; break;
                case 'b': out[o++]='\b'; break;
                case 'f': out[o++]='\f'; break;
                case '(': out[o++]='('; break;
                case ')': out[o++]=')'; break;
                case '\\': out[o++]='\\'; break;
                default:
                    if (e >= '0' && e <= '7') {
                        uint32_t oct = e - '0';
                        uint32_t d = 0;
                        while (d < 2 && (*pi)+1 < len &&
                               s[*pi+1] >= '0' && s[*pi+1] <= '7') {
                            (*pi)++; oct = oct*8 + (s[*pi]-'0'); d++;
                        }
                        out[o++] = (char) oct;
                    } else { out[o++] = e; }
                    break;
            }
            (*pi)++;
        } else if (c == '(') { depth++; out[o++] = c; (*pi)++; }
        else if (c == ')') { depth--; if (depth==0) (*pi)++; else { out[o++]=c; (*pi)++; } }
        else { out[o++] = c; (*pi)++; }
    }
    out[o] = 0;
    return o;
}

static uint32_t decode_hex(const uint8_t *s, uint32_t len, uint32_t *pi,
                           char *out, uint32_t outcap)
{
    uint32_t o = 0;
    (*pi)++; /* skip '<' */
    uint8_t hi = 0, have = 0;
    while (*pi < len && o < outcap - 1) {
        char c = (char) s[*pi];
        if (c == '>') { (*pi)++; break; }
        if (c==' '||c=='\t'||c=='\r'||c=='\n'){(*pi)++;continue;}
        uint8_t nib;
        if (c>='0'&&c<='9') nib = (uint8_t)(c-'0');
        else if (c>='a'&&c<='f') nib = (uint8_t)(c-'a'+10);
        else if (c>='A'&&c<='F') nib = (uint8_t)(c-'A'+10);
        else { (*pi)++; continue; }
        if (!have) { hi = nib << 4; have = 1; }
        else { out[o++] = (char)(hi | nib); have = 0; }
        (*pi)++;
    }
    out[o] = 0;
    return o;
}

/* ------------------------------------------------------------------ */
/* content-stream text extraction                                     */
/* ------------------------------------------------------------------ */
static double tok_num(const char *t)
{
    double sign=1,val=0,frac=1; bool hf=false; uint32_t q=0;
    if (t[0]=='-'){sign=-1;q=1;} else if(t[0]=='+')q=1;
    while (t[q]){
        if (t[q]=='.'){hf=true;q++;continue;}
        if (hf){frac*=0.1; val+=(t[q]-'0')*frac;}
        else val=val*10+(t[q]-'0');
        q++;
    }
    return sign*val;
}

static bool is_num_tok(const char *t)
{
    uint32_t q=0;
    if (t[0]=='-'||t[0]=='+') q=1;
    if (t[q]==0) return false;
    while (t[q]) {
        if (!(t[q]>='0'&&t[q]<='9') && t[q]!='.') return false;
        q++;
    }
    return true;
}

static void line_new(void)
{
    if (g_line_count >= MAX_LINES) return;
    g_line_len = 0;
    g_lines[g_line_count][0] = 0;
}

static void line_flush(void)
{
    if (g_line_count >= MAX_LINES) return;
    /* drop blank lines */
    if (g_line_len == 0) return;
    g_line_count++;
    line_new();
}

static void line_append(const char *s)
{
    if (g_line_count >= MAX_LINES) return;
    uint32_t k = 0;
    while (s[k] && g_line_len < LINE_SZ - 1) {
        g_lines[g_line_count][g_line_len++] = s[k++];
    }
    g_lines[g_line_count][g_line_len] = 0;
}

static void extract_text(uint32_t stream_len)
{
    const uint8_t *s = g_stream;
    uint32_t i = 0;
    bool in_text = false;
    char pending[SPAN_STR_SZ];
    bool have_pending = false;
    char tok[32];

    g_line_count = 0;
    line_new();
    while (i < stream_len) {
        skip_ws(s, stream_len, &i);
        if (i >= stream_len) break;

        /* literal string */
        if (s[i] == '(') {
            decode_literal(s, stream_len, &i, pending, sizeof(pending));
            have_pending = true;
            continue;
        }
        /* hex string */
        if (s[i] == '<' && i+1 < stream_len && s[i+1] != '<') {
            decode_hex(s, stream_len, &i, pending, sizeof(pending));
            have_pending = true;
            continue;
        }
        /* array for TJ */
        if (s[i] == '[') {
            i++;
            while (i < stream_len) {
                skip_ws(s, stream_len, &i);
                if (i >= stream_len || s[i] == ']') { i++; break; }
                if (s[i] == '(') {
                    char buf[SPAN_STR_SZ];
                    decode_literal(s, stream_len, &i, buf, sizeof(buf));
                    if (in_text) line_append(buf);
                } else if (s[i] == '<') {
                    char buf[SPAN_STR_SZ];
                    decode_hex(s, stream_len, &i, buf, sizeof(buf));
                    if (in_text) line_append(buf);
                } else {
                    i++; /* skip adjustment number */
                }
            }
            skip_ws(s, stream_len, &i);
            /* skip "TJ" */
            i += 2;
            have_pending = false;
            continue;
        }
        /* gather operator / number token */
        {
            uint32_t k = 0;
            while (i < stream_len && s[i]!=' '&&s[i]!='\t'&&s[i]!='\r'&&s[i]!='\n'&&
                   s[i]!='('&&s[i]!='['&&s[i]!='<'&&k<sizeof(tok)-1)
                tok[k++] = (char)s[i++];
            tok[k] = 0;
        }
        if (tok[0] == 0) continue;
        if (!have_pending && is_num_tok(tok)) continue; /* operand, ignore */

        if (!strcmp(tok, "BT")) {
            in_text = true; line_flush(); have_pending = false;
        }
        else if (!strcmp(tok, "ET")) {
            in_text = false; line_flush(); have_pending = false;
        }
        else if (!strcmp(tok, "Tj")) {
            if (in_text && have_pending) line_append(pending);
            have_pending = false;
        }
        else if (!strcmp(tok, "Td") || !strcmp(tok, "TD")) { line_flush(); have_pending=false; }
        else if (!strcmp(tok, "T*")) { line_flush(); have_pending=false; }
        else if (!strcmp(tok, "Tm")) { have_pending=false; }
        else if (!strcmp(tok, "Tf")) { have_pending=false; }
        else if (!strcmp(tok, "'") || !strcmp(tok, "\"")) { line_flush(); have_pending=false; }
        else { have_pending = false; }
    }
    line_flush();
}

/* ------------------------------------------------------------------ */
/* page loading                                                       */
/* ------------------------------------------------------------------ */
static void enumerate_pages(void)
{
    uint32_t scan = 0;
    g_page_count = 0;
    while (scan + 16 < g_file_size && g_page_count < MAX_PAGES) {
        int32_t f = find_bytes(g_filebuf + scan, g_file_size - scan, "/Type/Page", 10);
        if (f < 0) break;
        uint32_t at = scan + (uint32_t) f;
        /* exclude "/Type/Pages": check char after "/Type/Page" */
        uint32_t after = at + 10;
        if (after < g_file_size && g_filebuf[after] != 's') {
            /* backtrack to enclosing object start ("N G obj") */
            int32_t o = find_bytes(g_filebuf + 0, at + 1, "obj", 3);
            (void) o;
            /* find nearest preceding object by scanning table offsets */
            int32_t best = -1;
            for (uint32_t i = 0; i < g_obj_count; i++) {
                if ((int32_t)g_objs[i].offset <= (int32_t)at &&
                    (int32_t)g_objs[i].offset > best)
                    best = (int32_t) g_objs[i].offset;
            }
            if (best >= 0) g_page_offsets[g_page_count++] = (uint32_t) best;
        }
        scan = at + 10;
    }
}

static void load_page(uint32_t idx)
{
    if (idx >= g_page_count) return;
    g_cur_page = idx;
    uint32_t off = g_page_offsets[idx];
    parse_mediabox(off);
    uint32_t sl = extract_stream(off);
    if (sl > 0) extract_text(sl);
    else g_line_count = 0;

    char b[80];
    int k = 0;
    b[k++] = 'P'; b[k++] = 'a'; b[k++] = 'g'; b[k++] = 'e'; b[k++] = ' ';
    {
        uint32_t p1 = g_cur_page + 1;
        b[k++] = (char)('0' + p1 / 10); b[k++] = (char)('0' + p1 % 10);
    }
    b[k++] = '/';
    b[k++] = (char)('0' + g_page_count / 10); b[k++] = (char)('0' + g_page_count % 10);
    b[k] = 0;
    uint32_t t = 0; while (b[t] && t < sizeof(g_status)-1) { g_status[t]=b[t]; t++; }
    g_status[t]=0;
    g_dirty = true;
}

static bool load_pdf(const char *path)
{
    int32_t sz = app_file_size(path);
    g_loaded = false;
    g_page_count = 0;
    g_line_count = 0;
    if (sz <= 0) {
        uint32_t t=0; const char *m="Cannot open file";
        while (m[t]&&t<sizeof(g_status)-1){g_status[t]=m[t];t++;} g_status[t]=0;
        return false;
    }
    g_file_size = (uint32_t) sz;
    if (g_file_size > FILE_BUF_SZ) g_file_size = FILE_BUF_SZ;
    if (app_file_read(path, g_filebuf, g_file_size) < 0) return false;

    build_object_table();
    enumerate_pages();
    if (g_page_count == 0) {
        uint32_t t=0; const char *m="No pages found";
        while (m[t]&&t<sizeof(g_status)-1){g_status[t]=m[t];t++;} g_status[t]=0;
        return false;
    }
    g_loaded = true;
    load_page(0);
    return true;
}

/* ------------------------------------------------------------------ */
/* rendering                                                          */
/* ------------------------------------------------------------------ */
static bool point_in(const ctrl_t *c, int32_t px, int32_t py)
{
    return px >= c->rect.x && px < c->rect.x + c->rect.width &&
           py >= c->rect.y && py < c->rect.y + c->rect.height;
}

static void render(void)
{
    g_ctrl_count = 0;
    osui_canvas(0x00EAF0F6);
    osui_titlebar((osui_rect_t){ 0, 0, (uint16_t)g_screen_w, 36 }, "PDF Viewer", true);

    /* toolbar */
    osui_button((osui_rect_t){ 8, 44, 70, 28 }, "Open", 0);
    ctrl_add(8, 44, 70, 28, ACT_OPEN);
    osui_button((osui_rect_t){ 86, 44, 70, 28 }, "Prev", 0);
    ctrl_add(86, 44, 70, 28, ACT_PREV);
    osui_button((osui_rect_t){ 164, 44, 70, 28 }, "Next", 0);
    ctrl_add(164, 44, 70, 28, ACT_NEXT);
    osui_button((osui_rect_t){ 242, 44, 34, 28 }, "-", 0);
    ctrl_add(242, 44, 34, 28, ACT_ZOOM_OUT);
    osui_button((osui_rect_t){ 282, 44, 34, 28 }, "+", 0);
    ctrl_add(282, 44, 34, 28, ACT_ZOOM_IN);
    osui_label(326, 52, g_status, false);

    /* content area: white page card */
    uint16_t cx = 8, cy = 84;
    uint16_t cw = (uint16_t)(g_screen_w - 16);
    uint16_t ch = (uint16_t)(g_screen_h - 84 - 36);
    osui_card((osui_rect_t){ cx, cy, cw, ch });

    if (!g_loaded) {
        osui_label(cx + 16, cy + 16, g_file_name[0] ? g_file_name : "No document", false);
        osui_label(cx + 16, cy + 40, g_status, true);
        osui_label(cx + 16, cy + 64, "Usage: pdfview <file.pdf>", true);
    } else {
        /* flowing text lines */
        uint16_t ly = (uint16_t)(cy + 16);
        uint16_t lh = (uint16_t)(16 * g_zoom);
        if (lh < 12) lh = 12;
        for (uint32_t i = 0; i < g_line_count; i++) {
            windows_draw_text((uint16_t)(cx + 16), ly, g_lines[i], 0x00202020);
            ly = (uint16_t)(ly + lh);
            if (ly > cy + ch - 20) break;
        }
        if (g_line_count == 0) {
            osui_label(cx + 16, cy + 16, "(no extractable text on this page)", true);
        }
    }

    osui_statusbar((osui_rect_t){ 0, (uint16_t)(g_screen_h - 28), (uint16_t)g_screen_w, 28 },
                   g_file_name[0] ? g_file_name : "pdfview", g_status,
                   g_loaded ? OSUI_STATE_SUCCESS : OSUI_STATE_WARNING);
    osui_present();
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    app_key_event_t ev;
    app_mouse_snapshot_t mouse;
    int32_t prev_btn = 0;

    fputs("pdfview.exe\r\n");

    g_dirty = true;
    g_status[0] = 0; g_file_name[0] = 0;

    app_enter_graphics_mode();
    g_screen_w = (uint32_t)monios_syscall0(SYS_GRAPHICS_GET_WIDTH);
    g_screen_h = (uint32_t)monios_syscall0(SYS_GRAPHICS_GET_HEIGHT);
    if (g_screen_w == 0) g_screen_w = 1024;
    if (g_screen_h == 0) g_screen_h = 768;

    if (argc >= 2) {
        uint32_t j = 0;
        while (argv[1][j] && j < sizeof(g_file_name) - 1) {
            /* keep only basename for the title */
            g_file_name[j] = argv[1][j]; j++;
        }
        g_file_name[j] = 0;
        load_pdf(argv[1]);
    } else {
        const char *def = "C:\\Monios\\sample.pdf";
        if (app_file_exists(def)) load_pdf(def);
        else {
            uint32_t t=0; const char *m="No file argument";
            while (m[t]&&t<sizeof(g_status)-1){g_status[t]=m[t];t++;} g_status[t]=0;
        }
    }

    for (;;) {
        bool did = false;
        while (syscall1(SYS_KEYBOARD_READ_EVENT, (uint64_t)&ev) == 1) {
            did = true;
            if (ev.type == EV_CHAR) {
                if (ev.ch == 'q' || ev.ch == 'Q') app_exit(0);
            } else if (ev.type == EV_ESC) {
                app_exit(0);
            } else if (ev.type == EV_RIGHT || ev.type == EV_PGDN) {
                if (g_loaded && g_cur_page + 1 < g_page_count) load_page(g_cur_page + 1);
            } else if (ev.type == EV_LEFT || ev.type == EV_PGUP) {
                if (g_loaded && g_cur_page > 0) load_page(g_cur_page - 1);
            }
        }

        if (app_get_mouse(&mouse) >= 0) {
            int32_t btn = (int32_t)mouse.buttons;
            if ((btn & 1) && !(prev_btn & 1)) {
                for (uint32_t i = 0; i < g_ctrl_count; i++) {
                    if (point_in(&g_ctrls[i], mouse.x_pixels, mouse.y_pixels)) {
                        switch (g_ctrls[i].action) {
                            case ACT_PREV:
                                if (g_cur_page > 0) load_page(g_cur_page - 1);
                                break;
                            case ACT_NEXT:
                                if (g_cur_page + 1 < g_page_count) load_page(g_cur_page + 1);
                                break;
                            case ACT_ZOOM_IN: g_zoom *= 1.2; if (g_zoom>4.0) g_zoom=4.0; g_dirty=true; break;
                            case ACT_ZOOM_OUT: g_zoom /= 1.2; if (g_zoom<0.3) g_zoom=0.3; g_dirty=true; break;
                            default: break;
                        }
                        break;
                    }
                }
            }
            prev_btn = btn;
        }

        if (g_dirty) { render(); g_dirty = false; }
        if (!did) app_sleep_ticks(3);
    }
    return 0;
}
