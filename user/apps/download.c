/*
 * download.c — MoniOS graphical download manager (1024x768 GUI).
 *
 * Layout:
 *   top     URL input + [下载] button + save-path input
 *   middle  task rows: name / url / progress bar / speed / done-total / state
 *   bottom  status bar (total speed, active count) + task action buttons
 *
 * Features:
 *   - HTTP GET over raw TCP sockets (SYS_SOCKET_CALL TCP_*).
 *   - Up to 3 concurrent downloads; extra tasks queue.
 *   - Range resume: pausing keeps the partial buffer; resuming reissues
 *     "Range: bytes=<done>-".
 *   - 301/302 redirect following, Content-Length parsing.
 *   - Per-task progress %, speed (KB/s), done/total sizes.
 *
 * Note: files are staged in a per-task in-memory buffer and written when
 * complete (the runtime exposes file_write only, no append).  Each task
 * buffer is 128 KiB; larger files still report live progress.
 * HTTPS is not supported by the raw TCP stack; use http:// URLs.
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "monios_dll.h"
#include "string.h"
#include "stdint.h"
#include "stdbool.h"
#include "syscall.h"

#define SCR_W 1024
#define SCR_H 768

#define COL_BG      0x1E1E2Eu
#define COL_PANEL   0x2A2A3Cu
#define COL_BORDER  0x45475Au
#define COL_TEXT    0xCDD6F4u
#define COL_MUTED   0x7F849Cu
#define COL_ACCENT  0x89B4FAu
#define COL_GREEN   0xA6E3A1u
#define COL_RED     0xF38BA8u
#define COL_INPUT   0x181825u
#define COL_SEL     0x3B4261u
#define COL_WHITE   0xFFFFFFu

#define DL_DIR        "C:\\Monios\\Downloads"
#define MAX_TASKS     6
#define TASK_RING     2048u
#define TASK_BUF_CAP  (128u * 1024u)
#define MAX_ACTIVE    3

typedef enum {
    T_EMPTY = 0,
    T_QUEUED,
    T_ACTIVE,
    T_PAUSED,
    T_DONE,
    T_ERROR,
    T_CANCEL
} tstate_t;

typedef struct {
    char     url[200];
    char     name[96];
    char     path[300];
    uint32_t total;
    uint32_t done;
    uint32_t speed_kb;
    tstate_t state;
    /* live socket state */
    int      sock;
    uint8_t  ring[TASK_RING];
    uint32_t ring_len, ring_pos;
    uint8_t  phase;     /* 0 connect, 1 sent headers, 2 headers done, 3 body */
    char     hdr[1024];
    uint32_t hdr_len;
    uint8_t  buf[TASK_BUF_CAP];
    uint64_t last_tick;
    uint32_t last_done;
} task_t;

static task_t g_tasks[MAX_TASKS];
static int    g_sel;
static char   f_url[200];
static char   f_path[300];

/* ---------------- helpers ---------------- */
static uint32_t u2str(uint32_t v, char *out)
{
    char tmp[12];
    uint32_t n = 0, i = 0;
    if (v == 0) { out[0] = '0'; out[1] = '\0'; return 1; }
    while (v > 0) { tmp[n++] = (char) ('0' + (v % 10u)); v /= 10u; }
    for (i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = '\0';
    return n;
}

static void rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t c)
{
    app_graphics_fill_rect(x, y, w, h, c);
}

static void text_at(uint16_t x, uint16_t y, const char *s, uint32_t c)
{
    app_graphics_draw_text(x, y, s, c);
}

typedef struct { uint16_t x, y, w, h; } rct;
static bool rct_hit(rct r, int mx, int my)
{
    return mx >= (int) r.x && mx < (int) (r.x + r.w) &&
           my >= (int) r.y && my < (int) (r.y + r.h);
}

static void fmt_size(uint32_t v, char *out)
{
    if (v >= 1024u * 1024u) {
        u2str(v / (1024u * 1024u), out);
        { uint32_t n = (uint32_t) strlen(out); out[n] = 'M'; out[n + 1] = 'B'; out[n + 2] = '\0'; }
    } else if (v >= 1024u) {
        u2str(v / 1024u, out);
        { uint32_t n = (uint32_t) strlen(out); out[n] = 'K'; out[n + 1] = 'B'; out[n + 2] = '\0'; }
    } else {
        u2str(v, out);
    }
}

/* ---------------- URL parsing ---------------- */
static bool parse_url(const char *url, char *host, uint32_t host_sz,
                      uint16_t *port, char *path, uint32_t path_sz)
{
    const char *p = url;
    uint32_t i = 0;
    host[0] = '\0'; path[0] = '\0';
    *port = 80;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "https://", 8) == 0) return false;
    while (*p != '\0' && *p != ':' && *p != '/' && i + 1 < host_sz) host[i++] = *p++;
    host[i] = '\0';
    if (host[0] == '\0') return false;
    if (*p == ':') {
        uint32_t prt = 0;
        p++;
        while (*p >= '0' && *p <= '9') { prt = prt * 10u + (uint32_t) (*p - '0'); p++; }
        if (prt > 0 && prt < 65536u) *port = (uint16_t) prt;
    }
    i = 0;
    if (*p == '/') { while (*p != '\0' && i + 1 < path_sz) path[i++] = *p++; }
    else path[i++] = '/';
    path[i] = '\0';
    return true;
}

static void basename_of(const char *path, char *out, uint32_t out_sz)
{
    const char *slash = path;
    const char *c = path;
    out[0] = '\0';
    while (*c != '\0') { if (*c == '/') slash = c + 1; c++; }
    if (*slash == '\0') { strlcpy(out, "index.html", out_sz); return; }
    strlcpy(out, slash, out_sz);
}

/* ---------------- socket helpers ---------------- */
static int tcp_open_connect(const char *host, uint16_t port)
{
    int h = monios_socket_tcp_open(0);
    uint32_t w = 0;
    if (h < 0) return -1;
    if (monios_socket_tcp_connect(h, host, port) != 0) {
        monios_socket_close(h);
        return -1;
    }
    while (!monios_socket_tcp_connected(h) && w < 2000) {
        app_sleep_ticks(1);
        w++;
    }
    if (!monios_socket_tcp_connected(h)) {
        monios_socket_close(h);
        return -1;
    }
    return h;
}

/* ---------------- task lifecycle ---------------- */
static task_t *find_free(void)
{
    int i = 0;
    for (i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].state == T_EMPTY) return &g_tasks[i];
    }
    return 0;
}

static int count_active(void)
{
    int i = 0, n = 0;
    for (i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].state == T_ACTIVE) n++;
    }
    return n;
}

static void task_close(task_t *t)
{
    if (t->sock >= 0) { monios_socket_close(t->sock); t->sock = -1; }
}

static void task_reset_net(task_t *t)
{
    task_close(t);
    t->ring_len = t->ring_pos = 0;
    t->phase = 0;
    t->hdr_len = 0;
    t->hdr[0] = '\0';
}

static void add_task(void)
{
    task_t *t;
    char host[64], path[256];
    uint16_t port = 80;
    if (f_url[0] == '\0') return;
    t = find_free();
    if (t == 0) return;
    memset(t, 0, sizeof(*t));
    t->sock = -1;
    strlcpy(t->url, f_url, sizeof(t->url));
    if (!parse_url(f_url, host, sizeof(host), &port, path, sizeof(path))) {
        t->state = T_ERROR;
        return;
    }
    basename_of(path, t->name, sizeof(t->name));
    if (f_path[0] == '\0') {
        uint32_t p = 0, k = 0;
        strlcpy(t->path, DL_DIR, sizeof(t->path));
        p = (uint32_t) strlen(t->path);
        t->path[p++] = '\\';
        while (t->name[k] != '\0' && p + 1 < sizeof(t->path)) t->path[p++] = t->name[k++];
        t->path[p] = '\0';
    } else {
        strlcpy(t->path, f_path, sizeof(t->path));
    }
    t->state = T_QUEUED;
    g_sel = (int) (t - g_tasks);
}

/* send the GET request (with optional Range resume) */
static void task_send_request(task_t *t)
{
    char host[64], path[256], req[600];
    uint16_t port = 80;
    uint32_t i = 0;
    if (!parse_url(t->url, host, sizeof(host), &port, path, sizeof(path))) {
        t->state = T_ERROR; return;
    }
    i = 0;
    req[i++] = 'G'; req[i++] = 'E'; req[i++] = 'T'; req[i++] = ' ';
    { uint32_t k = 0; while (path[k] != '\0' && i + 1 < sizeof(req)) req[i++] = path[k++]; }
    req[i++] = ' '; req[i++] = 'H'; req[i++] = 'T'; req[i++] = 'T'; req[i++] = 'P';
    req[i++] = '/'; req[i++] = '1'; req[i++] = '.'; req[i++] = '1';
    req[i++] = '\r'; req[i++] = '\n';
    strlcpy(req + i, "Host: ", sizeof(req) - i); i = (uint32_t) strlen(req);
    { uint32_t k = 0; while (host[k] != '\0' && i + 1 < sizeof(req)) req[i++] = host[k++]; }
    strlcpy(req + i, "\r\nUser-Agent: MoniosDownloader\r\nAccept: */*\r\n", sizeof(req) - i);
    i = (uint32_t) strlen(req);
    if (t->done > 0) {
        char dg[12]; uint32_t dn = 0, v = t->done;
        strlcpy(req + i, "Range: bytes=", sizeof(req) - i); i = (uint32_t) strlen(req);
        if (v == 0) req[i++] = '0';
        while (v > 0 && dn < sizeof(dg)) { dg[dn++] = (char) ('0' + v % 10u); v /= 10u; }
        while (dn > 0 && i + 1 < sizeof(req)) req[i++] = dg[--dn];
        strlcpy(req + i, "-\r\n", sizeof(req) - i);
        i = (uint32_t) strlen(req);
    }
    strlcpy(req + i, "Connection: close\r\n\r\n", sizeof(req) - i);
    monios_socket_tcp_send(t->sock, req, (uint16_t) strlen(req));
    t->phase = 1;
}

/* parse accumulated headers; returns: 0 pending, 1 body, 2 redirect, -1 error */
static int task_parse_headers(task_t *t)
{
    const char *p;
    const char *sp;
    int code = 0;
    uint32_t cl = 0;
    /* need terminating blank line */
    if (strstr(t->hdr, "\r\n\r\n") == 0 && strstr(t->hdr, "\n\n") == 0) return 0;

    /* status code: "HTTP/1.1 200 ..." */
    p = t->hdr;
    while (*p != '\0' && *p != ' ') p++;
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') { code = code * 10 + (*p - '0'); p++; }

    if (code == 301 || code == 302) {
        const char *loc = strstr(t->hdr, "Location:");
        if (loc != 0) {
            char *out = t->url;
            uint32_t k = 0;
            loc += 9;
            while (*loc == ' ' || *loc == '\t') loc++;
            while (*loc != '\r' && *loc != '\n' && *loc != '\0' && k + 1 < sizeof(t->url)) {
                out[k++] = *loc++;
            }
            out[k] = '\0';
            return 2; /* caller restarts at new Location */
        }
    }
    if (code != 200 && code != 206) {
        t->state = T_ERROR;
        return -1;
    }
    sp = strstr(t->hdr, "Content-Length:");
    if (sp == 0) sp = strstr(t->hdr, "content-length:");
    if (sp != 0) {
        const char *v = sp + 15;
        while (*v == ' ') v++;
        while (*v >= '0' && *v <= '9') { cl = cl * 10u + (uint32_t) (*v - '0'); v++; }
    }
    if (code == 206) t->total = t->done + cl;
    else { t->total = cl; t->done = 0; }
    return 1;
}

/* pump one active task; call every frame */
static void task_pump(task_t *t)
{
    char host[64], path[256];
    uint16_t port = 80;
    uint32_t now_ticks;
    int n;

    /* speed sampling */
    now_ticks = (uint32_t) app_ticks();
    if (now_ticks - t->last_tick >= 20u) {
        uint32_t d = t->done - t->last_done;
        uint32_t dt = now_ticks - t->last_tick;
        t->speed_kb = (uint32_t) (((uint64_t) d * 1000u) / (uint64_t) dt) / 1024u;
        t->last_tick = now_ticks;
        t->last_done = t->done;
    }

    if (t->phase == 0) {
        if (!parse_url(t->url, host, sizeof(host), &port, path, sizeof(path))) {
            t->state = T_ERROR; return;
        }
        t->sock = tcp_open_connect(host, port);
        if (t->sock < 0) { t->state = T_ERROR; return; }
        task_send_request(t);
        return;
    }

    /* drain whatever is available without blocking forever */
    while (monios_socket_tcp_has_data(t->sock)) {
        uint32_t space = TASK_RING - t->ring_len;
        uint32_t want = space > 1400u ? 1400u : space;
        if (want == 0) break;
        n = monios_socket_tcp_recv(t->sock, t->ring + t->ring_len, (uint16_t) want);
        if (n <= 0) break;
        t->ring_len += (uint32_t) n;
    }

    if (t->phase == 1 || t->phase == 2) {
        /* move ring bytes into hdr buffer until headers done */
        while (t->ring_pos < t->ring_len && t->hdr_len < sizeof(t->hdr) - 1u) {
            t->hdr[t->hdr_len++] = (char) t->ring[t->ring_pos++];
        }
        t->hdr[t->hdr_len] = '\0';
        {
            int r = task_parse_headers(t);
            if (r == 1) {
                t->phase = 3;
            } else if (r == 2) {
                /* 301/302: url already rewritten; restart connect */
                task_reset_net(t);
                return;
            } else if (r == -1) {
                /* error already flagged in t->state by the parser */
                task_close(t);
                return;
            }
        }
        /* compact ring (we consumed it) */
        t->ring_len = 0; t->ring_pos = 0;
    }

    if (t->phase == 3) {
        while (t->ring_pos < t->ring_len) {
            uint8_t b = t->ring[t->ring_pos++];
            if (t->done < TASK_BUF_CAP) t->buf[t->done] = b;
            t->done++;
        }
        t->ring_len = 0; t->ring_pos = 0;
        /* finished when socket closes */
        if (!monios_socket_tcp_connected(t->sock)) {
            app_file_write(t->path, t->buf, t->done);
            task_close(t);
            t->state = T_DONE;
        }
    }
}

/* schedule queued tasks up to MAX_ACTIVE */
static void schedule(void)
{
    int i = 0;
    for (i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].state == T_QUEUED && count_active() < MAX_ACTIVE) {
            g_tasks[i].state = T_ACTIVE;
            g_tasks[i].last_tick = (uint32_t) app_ticks();
            g_tasks[i].last_done = g_tasks[i].done;
        }
    }
}

static void pause_sel(void)
{
    task_t *t;
    if (g_sel < 0 || g_sel >= MAX_TASKS) return;
    t = &g_tasks[g_sel];
    if (t->state == T_ACTIVE) {
        task_close(t);
        t->state = T_PAUSED;
    }
}

static void resume_sel(void)
{
    task_t *t;
    if (g_sel < 0 || g_sel >= MAX_TASKS) return;
    t = &g_tasks[g_sel];
    if (t->state == T_PAUSED) {
        task_reset_net(t);
        t->state = T_QUEUED;
    }
}

static void cancel_sel(void)
{
    task_t *t;
    if (g_sel < 0 || g_sel >= MAX_TASKS) return;
    t = &g_tasks[g_sel];
    task_close(t);
    memset(t, 0, sizeof(*t));
    t->sock = -1;
    t->state = T_EMPTY;
}

/* ---------------- rendering ---------------- */
static void draw_button(rct r, const char *label, uint32_t c)
{
    rect(r.x, r.y, r.w, r.h, c);
    text_at(r.x + 8, r.y + 6, label, COL_WHITE);
}

static const char *state_str(tstate_t s)
{
    switch (s) {
    case T_QUEUED:  return "排队";
    case T_ACTIVE:  return "下载中";
    case T_PAUSED:  return "已暂停";
    case T_DONE:    return "完成";
    case T_ERROR:   return "错误";
    case T_CANCEL:  return "取消";
    default:        return "";
    }
}

static uint32_t state_color(tstate_t s)
{
    switch (s) {
    case T_ACTIVE: return COL_ACCENT;
    case T_DONE:   return COL_GREEN;
    case T_ERROR:  return COL_RED;
    case T_PAUSED: return COL_MUTED;
    default:       return COL_TEXT;
    }
}

static void render(void)
{
    rct r_url = { 8, 36, 700, 22 };
    rct r_dl  = { 716, 34, 90, 26 };
    rct r_path = { 8, 66, 700, 22 };
    rct b_pause = { 8, 720, 90, 26 };
    rct b_resume = { 104, 720, 90, 26 };
    rct b_cancel = { 200, 720, 90, 26 };
    int i = 0;
    uint32_t total_speed = 0;

    rect(0, 0, SCR_W, SCR_H, COL_BG);
    text_at(8, 8, "Monios 下载管理器", COL_WHITE);

    rect(r_url.x, r_url.y, r_url.w, r_url.h, COL_INPUT);
    text_at(r_url.x + 6, r_url.y + 5, f_url[0] ? f_url : "http://example.com/file.bin",
            f_url[0] ? COL_TEXT : COL_MUTED);
    draw_button(r_dl, "下载", COL_GREEN);

    rect(r_path.x, r_path.y, r_path.w, r_path.h, COL_INPUT);
    text_at(r_path.x + 6, r_path.y + 5, f_path[0] ? f_path : "保存路径 (默认 C:\\Monios\\Downloads)",
            f_path[0] ? COL_TEXT : COL_MUTED);

    /* task rows */
    for (i = 0; i < MAX_TASKS; i++) {
        task_t *t = &g_tasks[i];
        uint16_t ry = (uint16_t) (96 + i * 62);
        char line[120];
        uint32_t q = 0;
        char sz1[16], sz2[16];
        uint32_t pct = 0;
        rct row = { 8, ry, 1008, 56 };
        if (t->state == T_EMPTY) continue;
        rect(row.x, row.y, row.w, row.h, (i == g_sel) ? COL_SEL : COL_PANEL);

        /* name + state */
        {
            uint32_t k = 0;
            while (t->name[k] != '\0' && q + 1 < sizeof(line) - 24u) line[q++] = t->name[k++];
            line[q] = '\0';
        }
        text_at(row.x + 6, ry + 3, line, COL_TEXT);
        text_at(row.x + 500, ry + 3, state_str(t->state), state_color(t->state));

        /* progress bar */
        rect(row.x + 6, ry + 22, 700, 12, COL_INPUT);
        if (t->total > 0) pct = t->done * 100u / t->total;
        {
            uint16_t pw = (uint16_t) ((uint32_t) (700) * pct / 100u);
            if (pct > 0) rect(row.x + 6, ry + 22, pw, 12, COL_ACCENT);
        }
        /* done/total + speed */
        fmt_size(t->done, sz1);
        fmt_size(t->total, sz2);
        q = 0;
        line[q++] = ' ';
        { uint32_t k = 0; while (sz1[k] != '\0' && q + 1 < sizeof(line)) line[q++] = sz1[k++]; }
        line[q++] = '/';
        { uint32_t k = 0; while (sz2[k] != '\0' && q + 1 < sizeof(line)) line[q++] = sz2[k++]; }
        line[q] = '\0';
        text_at(row.x + 720, ry + 20, line, COL_MUTED);
        {
            char spd[24];
            uint32_t k = 0;
            const char *pre = "";
            (void) pre;
            while (k < sizeof(spd)) spd[k++] = '\0';
            {
                char num[12];
                u2str(t->speed_kb, num);
                q = 0;
                while (num[q] != '\0' && q + 5 < sizeof(spd)) { spd[q] = num[q]; q++; }
                spd[q++] = 'K'; spd[q++] = 'B'; spd[q++] = '/'; spd[q++] = 's'; spd[q] = '\0';
            }
            text_at(row.x + 860, ry + 20, spd, COL_GREEN);
        }
        total_speed += t->speed_kb;
    }

    /* status bar */
    rect(8, 690, 1008, 24, COL_INPUT);
    {
        char st[64];
        uint32_t q = 0;
        const char *pre = "总速度: ";
        uint32_t k = 0;
        while (pre[k] != '\0' && q + 1 < sizeof(st)) st[q++] = pre[k++];
        {
            char num[12];
            u2str(total_speed, num);
            k = 0; while (num[k] != '\0' && q + 1 < sizeof(st)) st[q++] = num[k++];
        }
        st[q] = '\0';
        text_at(12, 694, st, COL_TEXT);
        {
            char act[32];
            uint32_t a = (uint32_t) count_active();
            char num[12];
            u2str(a, num);
            q = 0;
            const char *p2 = "  活动任务: ";
            k = 0; while (p2[k] != '\0' && q + 1 < sizeof(act)) act[q++] = p2[k++];
            k = 0; while (num[k] != '\0' && q + 1 < sizeof(act)) act[q++] = num[k++];
            act[q] = '\0';
            text_at(300, 694, act, COL_MUTED);
        }
    }

    draw_button(b_pause, "暂停", COL_BORDER);
    draw_button(b_resume, "继续", COL_GREEN);
    draw_button(b_cancel, "取消", COL_RED);

    app_graphics_present();
}

/* ---------------- keyboard ---------------- */
static int read_kb(uint32_t *type, char *ch, uint8_t *mods)
{
    uint8_t buf[8];
    int64_t r = (int64_t) syscall3(SYS_KEYBOARD_READ_EVENT, (uint64_t) buf, 0, 0);
    if (r != 1) return 0;
    *type = (uint32_t) buf[0] | ((uint32_t) buf[1] << 8) |
            ((uint32_t) buf[2] << 16) | ((uint32_t) buf[3] << 24);
    *ch = (char) buf[4];
    *mods = buf[5];
    return 1;
}

/* ---------------- main ---------------- */
int main(int argc, char **argv)
{
    int prev_buttons = 0;
    int url_focus = 1;
    int i = 0;
    (void) argc; (void) argv;

    app_file_mkdir(DL_DIR);
    for (i = 0; i < MAX_TASKS; i++) g_tasks[i].sock = -1;

    app_enter_graphics_mode();

    for (;;) {
        uint32_t type = 0;
        char ch = 0;
        uint8_t mods = 0;
        app_mouse_snapshot_t ms;
        bool clicked = false;
        int down = 0;

        app_sleep_ticks(2);

        while (read_kb(&type, &ch, &mods)) {
            if (type == 30) { return 0; }
            if (type == 1) {
                if (ch == '\r' || ch == '\n') { add_task(); url_focus = 0; }
                else if (ch == 8) {
                    uint32_t n = (uint32_t) strlen(f_url);
                    if (n > 0) f_url[n - 1] = '\0';
                } else if (ch >= 32 && ch < 127 && strlen(f_url) + 1 < sizeof(f_url)) {
                    f_url[strlen(f_url)] = ch;
                    f_url[strlen(f_url)] = '\0';
                }
            }
        }

        if (app_get_mouse(&ms) == 0) {
            down = ms.buttons & 1;
            if (down && !prev_buttons) clicked = true;
            prev_buttons = down;
        }

        if (clicked) {
            int mx = ms.x_pixels;
            int my = ms.y_pixels;
            rct r_dl = { 716, 34, 90, 26 };
            rct r_url = { 8, 36, 700, 22 };
            rct b_pause = { 8, 720, 90, 26 };
            rct b_resume = { 104, 720, 90, 26 };
            rct b_cancel = { 200, 720, 90, 26 };
            if (rct_hit(r_dl, mx, my)) add_task();
            else if (rct_hit(r_url, mx, my)) url_focus = 1;
            else if (rct_hit(b_pause, mx, my)) pause_sel();
            else if (rct_hit(b_resume, mx, my)) resume_sel();
            else if (rct_hit(b_cancel, mx, my)) cancel_sel();
            else {
                int row = (my - 96) / 62;
                if (row >= 0 && row < MAX_TASKS && g_tasks[row].state != T_EMPTY) {
                    g_sel = row;
                }
            }
        }

        schedule();
        for (i = 0; i < MAX_TASKS; i++) {
            if (g_tasks[i].state == T_ACTIVE) task_pump(&g_tasks[i]);
        }

        (void) url_focus;
        render();
    }
}
