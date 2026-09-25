/*
 * ftp.c — MoniOS graphical FTP client (1024x768 GUI).
 *
 * Layout:
 *   top     connection bar (host / port / user / pass / connect button)
 *   toolbar upload / download / delete / refresh / mkdir / up-dir
 *   middle  left = local file list, right = remote (FTP) file list
 *   bottom  transfer progress bar + scrolling command/response log
 *
 * Protocol core (control channel on port 21, transient PASV data channel)
 * is preserved from the original CLI client: USER/PASS/PWD/CWD/LIST/RETR/
 * STOR/DELE/MKD/RMD/REST/QUIT.  Default mode is PASV; PORT (active) mode
 * scaffolding is kept but PASV is used for data transfers.
 *
 * Build: see report.  Enter graphics mode on launch; Esc quits.
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "monios_dll.h"
#include "string.h"
#include "stdint.h"
#include "stdbool.h"
#include "syscall.h"

/* ---------------- geometry / palette (1024x768) ---------------- */
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

#define FTP_RING    1024u
#define FTP_MAXFILE (256u * 1024u)
#define MAX_ENT     64
#define LOG_LINES   9

/* ---------------- transfer buffer ---------------- */
static uint8_t g_filebuf[FTP_MAXFILE];

/* ---------------- protocol state ---------------- */
static int      g_ctrl = -1;
static char     g_host[64];

/* ---------------- log ring (drawn, not stdout) ---------------- */
static char  g_log[LOG_LINES][128];
static uint32_t g_log_n;

static void log_line(const char *s)
{
    uint32_t i = 0;
    uint32_t n = (uint32_t) strlen(s);
    if (n > 126u) n = 126u;
    /* shift up */
    for (i = 1; i < LOG_LINES; i++) {
        strlcpy(g_log[i - 1], g_log[i], sizeof(g_log[i]));
    }
    memset(g_log[LOG_LINES - 1], 0, sizeof(g_log[LOG_LINES - 1]));
    for (i = 0; i < n; i++) g_log[LOG_LINES - 1][i] = s[i];
    g_log[LOG_LINES - 1][n] = '\0';
    if (g_log_n < LOG_LINES) g_log_n++;
}

/* small integer -> decimal string, returns length */
static uint32_t u2str(uint32_t v, char *out)
{
    char tmp[12];
    uint32_t n = 0;
    uint32_t i = 0;
    if (v == 0) { out[0] = '0'; out[1] = '\0'; return 1; }
    while (v > 0) { tmp[n++] = (char) ('0' + (v % 10u)); v /= 10u; }
    for (i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = '\0';
    return n;
}

static void log_u32(const char *pre, uint32_t v)
{
    char buf[140];
    char num[12];
    uint32_t i = 0, j = 0;
    while (pre[i] != '\0' && i + 1 < sizeof(buf)) { buf[i] = pre[i]; i++; }
    u2str(v, num);
    while (num[j] != '\0' && i + 1 < sizeof(buf)) buf[i++] = num[j++];
    buf[i] = '\0';
    log_line(buf);
}

/* ---------------- socket ring reader ---------------- */
typedef struct {
    int sock;
    uint8_t buf[FTP_RING];
    uint32_t len, pos;
} rdr_t;

static int rd_refill(rdr_t *r)
{
    int n;
    uint32_t spins = 0;
    if (r->pos < r->len) return 1;
    r->len = 0; r->pos = 0;
    while (!monios_socket_tcp_has_data(r->sock)) {
        if (!monios_socket_tcp_connected(r->sock)) {
            if (spins > 300) return 0;
        }
        app_sleep_ticks(1);
        if (++spins > 40000) return 0;
    }
    n = monios_socket_tcp_recv(r->sock, r->buf, sizeof(r->buf));
    if (n <= 0) return 0;
    r->len = (uint32_t) n;
    r->pos = 0;
    return 1;
}

static int rd_byte(rdr_t *r, char *c)
{
    if (r->pos >= r->len) {
        if (!rd_refill(r)) return 0;
    }
    *c = (char) r->buf[r->pos++];
    return 1;
}

static uint32_t rd_line(rdr_t *r, char *out, uint32_t out_sz)
{
    uint32_t i = 0;
    char c;
    while (i + 1 < out_sz) {
        if (!rd_byte(r, &c)) { out[i] = '\0'; return i; }
        if (c == '\n') break;
        out[i++] = c;
    }
    out[i] = '\0';
    if (i > 0 && out[i - 1] == '\r') { out[i - 1] = '\0'; i--; }
    return i;
}

/* ---------------- connect ---------------- */
static int ftp_tcp(const char *host, uint16_t port)
{
    int h = monios_socket_tcp_open(0);
    uint32_t w = 0;
    if (h < 0) return -1;
    if (monios_socket_tcp_connect(h, host, port) != 0) {
        monios_socket_close(h);
        return -1;
    }
    while (!monios_socket_tcp_connected(h) && w < 4000) {
        app_sleep_ticks(1);
        w++;
    }
    if (!monios_socket_tcp_connected(h)) {
        monios_socket_close(h);
        return -1;
    }
    return h;
}

/* ---------------- control channel ---------------- */
static rdr_t g_ctrlrd;

static void ftp_send(const char *cmd, const char *arg)
{
    char line[300];
    uint32_t n = 0;
    uint32_t i = 0;
    while (cmd[i] != '\0' && n + 1 < sizeof(line)) line[n++] = cmd[i++];
    if (arg != 0 && arg[0] != '\0') {
        line[n++] = ' ';
        i = 0;
        while (arg[i] != '\0' && n + 1 < sizeof(line)) line[n++] = arg[i++];
    }
    line[n++] = '\r';
    line[n++] = '\n';
    monios_socket_tcp_send(g_ctrl, line, (uint16_t) n);
    log_line(line);
}

/* read a response; return the 3-digit code; text out optional */
static int ftp_response(char *out, uint32_t out_sz)
{
    char line[256];
    int code = 0;
    bool started = false;

    out[0] = '\0';
    for (;;) {
        uint32_t n = rd_line(&g_ctrlrd, line, sizeof(line));
        if (n == 0 && !started) return -1;
        if (n == 0) break;
        if (!started) {
            if (line[0] >= '0' && line[0] <= '9' &&
                line[1] >= '0' && line[1] <= '9' &&
                line[2] >= '0' && line[2] <= '9') {
                code = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
                started = true;
            }
        }
        log_line(line);
        if (started && line[3] == ' ') break;
    }
    if (out_sz > 0) out[0] = '\0';
    return code;
}

/* ---------------- PASV: parse 227 (h1,h2,h3,h4,p1,p2) ---------------- */
static uint16_t ftp_pasv(void)
{
    char line[256];
    uint16_t port = 0;
    ftp_send("PASV", 0);
    if (ftp_response(line, sizeof(line)) != 227) return 0;
    {
        char *p = line;
        int vals[6];
        int i = 0;
        while (*p != '\0' && i < 6) {
            if (*p >= '0' && *p <= '9') {
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                vals[i++] = v;
            } else p++;
        }
        if (i == 6) port = (uint16_t) (vals[4] * 256 + vals[5]);
    }
    return port;
}

/* ---------------- directory entry model ---------------- */
typedef struct {
    char     name[96];
    uint32_t size;
    uint8_t  is_dir;
} ent_t;

static ent_t g_lent[MAX_ENT];
static int   g_lent_n;
static int   g_lsel;
static char  g_lcwd[256];

static ent_t g_rent[MAX_ENT];
static int   g_rent_n;
static int   g_rsel;
static char  g_rcwd[128];   /* remote current dir, e.g. /pub  */

/* ---------------- progress ---------------- */
static uint8_t  g_busy;        /* transfer in progress */
static uint32_t g_prog_cur;
static uint32_t g_prog_total;
static char     g_prog_label[64];

/* ---------------- UI input fields ---------------- */
#define FIELD_HOST 0
#define FIELD_PORT 1
#define FIELD_USER 2
#define FIELD_PASS 3
static int   g_focus = -1;
static char  f_host[64];
static char  f_port[16];
static char  f_user[64];
static char  f_pass[64];

/* ================= local directory ================= */
static void local_refresh(void)
{
    char buf[2048];
    uint32_t i = 0;
    g_lent_n = 0;
    g_lsel = 0;
    if (app_file_list_dir(g_lcwd, buf, sizeof(buf)) < 0) {
        log_line("local: cannot list dir");
        return;
    }
    while (buf[i] != '\0' && g_lent_n < MAX_ENT) {
        uint32_t start = i;
        char name[96];
        uint32_t len = 0;
        uint32_t k = 0;
        char full[280];
        while (buf[i] != '\0' && buf[i] != '\n') i++;
        len = i - start;
        if (len == 0) { if (buf[i] == '\n') i++; continue; }
        if (len >= sizeof(name)) len = sizeof(name) - 1u;
        for (k = 0; k < len; k++) name[k] = buf[start + k];
        name[k] = '\0';
        {
            ent_t *e = &g_lent[g_lent_n];
            uint32_t p = 0;
            memset(e, 0, sizeof(*e));
            if (name[len - 1u] == '/') { e->is_dir = 1; name[len - 1u] = '\0'; }
            while (name[p] != '\0' && p + 1 < sizeof(e->name)) {
                e->name[p] = name[p]; p++;
            }
            e->name[p] = '\0';
            /* build full path for size query */
            p = 0;
            while (g_lcwd[p] != '\0' && p + 1 < sizeof(full) - 1u) {
                full[p] = g_lcwd[p]; p++;
            }
            if (p > 0 && full[p - 1] != '\\' && full[p - 1] != '/') {
                full[p++] = '\\';
            }
            k = 0;
            while (e->name[k] != '\0' && p + 1 < sizeof(full)) full[p++] = e->name[k++];
            full[p] = '\0';
            if (!e->is_dir) e->size = (uint32_t) app_file_size(full);
            g_lent_n++;
        }
        if (buf[i] == '\n') i++;
    }
}

/* ================= remote LIST parsing (Unix ls -l) ================= */
static void remote_parse_list(const char *data)
{
    uint32_t i = 0;
    g_rent_n = 0;
    g_rsel = 0;
    while (data[i] != '\0' && g_rent_n < MAX_ENT) {
        uint32_t start = i;
        char line[160];
        uint32_t len = 0;
        uint32_t k = 0;
        while (data[i] != '\0' && data[i] != '\n') i++;
        len = i - start;
        if (len == 0) { if (data[i] == '\n') i++; continue; }
        if (len >= sizeof(line)) len = sizeof(line) - 1u;
        for (k = 0; k < len; k++) line[k] = data[start + k];
        line[k] = '\0';

        {
            ent_t *e = &g_rent[g_rent_n];
            char *p = line;
            int tok = 0;
            char *tokstart[12];
            char *namepos = 0;
            memset(e, 0, sizeof(*e));
            if (line[0] == 'd') e->is_dir = 1;
            if (line[0] != '-' && line[0] != 'd') {
                if (data[i] == '\n') i++;
                continue; /* skip total / other lines */
            }
            /* tokenize by runs of whitespace */
            while (*p != '\0' && tok < 12) {
                while (*p == ' ' || *p == '\t') p++;
                if (*p == '\0') break;
                tokstart[tok++] = p;
                while (*p != '\0' && *p != ' ' && *p != '\t') p++;
                if (*p != '\0') { *p = '\0'; p++; }
            }
            /* tokens: 0 perms 1 links 2 owner 3 group 4 size 5 Mon 6 DD 7 HH:MM 8 name... */
            if (tok >= 5) {
                char *sp = tokstart[4];
                uint32_t sz = 0;
                while (*sp >= '0' && *sp <= '9') { sz = sz * 10u + (uint32_t) (*sp - '0'); sp++; }
                e->size = sz;
            }
            if (tok >= 9) namepos = tokstart[8];
            else if (tok >= 5) namepos = tokstart[4];
            if (namepos != 0) {
                uint32_t q = 0;
                while (namepos[q] != '\0' && q + 1 < sizeof(e->name)) {
                    e->name[q] = namepos[q]; q++;
                }
                e->name[q] = '\0';
            }
            if (e->name[0] != '\0') g_rent_n++;
        }
        if (data[i] == '\n') i++;
    }
}

/* fetch remote LIST into a scratch buffer and parse */
static char g_listbuf[8192];
static void remote_list(void)
{
    int data;
    uint16_t dport;
    rdr_t drd;
    uint32_t used = 0;
    char ch;

    if (g_ctrl < 0) { log_line("remote: not connected"); return; }
    dport = ftp_pasv();
    if (dport == 0) { log_line("remote: pasv failed"); return; }
    data = ftp_tcp(g_host, dport);
    if (data < 0) { log_line("remote: data connect failed"); return; }
    ftp_send("LIST", 0);
    ftp_response(g_listbuf, sizeof(g_listbuf)); /* 150 */

    drd.sock = data; drd.len = 0; drd.pos = 0;
    memset(g_listbuf, 0, sizeof(g_listbuf));
    while (rd_byte(&drd, &ch) && used < sizeof(g_listbuf) - 1u) {
        g_listbuf[used++] = (uint8_t) ch;
    }
    g_listbuf[used] = '\0';
    monios_socket_close(data);
    ftp_response(g_listbuf, sizeof(g_listbuf)); /* 226 */
    remote_parse_list(g_listbuf);
    log_u32("remote entries: ", (uint32_t) g_rent_n);
}

/* ================= connect / disconnect ================= */
static bool do_connect(void)
{
    uint16_t port = 21;
    char pbuf[16];
    uint32_t k = 0;
    uint32_t v = 0;

    if (f_host[0] == '\0') { log_line("host empty"); return false; }
    while (f_port[k] >= '0' && f_port[k] <= '9') { v = v * 10u + (uint32_t) (f_port[k] - '0'); k++; }
    if (v > 0 && v < 65536u) port = (uint16_t) v;
    strlcpy(pbuf, f_port, sizeof(pbuf));

    g_busy = 1; g_prog_cur = 0; g_prog_total = 0;
    strlcpy(g_prog_label, "connecting...", sizeof(g_prog_label));
    strlcpy(g_host, f_host, sizeof(g_host));

    g_ctrl = ftp_tcp(f_host, port);
    if (g_ctrl < 0) { log_line("connect failed"); g_busy = 0; return false; }
    g_ctrlrd.sock = g_ctrl;
    g_ctrlrd.len = 0; g_ctrlrd.pos = 0;
    log_line("connected, waiting greeting");
    if (ftp_response(g_listbuf, sizeof(g_listbuf)) < 0) {
        log_line("no greeting");
        monios_socket_close(g_ctrl); g_ctrl = -1; g_busy = 0;
        return false;
    }
    ftp_send("USER", f_user[0] ? f_user : "anonymous");
    {
        int code = ftp_response(g_listbuf, sizeof(g_listbuf));
        if (code == 331) {
            ftp_send("PASS", f_pass[0] ? f_pass : "user@monios");
            ftp_response(g_listbuf, sizeof(g_listbuf));
        }
    }
    strlcpy(g_rcwd, "/", sizeof(g_rcwd));
    remote_list();
    g_busy = 0;
    g_prog_label[0] = '\0';
    return true;
}

static void do_disconnect(void)
{
    if (g_ctrl >= 0) {
        ftp_send("QUIT", 0);
        ftp_response(g_listbuf, sizeof(g_listbuf));
        monios_socket_close(g_ctrl);
        g_ctrl = -1;
    }
    g_rent_n = 0;
    log_line("disconnected");
}

/* ================= path helpers ================= */
static void remote_join(char *out, uint32_t out_sz, const char *name)
{
    uint32_t p = 0;
    while (g_rcwd[p] != '\0' && p + 1 < out_sz) { out[p] = g_rcwd[p]; p++; }
    if (p > 0 && out[p - 1] != '/') out[p++] = '/';
    {
        uint32_t k = 0;
        while (name[k] != '\0' && p + 1 < out_sz) out[p++] = name[k++];
    }
    out[p] = '\0';
}

/* ================= upload (STOR) ================= */
static void do_upload(void)
{
    char full[280];
    uint32_t p = 0, k = 0;
    int sz;
    uint32_t off = 0, left;

    if (g_ctrl < 0) { log_line("not connected"); return; }
    if (g_lsel < 0 || g_lsel >= g_lent_n) { log_line("select a local file"); return; }
    if (g_lent[g_lsel].is_dir) { log_line("cannot upload a dir"); return; }

    while (g_lcwd[p] != '\0' && p + 1 < sizeof(full) - 1u) { full[p] = g_lcwd[p]; p++; }
    if (p > 0 && full[p - 1] != '\\' && full[p - 1] != '/') full[p++] = '\\';
    k = 0;
    while (g_lent[g_lsel].name[k] != '\0' && p + 1 < sizeof(full)) full[p++] = g_lent[g_lsel].name[k++];
    full[p] = '\0';

    sz = app_file_size(full);
    if (sz <= 0) { log_line("local file not found"); return; }
    if (sz > (int) FTP_MAXFILE) sz = (int) FTP_MAXFILE;
    if (app_file_read(full, g_filebuf, (uint32_t) sz) != sz) { log_line("local read failed"); return; }

    ftp_send("TYPE I", 0);
    ftp_response(g_listbuf, sizeof(g_listbuf));
    {
        uint16_t dport = ftp_pasv();
        int data;
        if (dport == 0) { log_line("pasv failed"); return; }
        data = ftp_tcp(g_host, dport);
        if (data < 0) { log_line("data connect failed"); return; }
        ftp_send("STOR", g_lent[g_lsel].name);
        ftp_response(g_listbuf, sizeof(g_listbuf)); /* 150 */

        g_busy = 1;
        g_prog_cur = 0; g_prog_total = (uint32_t) sz;
        strlcpy(g_prog_label, "uploading", sizeof(g_prog_label));
        left = (uint32_t) sz;
        while (left > 0) {
            uint16_t chunk = left > 1024u ? 1024u : (uint16_t) left;
            int s = monios_socket_tcp_send(data, g_filebuf + off, chunk);
            if (s <= 0) break;
            off += (uint32_t) s; left -= (uint32_t) s;
            g_prog_cur = off;
        }
        monios_socket_close(data);
        ftp_response(g_listbuf, sizeof(g_listbuf)); /* 226 */
        g_busy = 0;
        g_prog_label[0] = '\0';
        log_line("upload complete");
    }
    remote_list();
}

/* ================= download (RETR) + REST resume ================= */
static void do_download(void)
{
    char full[280];
    char rempath[160];
    uint32_t p = 0, k = 0;
    uint16_t dport;
    int data;
    uint32_t existing = 0;
    uint32_t off = 0;
    rdr_t drd;
    char ch;

    if (g_ctrl < 0) { log_line("not connected"); return; }
    if (g_rsel < 0 || g_rsel >= g_rent_n) { log_line("select a remote file"); return; }
    if (g_rent[g_rsel].is_dir) { log_line("cannot download a dir"); return; }

    remote_join(rempath, sizeof(rempath), g_rent[g_rsel].name);

    /* local destination = cwd + basename */
    while (g_lcwd[p] != '\0' && p + 1 < sizeof(full) - 1u) { full[p] = g_lcwd[p]; p++; }
    if (p > 0 && full[p - 1] != '\\' && full[p - 1] != '/') full[p++] = '\\';
    k = 0;
    while (g_rent[g_rsel].name[k] != '\0' && p + 1 < sizeof(full)) full[p++] = g_rent[g_rsel].name[k++];
    full[p] = '\0';

    /* resume: if local file exists, REST from its size */
    if (app_file_exists(full)) {
        existing = (uint32_t) app_file_size(full);
        if (existing > FTP_MAXFILE) existing = FTP_MAXFILE;
        if (app_file_read(full, g_filebuf, existing) != (int32_t) existing) existing = 0;
    }

    ftp_send("TYPE I", 0);
    ftp_response(g_listbuf, sizeof(g_listbuf));
    dport = ftp_pasv();
    if (dport == 0) { log_line("pasv failed"); return; }
    data = ftp_tcp(g_host, dport);
    if (data < 0) { log_line("data connect failed"); return; }

    if (existing > 0) {
        char offstr[16];
        u2str(existing, offstr);
        ftp_send("REST", offstr);
        ftp_response(g_listbuf, sizeof(g_listbuf));
        log_u32("resume from offset ", existing);
    }
    ftp_send("RETR", rempath);
    ftp_response(g_listbuf, sizeof(g_listbuf)); /* 150 */

    g_busy = 1;
    g_prog_cur = existing; g_prog_total = 0;
    strlcpy(g_prog_label, "downloading", sizeof(g_prog_label));

    drd.sock = data; drd.len = 0; drd.pos = 0;
    off = existing;
    while (rd_byte(&drd, &ch)) {
        if (off < FTP_MAXFILE) g_filebuf[off++] = (uint8_t) ch;
        g_prog_cur = off;
    }
    monios_socket_close(data);
    ftp_response(g_listbuf, sizeof(g_listbuf)); /* 226 */

    app_file_write(full, g_filebuf, off);
    g_busy = 0;
    g_prog_label[0] = '\0';
    log_u32("downloaded bytes: ", off);
    local_refresh();
}

/* ================= remote ops ================= */
static void do_delete_remote(void)
{
    if (g_rsel < 0 || g_rsel >= g_rent_n) { log_line("select remote entry"); return; }
    ftp_send(g_rent[g_rsel].is_dir ? "RMD" : "DELE", g_rent[g_rsel].name);
    ftp_response(g_listbuf, sizeof(g_listbuf));
    remote_list();
}

static void do_mkdir_remote(void)
{
    ftp_send("MKD", "newdir");
    ftp_response(g_listbuf, sizeof(g_listbuf));
    remote_list();
}

static void do_remote_enter(void)
{
    char newpath[160];
    if (g_rsel < 0 || g_rsel >= g_rent_n) return;
    if (!g_rent[g_rsel].is_dir) return;
    remote_join(newpath, sizeof(newpath), g_rent[g_rsel].name);
    ftp_send("CWD", newpath);
    if (ftp_response(g_listbuf, sizeof(g_listbuf)) >= 0) {
        strlcpy(g_rcwd, newpath, sizeof(g_rcwd));
        remote_list();
    }
}

static void do_remote_up(void)
{
    char *last = 0;
    uint32_t k = 0;
    if (strcmp(g_rcwd, "/") == 0) return;
    while (g_rcwd[k] != '\0') {
        if (g_rcwd[k] == '/') last = (char *) &g_rcwd[k];
        k++;
    }
    if (last == g_rcwd || last == 0) {
        strlcpy(g_rcwd, "/", sizeof(g_rcwd));
    } else {
        *last = '\0';
        if (g_rcwd[0] == '\0') strlcpy(g_rcwd, "/", sizeof(g_rcwd));
    }
    ftp_send("CWD", g_rcwd);
    ftp_response(g_listbuf, sizeof(g_listbuf));
    remote_list();
}

static void do_local_enter(void)
{
    char newpath[280];
    uint32_t p = 0;
    if (g_lsel < 0 || g_lsel >= g_lent_n) return;
    if (!g_lent[g_lsel].is_dir) return;
    while (g_lcwd[p] != '\0' && p + 1 < sizeof(newpath) - 1u) { newpath[p] = g_lcwd[p]; p++; }
    if (p > 0 && newpath[p - 1] != '\\' && newpath[p - 1] != '/') newpath[p++] = '\\';
    {
        uint32_t k = 0;
        while (g_lent[g_lsel].name[k] != '\0' && p + 1 < sizeof(newpath)) newpath[p++] = g_lent[g_lsel].name[k++];
    }
    newpath[p] = '\0';
    strlcpy(g_lcwd, newpath, sizeof(g_lcwd));
    local_refresh();
}

static void do_local_up(void)
{
    char *last = 0;
    uint32_t k = 0;
    while (g_lcwd[k] != '\0') {
        if (g_lcwd[k] == '\\' || g_lcwd[k] == '/') last = (char *) &g_lcwd[k];
        k++;
    }
    if (last == 0 || last == g_lcwd) return;
    *last = '\0';
    if (g_lcwd[0] == '\0') strlcpy(g_lcwd, "C:\\", sizeof(g_lcwd));
    local_refresh();
}

/* ================= drawing ================= */
static void rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t c)
{
    app_graphics_fill_rect(x, y, w, h, c);
}

static void text_at(uint16_t x, uint16_t y, const char *s, uint32_t c)
{
    app_graphics_draw_text(x, y, s, c);
}

/* button; returns true on this frame's click if hit. */
typedef struct { uint16_t x, y, w, h; } rct;

static bool rct_hit(rct r, int mx, int my)
{
    return mx >= (int) r.x && mx < (int) (r.x + r.w) &&
           my >= (int) r.y && my < (int) (r.y + r.h);
}

static void draw_button(rct r, const char *label, uint32_t state_color)
{
    rect(r.x, r.y, r.w, r.h, state_color);
    rect(r.x, r.y, r.w, 1, COL_BORDER);
    text_at(r.x + 8, r.y + 6, label, COL_WHITE);
}

static void draw_input(rct r, const char *value, int focused, bool password)
{
    rect(r.x, r.y, r.w, r.h, COL_INPUT);
    rect(r.x, r.y, r.w, 1, focused ? COL_ACCENT : COL_BORDER);
    if (value[0] == '\0') {
        text_at(r.x + 6, r.y + 5, " ", COL_MUTED);
    } else if (password) {
        char stars[64];
        uint32_t k = 0;
        while (value[k] != '\0' && k + 1 < sizeof(stars)) { stars[k] = '*'; k++; }
        stars[k] = '\0';
        text_at(r.x + 6, r.y + 5, stars, COL_TEXT);
    } else {
        text_at(r.x + 6, r.y + 5, value, COL_TEXT);
    }
}

/* format byte size into a small buffer */
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

static void draw_list_panel(rct box, const char *title, ent_t *ents, int n, int sel)
{
    uint32_t row = 0;
    uint32_t i = 0;
    rect(box.x, box.y, box.w, box.h, COL_PANEL);
    text_at(box.x + 6, box.y + 4, title, COL_ACCENT);
    row = 22;
    for (i = 0; i < (uint32_t) n && row < (uint32_t) (box.h - 4u); i++) {
        uint16_t ry = (uint16_t) (box.y + row);
        char line[140];
        uint32_t q = 0;
        char sz[16];
        if ((int) i == sel) {
            rect(box.x + 2, ry - 1, (uint16_t) (box.w - 4u), 16, COL_SEL);
        }
        if (ents[i].is_dir) { line[q++] = '['; }
        {
            uint32_t k = 0;
            while (ents[i].name[k] != '\0' && q + 2 < sizeof(line) - 12u) line[q++] = ents[i].name[k++];
        }
        if (ents[i].is_dir) line[q++] = ']';
        line[q] = '\0';
        fmt_size(ents[i].size, sz);
        /* pad to fixed column then size */
        while (q < 26u && q + 1 < sizeof(line)) line[q++] = ' ';
        line[q] = '\0';
        {
            uint32_t z = 0;
            while (sz[z] != '\0' && q + 1 < sizeof(line)) line[q++] = sz[z++];
            line[q] = '\0';
        }
        text_at(box.x + 6, ry, line, ents[i].is_dir ? COL_GREEN : COL_TEXT);
        row += 16;
    }
}

static void render(void)
{
    rct r_host = { 8,  36, 200, 22 };
    rct r_port = { 216, 36, 56, 22 };
    rct r_user = { 280, 36, 140, 22 };
    rct r_pass = { 428, 36, 140, 22 };
    rct r_conn = { 576, 34, 96, 26 };
    rct b_up   = { 8,  68, 90, 24 };
    rct b_dl   = { 104, 68, 90, 24 };
    rct b_del  = { 200, 68, 90, 24 };
    rct b_ref  = { 296, 68, 90, 24 };
    rct b_mk   = { 392, 68, 96, 24 };
    rct b_updir= { 494, 68, 110, 24 };
    rct lbox   = { 8,  100, 490, 470 };
    rct rbox   = { 526, 100, 490, 470 };
    uint32_t li = 0;
    char title[40];

    rect(0, 0, SCR_W, SCR_H, COL_BG);
    text_at(8, 8, "Monios FTP 客户端", COL_WHITE);
    text_at(760, 10, g_ctrl < 0 ? "状态: 未连接" : "状态: 已连接",
            g_ctrl < 0 ? COL_RED : COL_GREEN);

    draw_input(r_host, f_host, g_focus == FIELD_HOST, false);
    draw_input(r_port, f_port, g_focus == FIELD_PORT, false);
    draw_input(r_user, f_user, g_focus == FIELD_USER, false);
    draw_input(r_pass, f_pass, g_focus == FIELD_PASS, true);
    draw_button(r_conn, g_ctrl < 0 ? "连接" : "断开",
                g_ctrl < 0 ? COL_ACCENT : COL_RED);

    draw_button(b_up,   "上传",  COL_ACCENT);
    draw_button(b_dl,   "下载",  COL_GREEN);
    draw_button(b_del,  "删除",  COL_RED);
    draw_button(b_ref,  "刷新",  COL_BORDER);
    draw_button(b_mk,   "新建夹", COL_BORDER);
    draw_button(b_updir,"上级目录", COL_BORDER);

    {
        uint32_t n = (uint32_t) strlen(g_lcwd);
        uint32_t k = 0;
        uint32_t c = 0;
        while (g_lcwd[k] != '\0' && k < sizeof(title) - 20u) { title[c++] = g_lcwd[k]; k++; }
        title[c] = '\0';
        /* note n unused */
        (void) n;
    }
    draw_list_panel(lbox, title, g_lent, g_lent_n, g_lsel);
    draw_list_panel(rbox, g_rcwd, g_rent, g_rent_n, g_rsel);

    /* progress bar */
    rect(8, 578, 1008, 14, COL_INPUT);
    if (g_busy && g_prog_total > 0) {
        uint32_t pct = g_prog_cur * 100u / g_prog_total;
        uint16_t pw = (uint16_t) (g_prog_cur * 1008u / g_prog_total);
        rect(8, 578, pw, 14, COL_ACCENT);
        {
            char p[16];
            u2str(pct, p);
            text_at(500, 580, p, COL_WHITE);
        }
    }
    text_at(10, 580, g_prog_label, COL_MUTED);

    /* log area */
    rect(8, 598, 1008, 162, COL_INPUT);
    for (li = 0; li < LOG_LINES; li++) {
        if (g_log[li][0] != '\0') {
            text_at(12, (uint16_t) (600 + li * 17), g_log[li], COL_MUTED);
        }
    }
    app_graphics_present();
}

/* ================= keyboard ================= */
static int read_kb(uint32_t *type, char *ch, uint8_t *mods)
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

static void field_append(int field, char c)
{
    char *dst = f_host;
    uint32_t sz = sizeof(f_host);
    uint32_t n = 0;
    if (field == FIELD_PORT) { dst = f_port; sz = sizeof(f_port); }
    else if (field == FIELD_USER) { dst = f_user; sz = sizeof(f_user); }
    else if (field == FIELD_PASS) { dst = f_pass; sz = sizeof(f_pass); }
    n = (uint32_t) strlen(dst);
    if (n + 1 < sz) dst[n++] = c;
    dst[n] = '\0';
}

static void field_backspace(int field)
{
    char *dst = f_host;
    uint32_t n = 0;
    if (field == FIELD_PORT) dst = f_port;
    else if (field == FIELD_USER) dst = f_user;
    else if (field == FIELD_PASS) dst = f_pass;
    n = (uint32_t) strlen(dst);
    if (n > 0) dst[n - 1] = '\0';
}

/* ================= main ================= */
int main(int argc, char **argv)
{
    int prev_buttons = 0;
    int last_panel = -1;
    int last_row = -1;
    uint64_t last_click_tick = 0;

    (void) argc; (void) argv;

    strlcpy(f_host, "ftp.monios", sizeof(f_host));
    strlcpy(f_port, "21", sizeof(f_port));
    strlcpy(f_user, "anonymous", sizeof(f_user));
    f_pass[0] = '\0';

    app_getcwd(g_lcwd, sizeof(g_lcwd));
    if (g_lcwd[0] == '\0') strlcpy(g_lcwd, "C:\\", sizeof(g_lcwd));
    local_refresh();

    app_enter_graphics_mode();

    for (;;) {
        uint32_t type = 0;
        char ch = 0;
        uint8_t mods = 0;
        app_mouse_snapshot_t ms;
        int down = 0;
        bool clicked = false;

        app_sleep_ticks(2);

        /* drain keyboard */
        while (read_kb(&type, &ch, &mods)) {
            if (type == 30 /* ESC */) {
                if (g_focus >= 0) g_focus = -1;
                else { do_disconnect(); return 0; }
                continue;
            }
            if (type == 1 /* CHAR */) {
                if (g_focus >= 0) {
                    if (ch == '\r' || ch == '\n') {
                        g_focus = -1;
                    } else if (ch == 8) {
                        field_backspace(g_focus);
                    } else if (ch >= 32 && ch < 127) {
                        field_append(g_focus, ch);
                    }
                } else if (ch == '\r' || ch == '\n') {
                    do_connect();
                }
            }
        }

        /* mouse edge detect */
        if (app_get_mouse(&ms) == 0) {
            down = ms.buttons & 1;
            if (down && !prev_buttons) clicked = true;
            prev_buttons = down;
        }

        if (clicked) {
            int mx = ms.x_pixels;
            int my = ms.y_pixels;
            rct r_host = { 8, 36, 200, 22 };
            rct r_port = { 216, 36, 56, 22 };
            rct r_user = { 280, 36, 140, 22 };
            rct r_pass = { 428, 36, 140, 22 };
            rct r_conn = { 576, 34, 96, 26 };
            rct b_up = { 8, 68, 90, 24 };
            rct b_dl = { 104, 68, 90, 24 };
            rct b_del = { 200, 68, 90, 24 };
            rct b_ref = { 296, 68, 90, 24 };
            rct b_mk = { 392, 68, 96, 24 };
            rct b_updir = { 494, 68, 110, 24 };
            rct lbox = { 8, 100, 490, 470 };
            rct rbox = { 526, 100, 490, 470 };

            if (rct_hit(r_host, mx, my)) g_focus = FIELD_HOST;
            else if (rct_hit(r_port, mx, my)) g_focus = FIELD_PORT;
            else if (rct_hit(r_user, mx, my)) g_focus = FIELD_USER;
            else if (rct_hit(r_pass, mx, my)) g_focus = FIELD_PASS;
            else if (rct_hit(r_conn, mx, my)) {
                if (g_ctrl < 0) do_connect(); else do_disconnect();
            }
            else if (rct_hit(b_up, mx, my)) do_upload();
            else if (rct_hit(b_dl, mx, my)) do_download();
            else if (rct_hit(b_del, mx, my)) do_delete_remote();
            else if (rct_hit(b_ref, mx, my)) { local_refresh(); if (g_ctrl >= 0) remote_list(); }
            else if (rct_hit(b_mk, mx, my)) do_mkdir_remote();
            else if (rct_hit(b_updir, mx, my)) { do_local_up(); do_remote_up(); }
            else if (rct_hit(lbox, mx, my)) {
                int row = (my - 100 - 22) / 16;
                if (row >= 0 && row < g_lent_n) {
                    uint64_t now = app_ticks();
                    g_lsel = row;
                    if (last_panel == 0 && last_row == row && (now - last_click_tick) < 25u) {
                        do_local_enter();
                        last_row = -1;
                    } else {
                        last_panel = 0; last_row = row; last_click_tick = now;
                    }
                }
            }
            else if (rct_hit(rbox, mx, my)) {
                int row = (my - 100 - 22) / 16;
                if (row >= 0 && row < g_rent_n) {
                    uint64_t now = app_ticks();
                    g_rsel = row;
                    if (last_panel == 1 && last_row == row && (now - last_click_tick) < 25u) {
                        do_remote_enter();
                        last_row = -1;
                    } else {
                        last_panel = 1; last_row = row; last_click_tick = now;
                    }
                }
            }
            else {
                g_focus = -1;
            }
        }

        render();
    }
}
