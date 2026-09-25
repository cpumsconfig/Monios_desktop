/*
 * mail.c -- Monios simple SMTP/POP3 mail client.
 *
 *   1024x768 GUI. Toolbar: 收信 / 写信 / 删除 / 设置.
 *   Left folder list, right-top message list, right-bottom preview.
 *
 *   SMTP: connect port 25, EHLO, AUTH LOGIN (base64), MAIL FROM,
 *         RCPT TO, DATA, QUIT.
 *   POP3: connect port 110, USER, PASS, LIST, RETR, DELE, QUIT.
 *
 *   Config: /Monios/Config/mail.cfg  (key=value lines)
 *
 *   Keys:
 *     Tab / arrows  move focus
 *     Enter         activate button / send
 *     Esc           back / quit
 *     Typable chars enter the focused text field
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "monios_dll.h"
#include "string.h"
#include "stdint.h"
#include "stdbool.h"
#include "syscall.h"
#include "socket.h"

#define EV_CHAR   1
#define EV_ESC    29
#define EV_TAB    15
#define EV_ENTER  28

typedef struct {
    uint32_t type;
    char ch;
    uint8_t mods;
    uint8_t pad[3];
} app_key_event_t;

/* ---------------- config ---------------- */
#define CFG_PATH "/Monios/Config/mail.cfg"

typedef struct {
    char smtp_host[64];
    char smtp_port[8];
    char pop3_host[64];
    char pop3_port[8];
    char username[64];
    char password[64];
    char display_name[32];
} mail_config_t;

static mail_config_t g_cfg;

static void cfg_defaults(void)
{
    strlcpy(g_cfg.smtp_host, "mail.example.com", sizeof(g_cfg.smtp_host));
    strlcpy(g_cfg.smtp_port, "25", sizeof(g_cfg.smtp_port));
    strlcpy(g_cfg.pop3_host, "mail.example.com", sizeof(g_cfg.pop3_host));
    strlcpy(g_cfg.pop3_port, "110", sizeof(g_cfg.pop3_port));
    strlcpy(g_cfg.username, "user@example.com", sizeof(g_cfg.username));
    strlcpy(g_cfg.password, "", sizeof(g_cfg.password));
    strlcpy(g_cfg.display_name, "Monios User", sizeof(g_cfg.display_name));
}

static void cfg_load(void)
{
    static char buf[1024];
    int n;
    char *line;

    cfg_defaults();
    n = app_file_read(CFG_PATH, buf, sizeof(buf) - 1u);
    if (n <= 0) return;
    buf[n] = '\0';

    line = buf;
    while (*line != '\0') {
        char *eol = line;
        char *eq;
        char *key, *val;
        uint32_t i;

        while (*eol != '\0' && *eol != '\n') eol++;
        if (*eol == '\n') { *eol = '\0'; eol++; }
        /* strip CR */
        i = (uint32_t)strlen(line);
        if (i > 0 && line[i - 1] == '\r') line[i - 1] = '\0';

        eq = strchr(line, '=');
        if (eq != 0) {
            *eq = '\0';
            key = line;
            val = eq + 1;
            if (strcmp(key, "smtp_host") == 0)       strlcpy(g_cfg.smtp_host, val, sizeof(g_cfg.smtp_host));
            else if (strcmp(key, "smtp_port") == 0)  strlcpy(g_cfg.smtp_port, val, sizeof(g_cfg.smtp_port));
            else if (strcmp(key, "pop3_host") == 0)  strlcpy(g_cfg.pop3_host, val, sizeof(g_cfg.pop3_host));
            else if (strcmp(key, "pop3_port") == 0)  strlcpy(g_cfg.pop3_port, val, sizeof(g_cfg.pop3_port));
            else if (strcmp(key, "username") == 0)   strlcpy(g_cfg.username, val, sizeof(g_cfg.username));
            else if (strcmp(key, "password") == 0)   strlcpy(g_cfg.password, val, sizeof(g_cfg.password));
            else if (strcmp(key, "display_name") == 0) strlcpy(g_cfg.display_name, val, sizeof(g_cfg.display_name));
        }
        line = eol;
    }
}

static void cfg_save(void)
{
    char buf[1024];
    int n = 0;
    n += sprintf(buf + n, "smtp_host=%s\n", g_cfg.smtp_host);
    n += sprintf(buf + n, "smtp_port=%s\n", g_cfg.smtp_port);
    n += sprintf(buf + n, "pop3_host=%s\n", g_cfg.pop3_host);
    n += sprintf(buf + n, "pop3_port=%s\n", g_cfg.pop3_port);
    n += sprintf(buf + n, "username=%s\n", g_cfg.username);
    n += sprintf(buf + n, "password=%s\n", g_cfg.password);
    n += sprintf(buf + n, "display_name=%s\n", g_cfg.display_name);
    app_file_write(CFG_PATH, buf, (uint32_t)n);
}

/* ---------------- base64 ---------------- */
static int prefix_ci(const char *s, const char *prefix)
{
    while (*prefix != '\0') {
        char a = *s;
        char b = *prefix;
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return 1;
        s++; prefix++;
    }
    return 0;
}

static void b64_encode(const char *in, char *out, uint32_t outsz)
{
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t len = (uint32_t)strlen(in);
    uint32_t i = 0, o = 0;
    while (i < len && o + 4 < outsz) {
        uint32_t b0 = (uint8_t)in[i++];
        uint32_t b1 = (i < len) ? (uint8_t)in[i++] : 0;
        uint32_t b2 = (i < len) ? (uint8_t)in[i++] : 0;
        uint32_t v = (b0 << 16) | (b1 << 8) | b2;
        out[o++] = t[(v >> 18) & 63];
        out[o++] = t[(v >> 12) & 63];
        out[o++] = (i - 1 < len) ? t[(v >> 6) & 63] : '=';
        out[o++] = (i < len) ? t[v & 63] : '=';
    }
    out[o] = '\0';
}

/* ---------------- socket line reader ---------------- */
#define RDBUF 1024u
typedef struct {
    int sock;
    uint8_t buf[RDBUF];
    uint32_t len, pos;
} rdr_t;

static int rd_refill(rdr_t *r)
{
    int n;
    if (r->pos < r->len) return 1;
    r->len = 0; r->pos = 0;
    {
        uint32_t spins = 0;
        while (!monios_socket_tcp_has_data(r->sock)) {
            app_sleep_ticks(1);
            if (++spins > 2000) return 0;
        }
    }
    n = monios_socket_tcp_recv(r->sock, r->buf, sizeof(r->buf));
    if (n <= 0) return 0;
    r->len = (uint32_t)n;
    r->pos = 0;
    return 1;
}

static uint32_t rd_line(rdr_t *r, char *out, uint32_t outsz)
{
    uint32_t i = 0;
    char c;
    for (;;) {
        if (r->pos >= r->len) {
            if (!rd_refill(r)) { out[i] = '\0'; return i; }
        }
        c = (char)r->buf[r->pos++];
        if (c == '\n') break;
        if (i + 1 < outsz) out[i++] = c;
    }
    out[i] = '\0';
    if (i > 0 && out[i - 1] == '\r') out[--i] = '\0';
    return i;
}

static int tcp_connect_to(const char *host, uint16_t port)
{
    int h = monios_socket_tcp_open(0);
    uint32_t w = 0;
    if (h < 0) return -1;
    if (monios_socket_tcp_connect(h, host, port) != 0) {
        monios_socket_close(h);
        return -1;
    }
    while (!monios_socket_tcp_connected(h) && w < 3000) {
        app_sleep_ticks(1);
        w++;
    }
    if (!monios_socket_tcp_connected(h)) {
        monios_socket_close(h);
        return -1;
    }
    return h;
}

static uint16_t parse_port(const char *s)
{
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10u + (uint32_t)(*s - '0'); s++; }
    if (v == 0 || v > 65535) return 110;
    return (uint16_t)v;
}

/* ---------------- message store ---------------- */
#define MAX_MSGS 30u
typedef struct {
    char from[64];
    char subject[64];
    char date[32];
    char body[512];
    uint32_t size;
    uint32_t msgno;
} mail_msg_t;

static mail_msg_t g_msgs[MAX_MSGS];
static uint32_t g_msg_count;
static int g_selected = -1;
static char g_status[80];

/* ---------------- UI state ---------------- */
enum { UI_MAIN, UI_COMPOSE, UI_SETTINGS };
static uint32_t g_ui;

/* compose fields */
static char g_comp_to[64];
static char g_comp_subject[64];
static char g_comp_body[256];
static int g_comp_focus; /* 0=to 1=subject 2=body */

/* settings field focus */
static int g_set_focus;

/* button rectangles */
typedef struct { uint16_t x, y, w, h; const char *label; } button_t;
static button_t g_btns[4];
static uint8_t g_btn_count;

static void set_status(const char *s)
{
    strlcpy(g_status, s, sizeof(g_status));
}

/* ---------------- POP3 receive ---------------- */
static void pop3_fetch(void)
{
    int s;
    rdr_t r;
    char line[256];
    uint16_t port = parse_port(g_cfg.pop3_port);

    set_status("Connecting POP3...");
    s = tcp_connect_to(g_cfg.pop3_host, port);
    if (s < 0) { set_status("POP3 connect failed"); return; }
    r.sock = s; r.len = 0; r.pos = 0;

    /* greeting */
    rd_line(&r, line, sizeof(line));
    if (line[0] != '+') { set_status("POP3 greeting error"); monios_socket_close(s); return; }

    /* USER */
    {
        char cmd[128];
        uint32_t n = sprintf(cmd, "USER %s\r\n", g_cfg.username);
        monios_socket_tcp_send(s, cmd, (uint16_t)n);
        rd_line(&r, line, sizeof(line));
    }
    /* PASS */
    {
        char cmd[128];
        uint32_t n = sprintf(cmd, "PASS %s\r\n", g_cfg.password);
        monios_socket_tcp_send(s, cmd, (uint16_t)n);
        rd_line(&r, line, sizeof(line));
        if (line[0] != '+') { set_status("POP3 auth failed"); monios_socket_close(s); return; }
    }

    /* LIST */
    monios_socket_tcp_send(s, "LIST\r\n", 6);
    g_msg_count = 0;
    for (;;) {
        uint32_t n = rd_line(&r, line, sizeof(line));
        uint32_t msgnum, size;
        char *p;
        if (n == 0) break;
        if (strcmp(line, ".") == 0) break;
        if (line[0] == '.') continue;
        /* parse "n size" */
        msgnum = 0; p = line;
        while (*p >= '0' && *p <= '9') { msgnum = msgnum * 10u + (uint32_t)(*p - '0'); p++; }
        while (*p == ' ') p++;
        size = 0;
        while (*p >= '0' && *p <= '9') { size = size * 10u + (uint32_t)(*p - '0'); p++; }
        if (g_msg_count < MAX_MSGS) {
            g_msgs[g_msg_count].msgno = msgnum;
            g_msgs[g_msg_count].size = size;
            g_msgs[g_msg_count].from[0] = '?';
            g_msgs[g_msg_count].from[1] = '\0';
            strlcpy(g_msgs[g_msg_count].subject, "(loading)", sizeof(g_msgs[g_msg_count].subject));
            g_msg_count++;
        }
    }

    /* RETR each (up to MAX_MSGS) to parse headers */
    {
        uint32_t idx;
        for (idx = 0; idx < g_msg_count; idx++) {
            char cmd[32];
            uint32_t n = sprintf(cmd, "RETR %u\r\n", g_msgs[idx].msgno);
            uint32_t body_start = 0;
            uint32_t total = 0;
            bool in_header = true;
            char *dst_from = g_msgs[idx].from;
            char *dst_subj = g_msgs[idx].subject;
            char *dst_date = g_msgs[idx].date;
            char *dst_body = g_msgs[idx].body;
            uint32_t body_len = 0;

            monios_socket_tcp_send(s, cmd, (uint16_t)n);
            /* read response line */
            rd_line(&r, line, sizeof(line));
            if (line[0] != '+') continue;

            dst_from[0] = '\0'; dst_subj[0] = '\0'; dst_date[0] = '\0';
            dst_body[0] = '\0';

            for (;;) {
                char hl[256];
                uint32_t hn = rd_line(&r, hl, sizeof(hl));
                if (hn == 0) break;
                if (strcmp(hl, ".") == 0) break;
                if (in_header) {
                    if (hl[0] == '\0') { in_header = false; continue; }
                    if (!prefix_ci(hl, "From:")) {
                        strlcpy(dst_from, hl + 5, 64);
                        /* trim leading space */
                        char *q = dst_from;
                        while (*q == ' ') q++;
                        if (q != dst_from) { uint32_t i = 0; while (q[i] != '\0') { dst_from[i] = q[i]; i++; } dst_from[i] = '\0'; }
                    } else if (!prefix_ci(hl, "Subject:")) {
                        strlcpy(dst_subj, hl + 8, 64);
                        char *q = dst_subj; while (*q == ' ') q++;
                        if (q != dst_subj) { uint32_t i = 0; while (q[i] != '\0') { dst_subj[i] = q[i]; i++; } dst_subj[i] = '\0'; }
                    } else if (!prefix_ci(hl, "Date:")) {
                        strlcpy(dst_date, hl + 5, 32);
                    }
                } else {
                    if (body_len < 250) {
                        uint32_t i = 0;
                        while (hl[i] != '\0' && body_len < 250) {
                            dst_body[body_len++] = hl[i++];
                        }
                        dst_body[body_len++] = '\n';
                    }
                }
                total++;
                (void)body_start;
            }
            dst_body[body_len] = '\0';
        }
    }

    /* QUIT */
    monios_socket_tcp_send(s, "QUIT\r\n", 6);
    monios_socket_close(s);
    {
        char st[40];
        sprintf(st, "Received %u messages", g_msg_count);
        set_status(st);
    }
    g_selected = -1;
}

/* ---------------- SMTP send ---------------- */
static void smtp_send(void)
{
    int s;
    rdr_t r;
    char line[256];
    char b64u[128], b64p[128];
    uint16_t port = parse_port(g_cfg.smtp_port);

    if (g_comp_to[0] == '\0') { set_status("Recipient empty"); return; }

    set_status("Connecting SMTP...");
    s = tcp_connect_to(g_cfg.smtp_host, port);
    if (s < 0) { set_status("SMTP connect failed"); return; }
    r.sock = s; r.len = 0; r.pos = 0;

    rd_line(&r, line, sizeof(line)); /* greeting */
    monios_socket_tcp_send(s, "EHLO monios\r\n", 12);
    /* read EHLO reply (may be multi-line) */
    for (;;) {
        rd_line(&r, line, sizeof(line));
        if (line[0] >= '0' && line[0] <= '9' && line[3] == ' ') break;
        if (line[0] == '\0') break;
    }

    /* AUTH LOGIN */
    monios_socket_tcp_send(s, "AUTH LOGIN\r\n", 11);
    rd_line(&r, line, sizeof(line));
    b64_encode(g_cfg.username, b64u, sizeof(b64u));
    {
        char cmd[160];
        uint32_t n = sprintf(cmd, "%s\r\n", b64u);
        monios_socket_tcp_send(s, cmd, (uint16_t)n);
    }
    rd_line(&r, line, sizeof(line));
    b64_encode(g_cfg.password, b64p, sizeof(b64p));
    {
        char cmd[160];
        uint32_t n = sprintf(cmd, "%s\r\n", b64p);
        monios_socket_tcp_send(s, cmd, (uint16_t)n);
    }
    rd_line(&r, line, sizeof(line));
    if (line[0] != '2') { set_status("SMTP auth failed"); monios_socket_close(s); return; }

    /* MAIL FROM */
    {
        char cmd[160];
        uint32_t n = sprintf(cmd, "MAIL FROM:<%s>\r\n", g_cfg.username);
        monios_socket_tcp_send(s, cmd, (uint16_t)n);
        rd_line(&r, line, sizeof(line));
    }
    /* RCPT TO */
    {
        char cmd[160];
        uint32_t n = sprintf(cmd, "RCPT TO:<%s>\r\n", g_comp_to);
        monios_socket_tcp_send(s, cmd, (uint16_t)n);
        rd_line(&r, line, sizeof(line));
    }
    /* DATA */
    monios_socket_tcp_send(s, "DATA\r\n", 5);
    rd_line(&r, line, sizeof(line));
    {
        char msg[1024];
        uint32_t n = 0;
        n += sprintf(msg + n, "From: %s <%s>\r\n", g_cfg.display_name, g_cfg.username);
        n += sprintf(msg + n, "To: %s\r\n", g_comp_to);
        n += sprintf(msg + n, "Subject: %s\r\n", g_comp_subject);
        n += sprintf(msg + n, "Content-Type: text/plain; charset=utf-8\r\n");
        n += sprintf(msg + n, "\r\n");
        n += sprintf(msg + n, "%s\r\n", g_comp_body);
        n += sprintf(msg + n, ".\r\n");
        monios_socket_tcp_send(s, msg, (uint16_t)n);
    }
    rd_line(&r, line, sizeof(line));
    monios_socket_tcp_send(s, "QUIT\r\n", 6);
    monios_socket_close(s);

    if (line[0] == '2') {
        set_status("Message sent!");
        g_ui = UI_MAIN;
        g_comp_to[0] = g_comp_subject[0] = g_comp_body[0] = '\0';
    } else {
        set_status("Send failed");
    }
}

/* ---------------- POP3 delete ---------------- */
static void pop3_delete(void)
{
    int s;
    rdr_t r;
    char line[256];
    uint16_t port = parse_port(g_cfg.pop3_port);

    if (g_selected < 0 || g_selected >= (int)g_msg_count) return;

    s = tcp_connect_to(g_cfg.pop3_host, port);
    if (s < 0) { set_status("POP3 connect failed"); return; }
    r.sock = s; r.len = 0; r.pos = 0;
    rd_line(&r, line, sizeof(line));
    {
        char cmd[128];
        uint32_t n = sprintf(cmd, "USER %s\r\n", g_cfg.username);
        monios_socket_tcp_send(s, cmd, (uint16_t)n);
        rd_line(&r, line, sizeof(line));
    }
    {
        char cmd[128];
        uint32_t n = sprintf(cmd, "PASS %s\r\n", g_cfg.password);
        monios_socket_tcp_send(s, cmd, (uint16_t)n);
        rd_line(&r, line, sizeof(line));
    }
    {
        char cmd[32];
        uint32_t n = sprintf(cmd, "DELE %u\r\n", g_msgs[g_selected].msgno);
        monios_socket_tcp_send(s, cmd, (uint16_t)n);
        rd_line(&r, line, sizeof(line));
    }
    monios_socket_tcp_send(s, "QUIT\r\n", 6);
    monios_socket_close(s);
    set_status("Message deleted");
    /* remove from local list */
    {
        uint32_t i;
        for (i = (uint32_t)g_selected; i + 1 < g_msg_count; i++) {
            g_msgs[i] = g_msgs[i + 1];
        }
        g_msg_count--;
        g_selected = -1;
    }
}

/* ---------------- rendering ---------------- */
static void draw_button(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       const char *label, bool hover)
{
    app_graphics_fill_rect(x, y, w, h, hover ? 0x003B82F6u : 0x001E40AFu);
    app_graphics_fill_rect(x, y, w, 2, 0x0060A5FAu);
    app_graphics_draw_text((uint16_t)(x + 8u), (uint16_t)(y + 6u), label, 0x00FFFFFFu);
}

static void render_main(void)
{
    uint32_t i;
    app_mouse_snapshot_t mouse;
    app_get_mouse(&mouse);

    app_graphics_fill_rect(0, 0, 1024, 768, 0x00F1F5F9u);

    /* toolbar */
    app_graphics_fill_rect(0, 0, 1024, 40, 0x001E3A8Au);
    g_btns[0] = (button_t){ 8, 6, 90, 28, "收信" };
    g_btns[1] = (button_t){ 106, 6, 90, 28, "写信" };
    g_btns[2] = (button_t){ 204, 6, 90, 28, "删除" };
    g_btns[3] = (button_t){ 302, 6, 90, 28, "设置" };
    g_btn_count = 4;
    for (i = 0; i < g_btn_count; i++) {
        bool hov = mouse.x_pixels >= g_btns[i].x && mouse.x_pixels < g_btns[i].x + g_btns[i].w &&
                   mouse.y_pixels >= g_btns[i].y && mouse.y_pixels < g_btns[i].y + g_btns[i].h;
        draw_button(g_btns[i].x, g_btns[i].y, g_btns[i].w, g_btns[i].h, g_btns[i].label, hov);
    }

    /* left folder list */
    app_graphics_fill_rect(0, 40, 160, 700, 0x00E2E8F0u);
    app_graphics_draw_text(12, 52, "文件夹", 0x00334155u);
    app_graphics_draw_text(12, 80, "收件箱", 0x001E40AFu);
    app_graphics_draw_text(12, 104, "发件箱", 0x0064748Bu);
    app_graphics_draw_text(12, 128, "已发送", 0x0064748Bu);
    app_graphics_draw_text(12, 152, "草稿", 0x0064748Bu);

    /* right-top: message list */
    app_graphics_fill_rect(160, 40, 864, 320, 0x00FFFFFFu);
    app_graphics_draw_text(170, 46, "发件人", 0x0094A3B8u);
    app_graphics_draw_text(350, 46, "主题", 0x0094A3B8u);
    app_graphics_draw_text(700, 46, "日期", 0x0094A3B8u);
    for (i = 0; i < g_msg_count && i < 20; i++) {
        uint16_t y = (uint16_t)(70u + i * 14u);
        if ((int)i == g_selected) {
            app_graphics_fill_rect(162, (uint16_t)(y - 2u), 860, 14, 0x00BFDBFEu);
        }
        app_graphics_draw_text(170, y, g_msgs[i].from, 0x00111827u);
        app_graphics_draw_text(350, y, g_msgs[i].subject, 0x00111827u);
        app_graphics_draw_text(700, y, g_msgs[i].date, 0x00475569u);
    }

    /* right-bottom: preview */
    app_graphics_fill_rect(160, 360, 864, 350, 0x00FFFFFFu);
    if (g_selected >= 0 && g_selected < (int)g_msg_count) {
        char hdr[128];
        sprintf(hdr, "From: %s", g_msgs[g_selected].from);
        app_graphics_draw_text(170, 368, hdr, 0x001E40AFu);
        sprintf(hdr, "Subject: %s", g_msgs[g_selected].subject);
        app_graphics_draw_text(170, 386, hdr, 0x001E40AFu);
        {
            /* wrap body into ~60-char lines */
            const char *p = g_msgs[g_selected].body;
            uint32_t line_no = 0;
            char wl[64];
            while (*p != '\0' && line_no < 18) {
                uint32_t c = 0;
                while (*p != '\0' && *p != '\n' && c + 1 < sizeof(wl)) {
                    wl[c++] = *p++;
                }
                if (*p == '\n') p++;
                wl[c] = '\0';
                app_graphics_draw_text(170, (uint16_t)(410u + line_no * 16u), wl, 0x00111827u);
                line_no++;
            }
        }
    } else {
        app_graphics_draw_text(400, 500, "(no message selected)", 0x0094A3B8u);
    }

    /* status bar */
    app_graphics_fill_rect(0, 720, 1024, 48, 0x001E3A8Au);
    app_graphics_draw_text(8, 730, g_status, 0x00DBEAFEu);
    app_graphics_draw_text(8, 748, "Esc: quit", 0x0093C5FDu);
    app_graphics_present();
}

static void draw_field(uint16_t y, const char *label, const char *value, int focus)
{
    app_graphics_draw_text(120, y, label, 0x001E293Bu);
    app_graphics_fill_rect(220, (uint16_t)(y - 2u), 600, 22, focus ? 0x00FFF7CCu : 0x00FFFFFFu);
    app_graphics_draw_text(226, y, value, 0x00111827u);
}

static void render_compose(void)
{
    app_graphics_fill_rect(0, 0, 1024, 768, 0x00F1F5F9u);
    app_graphics_draw_text(400, 20, "New Message", 0x001E3A8Au);

    draw_field(70, "To:", g_comp_to, g_comp_focus == 0);
    draw_field(100, "Subject:", g_comp_subject, g_comp_focus == 1);

    app_graphics_draw_text(120, 140, "Body:", 0x001E293Bu);
    app_graphics_fill_rect(220, 138, 600, 300, g_comp_focus == 2 ? 0x00FFF7CCu : 0x00FFFFFFu);
    {
        /* wrap body */
        const char *p = g_comp_body;
        uint32_t line_no = 0;
        char wl[80];
        while (*p != '\0' && line_no < 16) {
            uint32_t c = 0;
            while (*p != '\0' && c + 1 < sizeof(wl)) wl[c++] = *p++;
            wl[c] = '\0';
            app_graphics_draw_text(226, (uint16_t)(150u + line_no * 16u), wl, 0x00111827u);
            line_no++;
        }
    }

    /* buttons */
    app_graphics_fill_rect(220, 460, 100, 30, 0x0016A34Au);
    app_graphics_draw_text(240, 468, "Send", 0x00FFFFFFu);
    app_graphics_fill_rect(340, 460, 100, 30, 0x0064748Bu);
    app_graphics_draw_text(350, 468, "Cancel", 0x00FFFFFFu);

    app_graphics_draw_text(120, 520, "Tab: next field  Enter: send  Esc: cancel", 0x0064748Bu);
    app_graphics_present();
}

static void render_settings(void)
{
    app_graphics_fill_rect(0, 0, 1024, 768, 0x00F1F5F9u);
    app_graphics_draw_text(380, 20, "Account Settings", 0x001E3A8Au);

    draw_field(70, "SMTP:", g_cfg.smtp_host, g_set_focus == 0);
    draw_field(100, "Port:", g_cfg.smtp_port, g_set_focus == 1);
    draw_field(130, "POP3:", g_cfg.pop3_host, g_set_focus == 2);
    draw_field(160, "Port:", g_cfg.pop3_port, g_set_focus == 3);
    draw_field(190, "User:", g_cfg.username, g_set_focus == 4);
    draw_field(220, "Pass:", g_cfg.password, g_set_focus == 5);
    draw_field(250, "Name:", g_cfg.display_name, g_set_focus == 6);

    app_graphics_fill_rect(220, 290, 100, 30, 0x0016A34Au);
    app_graphics_draw_text(240, 298, "Save", 0x00FFFFFFu);
    app_graphics_fill_rect(340, 290, 100, 30, 0x0064748Bu);
    app_graphics_draw_text(350, 298, "Cancel", 0x00FFFFFFu);

    app_graphics_draw_text(120, 350, "Tab: next field  Enter: save  Esc: cancel", 0x0064748Bu);
    app_graphics_present();
}

/* ---------------- text field editing ---------------- */
static void field_append(char *field, uint32_t fsz, char ch)
{
    uint32_t l = (uint32_t)strlen(field);
    if (l + 1 < fsz && ch >= 32 && ch < 127) {
        field[l] = ch;
        field[l + 1] = '\0';
    }
}

static void field_backspace(char *field)
{
    uint32_t l = (uint32_t)strlen(field);
    if (l > 0) field[l - 1] = '\0';
}

static void edit_compose(char ch)
{
    char *f;
    uint32_t sz;
    if (g_comp_focus == 0) { f = g_comp_to; sz = sizeof(g_comp_to); }
    else if (g_comp_focus == 1) { f = g_comp_subject; sz = sizeof(g_comp_subject); }
    else { f = g_comp_body; sz = sizeof(g_comp_body); }

    if (ch == 8) field_backspace(f);
    else field_append(f, sz, ch);
}

static void edit_settings(char ch)
{
    char *f;
    uint32_t sz;
    switch (g_set_focus) {
    case 0: f = g_cfg.smtp_host; sz = sizeof(g_cfg.smtp_host); break;
    case 1: f = g_cfg.smtp_port; sz = sizeof(g_cfg.smtp_port); break;
    case 2: f = g_cfg.pop3_host; sz = sizeof(g_cfg.pop3_host); break;
    case 3: f = g_cfg.pop3_port; sz = sizeof(g_cfg.pop3_port); break;
    case 4: f = g_cfg.username; sz = sizeof(g_cfg.username); break;
    case 5: f = g_cfg.password; sz = sizeof(g_cfg.password); break;
    default: f = g_cfg.display_name; sz = sizeof(g_cfg.display_name); break;
    }
    if (ch == 8) field_backspace(f);
    else field_append(f, sz, ch);
}

/* ---------------- main ---------------- */
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    app_enter_graphics_mode();
    cfg_load();
    set_status("Ready. Click 收信 to fetch mail.");
    g_ui = UI_MAIN;

    for (;;) {
        app_key_event_t ev;
        app_mouse_snapshot_t mouse;
        static uint8_t prev_buttons = 0;

        while (syscall1(SYS_KEYBOARD_READ_EVENT, (uint64_t)&ev) == 1) {
            if (ev.type == EV_ESC) {
                if (g_ui == UI_COMPOSE || g_ui == UI_SETTINGS) g_ui = UI_MAIN;
                else app_exit(0);
                continue;
            }
            if (g_ui == UI_MAIN) {
                if (ev.type == EV_CHAR && ev.ch == 'r') pop3_fetch();
            } else if (g_ui == UI_COMPOSE) {
                if (ev.type == EV_TAB) {
                    g_comp_focus = (g_comp_focus + 1) % 3;
                } else if (ev.type == EV_ENTER) {
                    smtp_send();
                } else if (ev.type == EV_CHAR) {
                    edit_compose(ev.ch);
                }
            } else if (g_ui == UI_SETTINGS) {
                if (ev.type == EV_TAB) {
                    g_set_focus = (g_set_focus + 1) % 7;
                } else if (ev.type == EV_ENTER) {
                    cfg_save();
                    set_status("Settings saved");
                    g_ui = UI_MAIN;
                } else if (ev.type == EV_CHAR) {
                    edit_settings(ev.ch);
                }
            }
        }

        app_get_mouse(&mouse);

        if ((mouse.buttons & 0x01u) && !(prev_buttons & 0x01u)) {
            if (g_ui == UI_MAIN) {
                uint8_t b;
                for (b = 0; b < g_btn_count; b++) {
                    if (mouse.x_pixels >= g_btns[b].x &&
                        mouse.x_pixels < g_btns[b].x + g_btns[b].w &&
                        mouse.y_pixels >= g_btns[b].y &&
                        mouse.y_pixels < g_btns[b].y + g_btns[b].h) {
                        if (b == 0) pop3_fetch();
                        else if (b == 1) {
                            g_ui = UI_COMPOSE;
                            g_comp_focus = 0;
                        }
                        else if (b == 2) pop3_delete();
                        else if (b == 3) {
                            g_ui = UI_SETTINGS;
                            g_set_focus = 0;
                        }
                    }
                }
                /* click a message in list */
                if (mouse.y_pixels >= 70 && mouse.y_pixels < 350 && mouse.x_pixels > 160) {
                    int row = (int)((mouse.y_pixels - 70) / 14);
                    if (row >= 0 && row < (int)g_msg_count) g_selected = row;
                }
            } else if (g_ui == UI_COMPOSE) {
                /* Send button */
                if (mouse.x_pixels >= 220 && mouse.x_pixels < 320 &&
                    mouse.y_pixels >= 460 && mouse.y_pixels < 490) {
                    smtp_send();
                }
                /* Cancel */
                if (mouse.x_pixels >= 340 && mouse.x_pixels < 440 &&
                    mouse.y_pixels >= 460 && mouse.y_pixels < 490) {
                    g_ui = UI_MAIN;
                }
                /* focus field by y */
                if (mouse.y_pixels >= 68 && mouse.y_pixels < 90) g_comp_focus = 0;
                else if (mouse.y_pixels >= 98 && mouse.y_pixels < 120) g_comp_focus = 1;
                else if (mouse.y_pixels >= 138 && mouse.y_pixels < 440) g_comp_focus = 2;
            } else if (g_ui == UI_SETTINGS) {
                if (mouse.x_pixels >= 220 && mouse.x_pixels < 320 &&
                    mouse.y_pixels >= 290 && mouse.y_pixels < 320) {
                    cfg_save();
                    set_status("Settings saved");
                    g_ui = UI_MAIN;
                }
                if (mouse.x_pixels >= 340 && mouse.x_pixels < 440 &&
                    mouse.y_pixels >= 290 && mouse.y_pixels < 320) {
                    g_ui = UI_MAIN;
                }
                /* focus field by y */
                if (mouse.y_pixels >= 68 && mouse.y_pixels < 90) g_set_focus = 0;
                else if (mouse.y_pixels >= 98 && mouse.y_pixels < 120) g_set_focus = 1;
                else if (mouse.y_pixels >= 128 && mouse.y_pixels < 150) g_set_focus = 2;
                else if (mouse.y_pixels >= 158 && mouse.y_pixels < 180) g_set_focus = 3;
                else if (mouse.y_pixels >= 188 && mouse.y_pixels < 210) g_set_focus = 4;
                else if (mouse.y_pixels >= 218 && mouse.y_pixels < 240) g_set_focus = 5;
                else if (mouse.y_pixels >= 248 && mouse.y_pixels < 270) g_set_focus = 6;
            }
        }
        prev_buttons = mouse.buttons;

        if (g_ui == UI_MAIN) render_main();
        else if (g_ui == UI_COMPOSE) render_compose();
        else render_settings();
        app_sleep_ticks(2);
    }
    return 0;
}
