/*
 * chat.c -- Monios simple TCP instant messenger.
 *
 *   1024x768 GUI. Left: user list. Right: message history + input box.
 *   Top bar: connection status, username, connect/listen button.
 *
 *   Modes:
 *     client: connect to a chat server at IP:port
 *     server: listen on a port, accept up to 8 clients, broadcast
 *
 *   Wire protocol (text, newline-terminated):
 *     PUBLIC  "<username>:<message>\n"
 *     PRIVATE "@<target>:<username>:<message>\n"
 *     JOIN    "+<username>\n"
 *     PART    "-<username>\n"
 *     USERS   "!<user1,user2,...>\n"
 *
 *   Config: /Monios/Config/chat.cfg
 *     default_host=192.168.1.10
 *     default_port=6666
 *     username=MoniosUser
 *
 *   Keys: type into input box, Enter to send, Esc to quit.
 *   Mouse: click [Connect] to connect/listen, click input box to focus.
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
#define EV_ENTER  28

typedef struct {
    uint32_t type;
    char ch;
    uint8_t mods;
    uint8_t pad[3];
} app_key_event_t;

/* ---------------- config ---------------- */
#define CFG_PATH "/Monios/Config/chat.cfg"
#define MAX_CLIENTS 8u
#define MAX_HISTORY 100u
#define HIST_LINE   200u

typedef struct {
    char host[64];
    char port[8];
    char username[32];
} chat_cfg_t;

static chat_cfg_t g_cfg;

static void cfg_load(void)
{
    static char buf[512];
    int n;
    char *line;

    strlcpy(g_cfg.host, "127.0.0.1", sizeof(g_cfg.host));
    strlcpy(g_cfg.port, "6666", sizeof(g_cfg.port));
    strlcpy(g_cfg.username, "MoniosUser", sizeof(g_cfg.username));

    n = app_file_read(CFG_PATH, buf, sizeof(buf) - 1u);
    if (n <= 0) return;
    buf[n] = '\0';
    line = buf;
    while (*line != '\0') {
        char *eol = line;
        char *eq;
        while (*eol != '\0' && *eol != '\n') eol++;
        if (*eol == '\n') { *eol = '\0'; eol++; }
        {
            uint32_t i = (uint32_t)strlen(line);
            if (i > 0 && line[i - 1] == '\r') line[i - 1] = '\0';
        }
        eq = strchr(line, '=');
        if (eq != 0) {
            *eq = '\0';
            if (strcmp(line, "default_host") == 0)
                strlcpy(g_cfg.host, eq + 1, sizeof(g_cfg.host));
            else if (strcmp(line, "default_port") == 0)
                strlcpy(g_cfg.port, eq + 1, sizeof(g_cfg.port));
            else if (strcmp(line, "username") == 0)
                strlcpy(g_cfg.username, eq + 1, sizeof(g_cfg.username));
        }
        line = eol;
    }
}

static void cfg_save(void)
{
    char buf[256];
    int n = 0;
    n += sprintf(buf + n, "default_host=%s\n", g_cfg.host);
    n += sprintf(buf + n, "default_port=%s\n", g_cfg.port);
    n += sprintf(buf + n, "username=%s\n", g_cfg.username);
    app_file_write(CFG_PATH, buf, (uint32_t)n);
}

/* ---------------- network ---------------- */
enum { MODE_IDLE, MODE_CLIENT, MODE_SERVER };
static uint32_t g_mode = MODE_IDLE;
static int g_sock = -1;                 /* client socket OR listen socket */
static int g_clients[MAX_CLIENTS];      /* accepted client fds (server) */
static char g_client_ip[MAX_CLIENTS][16];
static uint8_t g_client_count;

static char g_history[MAX_HISTORY][HIST_LINE];
static uint32_t g_hist_n;
static int g_hist_scroll;               /* scroll offset (>=0) */

static char g_users[16][24];            /* known usernames */
static uint8_t g_user_count;

static char g_input[128];
static uint8_t g_input_n;
static bool g_input_focus = true;

static char g_status[80];

static void hist_add(const char *line)
{
    strlcpy(g_history[g_hist_n % MAX_HISTORY], line, HIST_LINE);
    g_hist_n++;
    if (g_hist_scroll > 0) g_hist_scroll = 0;
}

static uint16_t parse_port(const char *s)
{
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10u + (uint32_t)(*s - '0'); s++; }
    if (v == 0 || v > 65535) return 6666;
    return (uint16_t)v;
}

static void set_status(const char *s) { strlcpy(g_status, s, sizeof(g_status)); }

/* server-side: send a line to all clients */
static void broadcast(const char *line)
{
    uint8_t i;
    uint16_t len = (uint16_t)strlen(line);
    for (i = 0; i < g_client_count; i++) {
        if (g_clients[i] >= 0) {
            monios_socket_tcp_send(g_clients[i], line, len);
        }
    }
}

/* server-side: build user list packet and broadcast */
static void broadcast_users(void)
{
    char pkt[384];
    uint32_t n = 0;
    uint8_t i;
    pkt[n++] = '!';
    for (i = 0; i < g_user_count; i++) {
        uint32_t j = 0;
        while (g_users[i][j] != '\0' && n + 1 < sizeof(pkt) - 2) {
            pkt[n++] = g_users[i][j++];
        }
        if (i + 1 < g_user_count) pkt[n++] = ',';
    }
    pkt[n++] = '\n';
    pkt[n] = '\0';
    broadcast(pkt);
}

static void add_user(const char *name)
{
    uint8_t i;
    for (i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i], name) == 0) return;
    }
    if (g_user_count < 16) {
        strlcpy(g_users[g_user_count], name, sizeof(g_users[0]));
        g_user_count++;
    }
}

/* ---------------- connect / listen ---------------- */
static void stop_all(void)
{
    uint8_t i;
    if (g_sock >= 0) { monios_socket_close(g_sock); g_sock = -1; }
    for (i = 0; i < g_client_count; i++) {
        if (g_clients[i] >= 0) monios_socket_close(g_clients[i]);
        g_clients[i] = -1;
    }
    g_client_count = 0;
    g_mode = MODE_IDLE;
}

static void start_client(void)
{
    int s;
    uint16_t port = parse_port(g_cfg.port);
    stop_all();
    set_status("Connecting...");
    s = monios_socket_tcp_open(0);
    if (s < 0) { set_status("socket open failed"); return; }
    if (monios_socket_tcp_connect(s, g_cfg.host, port) != 0) {
        monios_socket_close(s);
        set_status("connect failed");
        return;
    }
    {
        uint32_t w = 0;
        while (!monios_socket_tcp_connected(s) && w < 3000) {
            app_sleep_ticks(1); w++;
        }
    }
    if (!monios_socket_tcp_connected(s)) {
        monios_socket_close(s);
        set_status("connect timeout");
        return;
    }
    g_sock = s;
    g_mode = MODE_CLIENT;
    {
        char st[96];
        sprintf(st, "Connected to %s:%s as %s", g_cfg.host, g_cfg.port, g_cfg.username);
        set_status(st);
    }
    /* send join */
    {
        char pkt[80];
        sprintf(pkt, "+%s\n", g_cfg.username);
        monios_socket_tcp_send(s, pkt, (uint16_t)strlen(pkt));
    }
    cfg_save();
}

static void start_server(void)
{
    int s;
    uint16_t port = parse_port(g_cfg.port);
    struct { int32_t handle; } req;
    stop_all();
    set_status("Listening...");
    s = monios_socket_tcp_open(port);
    if (s < 0) { set_status("socket open failed"); return; }
    req.handle = s;
    if (syscall3(SYS_SOCKET_CALL, SOCKET_CALL_TCP_LISTEN,
                 (uint64_t)&req, sizeof(req)) != 0) {
        monios_socket_close(s);
        set_status("listen failed");
        return;
    }
    g_sock = s;
    g_mode = MODE_SERVER;
    g_user_count = 0;
    add_user(g_cfg.username);
    {
        char st[96];
        sprintf(st, "Server listening on port %u", (unsigned)port);
        set_status(st);
    }
    hist_add("[server] chat server started");
    cfg_save();
}

/* ---------------- process incoming line ---------------- */
static void process_line(const char *line)
{
    char buf[HIST_LINE];
    strlcpy(buf, line, sizeof(buf));

    if (buf[0] == '+') {
        add_user(buf + 1);
        {
            char s[96];
            sprintf(s, "[+] %s joined", buf + 1);
            hist_add(s);
        }
        if (g_mode == MODE_SERVER) broadcast_users();
    } else if (buf[0] == '-') {
        uint8_t i;
        for (i = 0; i < g_user_count; i++) {
            if (strcmp(g_users[i], buf + 1) == 0) {
                uint8_t j;
                for (j = i; j + 1 < g_user_count; j++) {
                    strlcpy(g_users[j], g_users[j + 1], sizeof(g_users[0]));
                }
                g_user_count--;
                break;
            }
        }
        {
            char s[96];
            sprintf(s, "[-] %s left", buf + 1);
            hist_add(s);
        }
    } else if (buf[0] == '!') {
        /* user list: "!u1,u2,..." */
        char *p = buf + 1;
        g_user_count = 0;
        while (*p != '\0' && g_user_count < 16) {
            char *comma = p;
            while (*comma != '\0' && *comma != ',') comma++;
            if (*comma == ',') { *comma = '\0'; comma++; }
            if (p[0] != '\0') {
                strlcpy(g_users[g_user_count], p, sizeof(g_users[0]));
                g_user_count++;
            }
            p = comma;
        }
    } else if (buf[0] == '@') {
        /* private: "@target:sender:message" */
        char *colon = strchr(buf, ':');
        if (colon != 0) {
            char *sender = strchr(colon + 1, ':');
            if (sender != 0) {
                *sender = '\0';
                {
                    char s[HIST_LINE];
                    sprintf(s, "[PM] %s: %s", colon + 1, sender + 1);
                    hist_add(s);
                }
            }
        }
    } else {
        /* public: "user:message" */
        char *colon = strchr(buf, ':');
        if (colon != 0) {
            *colon = '\0';
            {
                char s[HIST_LINE];
                sprintf(s, "%s: %s", buf, colon + 1);
                hist_add(s);
            }
        }
    }
}

/* ---------------- poll sockets ---------------- */
static void poll_network(void)
{
    uint8_t i;
    char buf[256];

    if (g_mode == MODE_CLIENT) {
        if (g_sock >= 0 && monios_socket_tcp_has_data(g_sock)) {
            int n = monios_socket_tcp_recv(g_sock, buf, sizeof(buf) - 1u);
            if (n > 0) {
                buf[n] = '\0';
                /* split by \n */
                char *p = buf;
                while (*p != '\0') {
                    char *nl = p;
                    while (*nl != '\0' && *nl != '\n') nl++;
                    if (*nl == '\n') { *nl = '\0'; nl++; }
                    if (p[0] != '\0') process_line(p);
                    p = nl;
                }
            }
        }
    } else if (g_mode == MODE_SERVER) {
        /* accept new clients */
        if (g_sock >= 0 && g_client_count < MAX_CLIENTS) {
            struct {
                int32_t handle;
                int32_t client;
                char client_ip[16];
                uint16_t client_port;
            } req;
            req.handle = g_sock;
            req.client = -1;
            req.client_ip[0] = '\0';
            req.client_port = 0;
            if (syscall3(SYS_SOCKET_CALL, SOCKET_CALL_TCP_ACCEPT,
                         (uint64_t)&req, sizeof(req)) == 0) {
                g_clients[g_client_count] = req.client;
                strlcpy(g_client_ip[g_client_count], req.client_ip, 16);
                g_client_count++;
            }
        }
        /* read from each client */
        for (i = 0; i < g_client_count; i++) {
            if (g_clients[i] < 0) continue;
            if (!monios_socket_tcp_has_data(g_clients[i])) continue;
            {
                int n = monios_socket_tcp_recv(g_clients[i], buf, sizeof(buf) - 1u);
                if (n <= 0) {
                    /* disconnect */
                    monios_socket_close(g_clients[i]);
                    g_clients[i] = -1;
                    continue;
                }
                buf[n] = '\0';
                {
                    char *p = buf;
                    while (*p != '\0') {
                        char *nl = p;
                        while (*nl != '\0' && *nl != '\n') nl++;
                        if (*nl == '\n') { *nl = '\0'; nl++; }
                        if (p[0] == '\0') { p = nl; continue; }
                        /* relay to all OTHER clients + echo locally */
                        if (p[0] == '+' || p[0] == '-') {
                            /* join/part: broadcast */
                            broadcast(p);
                            process_line(p);
                        } else if (p[0] == '@') {
                            /* private: "@target:sender:msg" -> relay to target */
                            char *colon = strchr(p, ':');
                            if (colon != 0) {
                                *colon = '\0';
                                const char *target = p + 1;
                                /* find which client fd matches target */
                                uint8_t j;
                                for (j = 0; j < g_client_count; j++) {
                                    if (g_clients[j] >= 0) {
                                        /* we don't track per-client names here;
                                           just relay to all (simple approach) */
                                        char out[256];
                                        sprintf(out, "@%s%s\n", target, colon);
                                        monios_socket_tcp_send(g_clients[j], out, (uint16_t)strlen(out));
                                    }
                                }
                                process_line(p);
                            }
                        } else {
                            /* public: "user:msg" -> broadcast */
                            char out[HIST_LINE];
                            sprintf(out, "%s\n", p);
                            broadcast(out);
                            process_line(p);
                        }
                        p = nl;
                    }
                }
            }
        }
    }
}

/* ---------------- send message ---------------- */
static void send_input(void)
{
    char pkt[256];
    if (g_input_n == 0) return;

    if (g_mode == MODE_CLIENT && g_sock >= 0) {
        /* private message? */
        if (g_input[0] == '@') {
            /* "@target message" -> "@target:sender:message\n" */
            char *sp = strchr(g_input, ' ');
            if (sp != 0) {
                *sp = '\0';
                sprintf(pkt, "@%s:%s:%s\n", g_input + 1, g_cfg.username, sp + 1);
            } else {
                sprintf(pkt, "%s:%s\n", g_cfg.username, g_input);
            }
        } else {
            sprintf(pkt, "%s:%s\n", g_cfg.username, g_input);
        }
        monios_socket_tcp_send(g_sock, pkt, (uint16_t)strlen(pkt));
        /* echo locally */
        {
            char s[HIST_LINE];
            sprintf(s, "%s: %s", g_cfg.username, g_input);
            hist_add(s);
        }
    } else if (g_mode == MODE_SERVER) {
        /* server broadcasts its own message */
        char s[HIST_LINE];
        sprintf(s, "%s: %s", g_cfg.username, g_input);
        hist_add(s);
        {
            char out[HIST_LINE];
            sprintf(out, "%s\n", s);
            broadcast(out);
        }
    } else {
        hist_add("[not connected]");
    }

    g_input[0] = '\0';
    g_input_n = 0;
}

/* ---------------- rendering ---------------- */
static void render(void)
{
    app_mouse_snapshot_t mouse;
    app_get_mouse(&mouse);

    app_graphics_fill_rect(0, 0, 1024, 768, 0x00E5E7EBu);

    /* top bar */
    app_graphics_fill_rect(0, 0, 1024, 36, 0x001E3A8Au);
    app_graphics_draw_text(8, 10, "Chat", 0x00FFFFFFu);
    app_graphics_draw_text(60, 10, g_status, 0x00BFDBFEu);

    /* connect/listen/disconnect button */
    {
        uint16_t bx = 820, by = 4, bw = 90, bh = 28;
        const char *lbl;
        bool hov = mouse.x_pixels >= bx && mouse.x_pixels < bx + bw &&
                   mouse.y_pixels >= by && mouse.y_pixels < by + bh;
        if (g_mode == MODE_IDLE) lbl = "Connect";
        else if (g_mode == MODE_SERVER) lbl = "Stop";
        else lbl = "Disconnect";
        app_graphics_fill_rect(bx, by, bw, bh, hov ? 0x002563EBu : 0x001D4ED8u);
        app_graphics_draw_text((uint16_t)(bx + 10u), (uint16_t)(by + 6u), lbl, 0x00FFFFFFu);

        /* server toggle button */
        bx = 920;
        if (g_mode == MODE_IDLE) lbl = "Listen";
        else lbl = "";
        if (lbl[0] != '\0') {
            hov = mouse.x_pixels >= bx && mouse.x_pixels < bx + bw &&
                  mouse.y_pixels >= by && mouse.y_pixels < by + bh;
            app_graphics_fill_rect(bx, by, bw, bh, hov ? 0x00047857u : 0x0005969Cu);
            app_graphics_draw_text((uint16_t)(bx + 10u), (uint16_t)(by + 6u), lbl, 0x00FFFFFFu);
        }
    }

    /* left: user list */
    app_graphics_fill_rect(0, 36, 180, 680, 0x00F8FAFCu);
    app_graphics_draw_text(10, 44, "Users", 0x00475569u);
    {
        uint8_t i;
        for (i = 0; i < g_user_count && i < 20; i++) {
            app_graphics_draw_text(10, (uint16_t)(64u + i * 16u), g_users[i], 0x001E40AFu);
        }
        if (g_user_count == 0) {
            app_graphics_draw_text(10, 64, "(no users)", 0x0094A3B8u);
        }
    }

    /* right: message history */
    app_graphics_fill_rect(180, 36, 844, 640, 0x00FFFFFFu);
    {
        uint32_t shown = 0;
        uint32_t start;
        uint32_t i;
        if (g_hist_n == 0) {
            app_graphics_draw_text(400, 300, "No messages yet", 0x0094A3B8u);
        } else {
            start = g_hist_n > 40 ? g_hist_n - 40 : 0;
            for (i = start; i < g_hist_n && shown < 40; i++) {
                const char *line = g_history[i % MAX_HISTORY];
                app_graphics_draw_text(190, (uint16_t)(44u + shown * 15u), line, 0x00111827u);
                shown++;
            }
        }
    }

    /* input box */
    app_graphics_fill_rect(180, 680, 760, 36, g_input_focus ? 0x00FFF7CCu : 0x00FFFFFFu);
    app_graphics_draw_text(190, 690, g_input, 0x00111827u);
    /* send button */
    {
        bool hov = mouse.x_pixels >= 950 && mouse.x_pixels < 1020 &&
                   mouse.y_pixels >= 680 && mouse.y_pixels < 716;
        app_graphics_fill_rect(950, 680, 70, 36, hov ? 0x0016A34Au : 0x0015803Du);
        app_graphics_draw_text(965, 692, "Send", 0x00FFFFFFu);
    }

    /* bottom hint */
    app_graphics_draw_text(10, 724, "Type message, Enter to send. @user msg = private. Esc: quit", 0x0064748Bu);

    app_graphics_present();
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    app_enter_graphics_mode();
    cfg_load();
    set_status("Ready. Click Connect or Listen.");
    g_input_focus = true;

    for (;;) {
        app_key_event_t ev;
        app_mouse_snapshot_t mouse;
        static uint8_t prev_buttons = 0;

        while (syscall1(SYS_KEYBOARD_READ_EVENT, (uint64_t)&ev) == 1) {
            if (ev.type == EV_ESC) {
                stop_all();
                app_exit(0);
            }
            if (ev.type == EV_ENTER) {
                send_input();
            } else if (ev.type == EV_CHAR) {
                if (ev.ch == 8) { /* backspace */
                    if (g_input_n > 0) {
                        g_input_n--;
                        g_input[g_input_n] = '\0';
                    }
                } else if (ev.ch >= 32 && ev.ch < 127 && g_input_n + 1 < sizeof(g_input)) {
                    g_input[g_input_n++] = ev.ch;
                    g_input[g_input_n] = '\0';
                }
            }
        }

        app_get_mouse(&mouse);

        if ((mouse.buttons & 0x01u) && !(prev_buttons & 0x01u)) {
            /* Connect button */
            if (mouse.x_pixels >= 820 && mouse.x_pixels < 910 &&
                mouse.y_pixels >= 4 && mouse.y_pixels < 32) {
                if (g_mode == MODE_IDLE) start_client();
                else { stop_all(); set_status("Disconnected"); }
            }
            /* Listen button */
            if (mouse.x_pixels >= 920 && mouse.x_pixels < 1010 &&
                mouse.y_pixels >= 4 && mouse.y_pixels < 32 && g_mode == MODE_IDLE) {
                start_server();
            }
            /* Send button */
            if (mouse.x_pixels >= 950 && mouse.x_pixels < 1020 &&
                mouse.y_pixels >= 680 && mouse.y_pixels < 716) {
                send_input();
            }
            /* focus input */
            if (mouse.y_pixels >= 680 && mouse.y_pixels < 716) {
                g_input_focus = true;
            }
        }
        prev_buttons = mouse.buttons;

        poll_network();
        render();
        app_sleep_ticks(2);
    }
    return 0;
}
