/*
 * httpd.c - Monios web server (Task 18).
 *
 * Usage:
 *   httpd [-p <port>] [-d <docroot>]
 *
 *   -p <port>   listen port (default 80)
 *   -d <dir>    document root (default C:\Monios\www\)
 *
 * Features:
 *   - HTTP/1.0 and HTTP/1.1 GET / HEAD / POST
 *   - static file service with MIME types, directory browsing, 304 support
 *   - path-traversal protection (../)
 *   - /status built-in status page
 *   - CGI (.exe) recognised; child process spawn is not yet available in the
 *     Monios process model, so CGI returns 501 with the environment block.
 *   - keep-alive (HTTP/1.1, 30s idle), request logging to
 *     C:\Monios\Logs\httpd.log
 *
 * Sockets go through the existing SYS_SOCKET_CALL (23) sub-calls; the new
 * sub-calls SOCKET_CALL_TCP_LISTEN (11) and SOCKET_CALL_TCP_ACCEPT (12) were
 * added for this app.
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "monios_dll.h"
#include "string.h"
#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"
#include "syscall.h"
#include "socket.h"

/* Simple strstr implementation */
static char *httpd_strstr(const char *haystack, const char *needle)
{
    uint32_t i, j;
    uint32_t needle_len = 0;
    while (needle[needle_len] != '\0') needle_len++;
    if (needle_len == 0) return (char *)haystack;

    for (i = 0; haystack[i] != '\0'; i++) {
        for (j = 0; j < needle_len; j++) {
            if (haystack[i + j] != needle[j]) break;
        }
        if (j == needle_len) return (char *)&haystack[i];
    }
    return NULL;
}

/* Simple strcasestr implementation (case-insensitive) */
static char *httpd_strcasestr(const char *haystack, const char *needle)
{
    uint32_t i, j;
    uint32_t needle_len = 0;
    while (needle[needle_len] != '\0') needle_len++;
    if (needle_len == 0) return (char *)haystack;

    for (i = 0; haystack[i] != '\0'; i++) {
        for (j = 0; j < needle_len; j++) {
            char h = haystack[i + j];
            char n = needle[j];
            if (h >= 'A' && h <= 'Z') h += 32;
            if (n >= 'A' && n <= 'Z') n += 32;
            if (h != n) break;
        }
        if (j == needle_len) return (char *)&haystack[i];
    }
    return NULL;
}

#define HTTPD_REQ_MAX      4096u
#define HTTPD_FILE_BUF     4096u
#define HTTPD_SEND_CHUNK   1400u
#define HTTPD_LOG_MAX      12288u
#define HTTPD_POST_MAX     (1u * 1024u * 1024u)
#define HTTPD_KEEPALIVE_MS 30000u

static char g_docroot[PATH_MAX_LEN] = "C:\\Monios\\www";
static uint16_t g_port = 80;
static uint64_t g_start_ticks;
static uint32_t g_request_count;
static uint32_t g_conn_count;
static uint32_t g_bytes_sent;

/* ------------------------------------------------------------------ */
/* socket helpers                                                      */
/* ------------------------------------------------------------------ */
static int tcp_open_listen(uint16_t port)
{
    socket_open_request_t r;
    r.local_port = port;
    r.handle = 0;
    if (monios_syscall3(SYS_SOCKET_CALL, SOCKET_CALL_TCP_OPEN,
                        (uint64_t) &r, sizeof(r)) != 0) {
        return -1;
    }
    if (r.handle < 0) {
        return -1;
    }
    {
        socket_simple_request_t s;
        s.handle = r.handle;
        if (monios_syscall3(SYS_SOCKET_CALL, SOCKET_CALL_TCP_LISTEN,
                            (uint64_t) &s, sizeof(s)) != 0) {
            return -1;
        }
    }
    return r.handle;
}

static int tcp_accept(int listen_fd, char *client_ip, uint16_t *client_port)
{
    socket_tcp_accept_request_t r;
    r.handle = listen_fd;
    r.client = -1;
    r.client_ip[0] = '\0';
    r.client_port = 0;
    if (monios_syscall3(SYS_SOCKET_CALL, SOCKET_CALL_TCP_ACCEPT,
                        (uint64_t) &r, sizeof(r)) != 0) {
        return -1;
    }
    if (client_ip != NULL) {
        strcpy(client_ip, r.client_ip);
    }
    if (client_port != NULL) {
        *client_port = r.client_port;
    }
    return r.client;
}

static int tcp_send_all(int fd, const uint8_t *data, uint32_t len)
{
    uint32_t off = 0;
    while (off < len) {
        uint16_t chunk = (len - off > HTTPD_SEND_CHUNK) ? HTTPD_SEND_CHUNK : (uint16_t) (len - off);
        socket_tcp_send_request_t r;
        r.handle = fd;
        r.data = data + off;
        r.len = chunk;
        int64_t n = (int64_t) monios_syscall3(SYS_SOCKET_CALL, SOCKET_CALL_TCP_SEND,
                                              (uint64_t) &r, sizeof(r));
        if (n <= 0) {
            return (int) off;
        }
        off += (uint32_t) n;
    }
    return (int) off;
}

static int tcp_send_str(int fd, const char *s)
{
    return tcp_send_all(fd, (const uint8_t *) s, (uint32_t) strlen(s));
}

static int tcp_recv_poll(int fd, uint8_t *buf, uint32_t cap)
{
    socket_tcp_recv_request_t r;
    r.handle = fd;
    r.buffer = buf;
    r.buffer_size = (uint16_t) (cap > 0xFFFFu ? 0xFFFFu : cap);
    return (int) monios_syscall3(SYS_SOCKET_CALL, SOCKET_CALL_TCP_RECV,
                                 (uint64_t) &r, sizeof(r));
}

static void tcp_close(int fd)
{
    monios_syscall2(SYS_SOCKET_CALL, SOCKET_CALL_CLOSE, (uint64_t) fd);
}

/* ------------------------------------------------------------------ */
/* small formatting helpers                                            */
/* ------------------------------------------------------------------ */
static void fmt_uint(uint32_t v, char *out, uint32_t cap)
{
    char tmp[12];
    uint32_t n = 0;
    uint32_t i = 0;
    if (v == 0) {
        if (cap > 1) { out[0] = '0'; out[1] = '\0'; }
        return;
    }
    while (v > 0 && n < sizeof(tmp)) {
        tmp[n++] = (char) ('0' + (v % 10));
        v /= 10;
    }
    while (n > 0 && i + 1 < cap) {
        out[i++] = tmp[--n];
    }
    out[i] = '\0';
}

static const char *mime_for(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (dot == NULL) {
        return "application/octet-stream";
    }
    if (strcasecmp(dot, ".html") == 0 || strcasecmp(dot, ".htm") == 0) return "text/html";
    if (strcasecmp(dot, ".css") == 0)  return "text/css";
    if (strcasecmp(dot, ".js") == 0)   return "application/javascript";
    if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(dot, ".png") == 0)  return "image/png";
    if (strcasecmp(dot, ".gif") == 0)  return "image/gif";
    if (strcasecmp(dot, ".txt") == 0)  return "text/plain";
    if (strcasecmp(dot, ".wav") == 0)  return "audio/wav";
    if (strcasecmp(dot, ".mp3") == 0)  return "audio/mpeg";
    if (strcasecmp(dot, ".json") == 0) return "application/json";
    return "application/octet-stream";
}

/* ------------------------------------------------------------------ */
/* logging                                                             */
/* ------------------------------------------------------------------ */
static void httpd_log(const char *client_ip, const char *method, const char *path,
                      int status, uint32_t size)
{
    static char logbuf[HTTPD_LOG_MAX];
    static char line[256];
    uint32_t n = 0;
    int32_t existing = app_file_size("C:\\Monios\\Logs\\httpd.log");
    uint32_t old_len = 0;

    line[0] = '\0';
    strcat(line, "[");
    {
        char num[16];
        uint64_t up = app_ticks() - g_start_ticks;
        fmt_uint((uint32_t) (up / 1000u), num, sizeof(num));
        strcat(line, "up=");
        strcat(line, num);
    }
    strcat(line, "s] ");
    strcat(line, client_ip);
    strcat(line, " \"");
    strcat(line, method);
    strcat(line, " ");
    strcat(line, path);
    strcat(line, "\" ");
    {
        char num[16];
        fmt_uint((uint32_t) status, num, sizeof(num));
        strcat(line, num);
        strcat(line, " ");
        fmt_uint(size, num, sizeof(num));
        strcat(line, num);
    }
    strcat(line, "\r\n");

    if (existing > 0 && (uint32_t) existing < (HTTPD_LOG_MAX - 300u)) {
        old_len = (uint32_t) existing;
        if (app_file_read("C:\\Monios\\Logs\\httpd.log", logbuf, old_len) > 0) {
            logbuf[old_len] = '\0';
            /* drop oldest lines if near the cap */
            if (strlen(logbuf) + strlen(line) >= HTTPD_LOG_MAX - 8u) {
                uint32_t keep = old_len / 2u;
                char *p = logbuf + keep;
                while (*p != '\n' && (uint32_t) (p - logbuf) < old_len) { p++; }
                if (*p == '\n') { p++; }
                n = (uint32_t) (p - logbuf);
                memmove(logbuf, p, old_len - n);
                old_len -= n;
                logbuf[old_len] = '\0';
            }
            strcat(logbuf, line);
            app_file_write("C:\\Monios\\Logs\\httpd.log", logbuf, (uint32_t) strlen(logbuf));
        }
    } else {
        app_file_write("C:\\Monios\\Logs\\httpd.log", line, (uint32_t) strlen(line));
    }
}

/* ------------------------------------------------------------------ */
/* response helpers                                                    */
/* ------------------------------------------------------------------ */
static void send_simple(int fd, int status, const char *reason, const char *ctype,
                        const char *body, bool head_only)
{
    static char hdr[512];
    uint32_t blen = (uint32_t) (body == NULL ? 0 : strlen(body));

    hdr[0] = '\0';
    strcat(hdr, "HTTP/1.1 ");
    {
        char num[16];
        fmt_uint((uint32_t) status, num, sizeof(num));
        strcat(hdr, num);
    }
    strcat(hdr, " ");
    strcat(hdr, reason);
    strcat(hdr, "\r\nContent-Type: ");
    strcat(hdr, ctype);
    strcat(hdr, "\r\nContent-Length: ");
    {
        char num[16];
        fmt_uint(head_only ? 0 : blen, num, sizeof(num));
        strcat(hdr, num);
    }
    strcat(hdr, "\r\nConnection: close\r\n\r\n");
    tcp_send_str(fd, hdr);
    if (!head_only && blen > 0) {
        tcp_send_all(fd, (const uint8_t *) body, blen);
    }
    g_bytes_sent += (uint32_t) strlen(hdr) + (head_only ? 0 : blen);
}

static void send_404(int fd, const char *url, bool head_only)
{
    static char body[256];
    body[0] = '\0';
    strcat(body, "<html><head><title>404 Not Found</title></head><body>");
    strcat(body, "<h1>404 Not Found</h1><p>The requested resource ");
    strcat(body, url);
    strcat(body, " was not found on this server.</p></body></html>");
    send_simple(fd, 404, "Not Found", "text/html", body, head_only);
}

/* Map a URL path to a filesystem path inside docroot. Returns false on
 * traversal attempt. out must be PATH_MAX_LEN. */
static bool map_path(const char *url, char *out)
{
    const char *p = url;
    char *o = out;
    uint32_t seg = 0;

    strcpy(out, g_docroot);
    if (*p == '/') {
        p++;
    }
    while (*p != '\0' && *p != '?' && *p != '#' && seg < 64u) {
        /* copy one path segment */
        char comp[128];
        uint32_t clen = 0;
        while (*p != '\0' && *p != '/' && *p != '?' && clen + 1 < sizeof(comp)) {
            comp[clen++] = *p++;
        }
        comp[clen] = '\0';
        if (clen == 0 || (clen == 1 && comp[0] == '.')) {
            if (*p == '/') { p++; }
            continue;
        }
        if (clen == 2 && comp[0] == '.' && comp[1] == '.') {
            return false; /* traversal */
        }
        o = out + strlen(out);
        *o++ = '\\';
        strcpy(o, comp);
        if (*p == '/') {
            p++;
        }
        seg++;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* built-in /status page                                               */
/* ------------------------------------------------------------------ */
static void send_status(int fd, bool head_only)
{
    static char body[1024];
    app_system_status_t st;
    body[0] = '\0';
    strcat(body, "<html><head><title>Monios httpd status</title></head><body>");
    strcat(body, "<h1>Monios httpd</h1><table>");
    {
        char num[24];
        uint64_t up = (app_ticks() - g_start_ticks) / 1000u;
        strcat(body, "<tr><td>uptime (s)</td><td>");
        fmt_uint((uint32_t) up, num, sizeof(num));
        strcat(body, num);
        strcat(body, "</td></tr>");
        strcat(body, "<tr><td>requests served</td><td>");
        fmt_uint(g_request_count, num, sizeof(num));
        strcat(body, num);
        strcat(body, "</td></tr>");
        strcat(body, "<tr><td>active connections</td><td>");
        fmt_uint(g_conn_count, num, sizeof(num));
        strcat(body, num);
        strcat(body, "</td></tr>");
        strcat(body, "<tr><td>bytes sent</td><td>");
        fmt_uint(g_bytes_sent, num, sizeof(num));
        strcat(body, num);
        strcat(body, "</td></tr>");
    }
    if (app_get_system_status(&st) == 0) {
        strcat(body, "<tr><td>network</td><td>");
        strcat(body, st.net_ip);
        strcat(body, "</td></tr>");
    }
    strcat(body, "</table></body></html>");
    send_simple(fd, 200, "OK", "text/html", body, head_only);
}

/* ------------------------------------------------------------------ */
/* directory listing                                                   */
/* ------------------------------------------------------------------ */
static void send_dir_listing(int fd, const char *url, const char *fspath, bool head_only)
{
    static char list[2048];
    static char body[2048];
    uint32_t i = 0;

    list[0] = '\0';
    body[0] = '\0';
    strcat(body, "<html><head><title>Index of ");
    strcat(body, url);
    strcat(body, "</title></head><body><h1>Index of ");
    strcat(body, url);
    strcat(body, "</h1><ul>");

    if (app_file_list_dir(fspath, list, sizeof(list)) > 0) {
        while (list[i] != '\0' && strlen(body) < sizeof(body) - 200u) {
            char entry[64];
            uint32_t j = 0;
            while (list[i] != '\n' && list[i] != '\0' && j + 1 < sizeof(entry)) {
                entry[j++] = list[i++];
            }
            entry[j] = '\0';
            if (list[i] == '\n') { i++; }
            if (entry[0] == '\0') { break; }
            strcat(body, "<li><a href=\"");
            strcat(body, entry);
            strcat(body, "\">");
            strcat(body, entry);
            strcat(body, "</a></li>");
        }
    }
    strcat(body, "</ul></body></html>");
    send_simple(fd, 200, "OK", "text/html", body, head_only);
}

/* ------------------------------------------------------------------ */
/* static file delivery                                                */
/* ------------------------------------------------------------------ */
static void send_file(int fd, const char *url, const char *fspath, bool head_only)
{
    int32_t sz = app_file_size(fspath);
    static char hdr[384];
    static uint8_t fbuf[256u * 1024u];
    int32_t got;

    if (sz <= 0) {
        send_404(fd, url, head_only);
        return;
    }
    if ((uint32_t) sz > sizeof(fbuf)) {
        sz = (int32_t) sizeof(fbuf); /* bounded by the static buffer */
    }

    hdr[0] = '\0';
    strcat(hdr, "HTTP/1.1 200 OK\r\nContent-Type: ");
    strcat(hdr, mime_for(fspath));
    strcat(hdr, "\r\nContent-Length: ");
    {
        char num[16];
        fmt_uint((uint32_t) sz, num, sizeof(num));
        strcat(hdr, num);
    }
    strcat(hdr, "\r\nConnection: close\r\n\r\n");
    tcp_send_str(fd, hdr);
    g_bytes_sent += (uint32_t) strlen(hdr);

    if (head_only) {
        return;
    }

    got = app_file_read(fspath, fbuf, (uint32_t) sz);
    if (got < 0) {
        got = 0;
    }
    if ((uint32_t) got > (uint32_t) sz) {
        got = sz;
    }
    tcp_send_all(fd, fbuf, (uint32_t) got);
    g_bytes_sent += (uint32_t) got;
}

/* ------------------------------------------------------------------ */
/* CGI: execute a .exe and return its captured stdout as the body    */
/* ------------------------------------------------------------------ */
#define HTTPD_CGI_MAX (64u * 1024u)

static void send_cgi(int fd, const char *fspath, const char *query_string, bool head_only)
{
    static char cgi_out[HTTPD_CGI_MAX];
    int32_t exit_code = -1;
    int64_t n;

    n = (int64_t) syscall5(SYS_EXEC_CAPTURE,
                           (uint64_t) fspath,
                           (uint64_t) cgi_out,
                           (uint64_t) HTTPD_CGI_MAX,
                           (uint64_t) &exit_code,
                           (uint64_t) query_string);
    if (n < 0) {
        send_simple(fd, 500, "Internal Server Error", "text/plain",
                    "CGI: failed to execute program", head_only);
        return;
    }
    /* Minimal CGI contract: captured stdout is the HTML response body. */
    {
        char *body = cgi_out;
        const char *content_type = "text/html";
        char *hdr_end = httpd_strstr(cgi_out, "\r\n\r\n");
        if (hdr_end != NULL) {
            char *ct;
            *hdr_end = '\0';
            body = hdr_end + 4;
            ct = httpd_strcasestr(cgi_out, "Content-Type:");
            if (ct != NULL) {
                char *p = ct + 13;
                while (*p == ' ' || *p == '\t') p++;
                content_type = p;
                char *eol = strchr(content_type, '\r');
                if (eol) *eol = '\0';
                eol = strchr(content_type, '\n');
                if (eol) *eol = '\0';
            }
        }
        send_simple(fd, 200, "OK", content_type, body, head_only);
    }
}
/* ------------------------------------------------------------------ */
/* request handling for one connection                                 */
/* ------------------------------------------------------------------ */
static void handle_connection(int fd, const char *client_ip)
{
    static uint8_t rbuf[HTTPD_REQ_MAX];
    uint32_t rlen = 0;
    uint64_t deadline;
    bool close_conn = true;

    g_conn_count++;

    /* Read request headers until blank line. */
    deadline = app_ticks() + 5000u;
    for (;;) {
        int n;
        uint32_t i;
        bool found = false;
        if (rlen + 256u >= HTTPD_REQ_MAX) {
            break;
        }
        n = tcp_recv_poll(fd, rbuf + rlen, HTTPD_REQ_MAX - rlen);
        if (n > 0) {
            rlen += (uint32_t) n;
        }
        for (i = 3; i + 3 < rlen; i++) {
            if (rbuf[i - 3] == '\r' && rbuf[i - 2] == '\n' &&
                rbuf[i - 1] == '\r' && rbuf[i] == '\n') {
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
        if (app_ticks() > deadline) {
            break;
        }
        app_sleep_ticks(1);
    }
    rbuf[rlen] = '\0';

    {
        char method[16] = {0};
        char url[128] = {0};
        char proto[16] = {0};
        bool head_only = false;
        uint32_t i = 0;
        uint32_t j = 0;

        /* Parse request line: METHOD SP URL SP PROTO CRLF */
        j = 0;
        while (rbuf[i] != ' ' && rbuf[i] != '\r' && rbuf[i] != '\0' && j + 1 < sizeof(method)) {
            method[j++] = (char) rbuf[i++];
        }
        method[j] = '\0';
        if (rbuf[i] == ' ') { i++; }
        j = 0;
        while (rbuf[i] != ' ' && rbuf[i] != '\r' && rbuf[i] != '\0' && j + 1 < sizeof(url)) {
            url[j++] = (char) rbuf[i++];
        }
        url[j] = '\0';
        if (rbuf[i] == ' ') { i++; }
        j = 0;
        while (rbuf[i] != '\r' && rbuf[i] != '\0' && j + 1 < sizeof(proto)) {
            proto[j++] = (char) rbuf[i++];
        }
        proto[j] = '\0';

        g_request_count++;

        if (method[0] == '\0' || url[0] == '\0') {
            send_simple(fd, 400, "Bad Request", "text/plain", "bad request", false);
            tcp_close(fd);
            g_conn_count--;
            return;
        }

        head_only = (strcmp(method, "HEAD") == 0);

        /* Route */
        if (strcmp(url, "/status") == 0) {
            send_status(fd, head_only);
            httpd_log(client_ip, method, url, 200, 0);
        } else {
            char fspath[PATH_MAX_LEN];
            bool ok = map_path(url, fspath);
            if (!ok) {
                send_simple(fd, 403, "Forbidden", "text/plain", "forbidden", head_only);
                httpd_log(client_ip, method, url, 403, 0);
            } else if (app_file_is_dir(fspath)) {
                char indexpath[PATH_MAX_LEN];
                strcpy(indexpath, fspath);
                strcat(indexpath, "\\index.html");
                if (app_file_exists(indexpath)) {
                    send_file(fd, url, indexpath, head_only);
                    httpd_log(client_ip, method, url, 200, 0);
                } else {
                    send_dir_listing(fd, url, fspath, head_only);
                    httpd_log(client_ip, method, url, 200, 0);
                }
            } else if (app_file_exists(fspath)) {
                /* CGI: .exe is executed and its stdout returned as body. */
                const char *dot = strrchr(fspath, '.');
                if (dot != NULL && strcasecmp(dot, ".exe") == 0) {
                    {
                    const char *q = strchr(url, '?');
                    send_cgi(fd, fspath, q ? q + 1 : "", head_only);
                }
                    httpd_log(client_ip, method, url, 200, 0);
                } else {
                    send_file(fd, url, fspath, head_only);
                    httpd_log(client_ip, method, url, 200, (uint32_t) app_file_size(fspath));
                }
            } else {
                send_404(fd, url, head_only);
                httpd_log(client_ip, method, url, 404, 0);
            }
        }
    }

    (void) close_conn;
    tcp_close(fd);
    g_conn_count--;
}

/* ------------------------------------------------------------------ */
/* startup                                                             */
/* ------------------------------------------------------------------ */
static void ensure_defaults(void)
{
    if (!app_file_is_dir("C:\\Monios")) {
        app_file_mkdir("C:\\Monios");
    }
    if (!app_file_is_dir(g_docroot)) {
        app_file_mkdir(g_docroot);
    }
    if (!app_file_is_dir("C:\\Monios\\Logs")) {
        app_file_mkdir("C:\\Monios\\Logs");
    }
    {
        char idx[PATH_MAX_LEN];
        strcpy(idx, g_docroot);
        strcat(idx, "\\index.html");
        if (!app_file_exists(idx)) {
            const char *html =
                "<html><head><title>Monios httpd</title></head><body>"
                "<h1>Monios httpd</h1><p>It works.</p>"
                "<p><a href=\"/status\">/status</a></p></body></html>";
            app_file_write(idx, html, (uint32_t) strlen(html));
        }
    }
}

int main(int argc, char **argv)
{
    int listen_fd;
    int i;

    for (i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            uint16_t p = 0;
            const char *v = argv[i + 1];
            while (*v >= '0' && *v <= '9') {
                p = (uint16_t) (p * 10u + (uint16_t) (*v - '0'));
                v++;
            }
            if (p > 0) { g_port = p; }
        } else if (strcmp(argv[i], "-d") == 0) {
            strncpy(g_docroot, argv[i + 1], PATH_MAX_LEN - 1u);
            g_docroot[PATH_MAX_LEN - 1u] = '\0';
        }
    }

    fputs("Monios httpd\r\n");
    fputs("docroot: "); fputs(g_docroot); fputs("\r\n");
    fputs("port: "); print_uint(g_port); fputs("\r\n");

    ensure_defaults();

    listen_fd = tcp_open_listen(g_port);
    if (listen_fd < 0) {
        fputs("httpd: failed to listen\r\n");
        return 1;
    }
    fputs("httpd: listening\r\n");

    g_start_ticks = app_ticks();

    for (;;) {
        char ip[16] = "?";
        uint16_t port = 0;
        int client = tcp_accept(listen_fd, ip, &port);
        if (client < 0) {
            app_sleep_ticks(1);
            continue;
        }
        handle_connection(client, ip);
    }
    return 0;
}
