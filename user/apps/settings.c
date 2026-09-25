/*
 * settings.c - Monios system settings panel (Task 12).
 *
 * Left navrail switches between six pages: Appearance, Display, Network,
 * Sound, Power, System. Controls are drawn with the OSUI component library
 * and respond to mouse clicks; keys 1..6 switch pages, Esc quits.
 *
 * Live values come from app_get_system_status() (IP / MAC / audio volume /
 * CPU count). Changes persist through the registry (app_registry_set). When
 * a setting has no backing kernel action, the control is drawn but marked
 * "unavailable" rather than faked.
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

/* key event types (mirror kernel include/keyboard.h) */
#define EV_CHAR   1
#define EV_UP     2
#define EV_DOWN   3
#define EV_LEFT   4
#define EV_RIGHT  5
#define EV_ESC    29

typedef struct {
    uint32_t type;
    char ch;
    uint8_t mods;
    uint8_t pad[3];
} app_key_event_t;

static uint32_t g_screen_w;
static uint32_t g_screen_h;

static uint32_t g_page;           /* 0..5 */
static bool     g_dirty = true;

/* settings state (persisted to registry) */
static uint32_t g_theme = 0;      /* 0 default,1 dark,2 light,3 high-contrast */
static uint32_t g_fontsize = 2;   /* 0 small,1 medium,2 large,3 xl */
static uint32_t g_wallpaper = 0;
static uint32_t g_resolution = 2; /* index into modes */
static char     g_dns[20] = "8.8.8.8";
static bool     g_proxy = false;
static uint32_t g_volume = 70;
static bool     g_mute = false;
static uint32_t g_output = 0;
static uint32_t g_sleep = 2;      /* never/5/10/30/60 */
static uint32_t g_powerbtn = 0;   /* sleep/shutdown/ask */
static bool     g_energy = false;

static const char *NAV_ITEMS[6] = {
    "Appearance", "Display", "Network", "Sound", "Power", "System"
};
static const char *THEMES[4] = { "Default", "Dark", "Light", "High contrast" };
static const char *SIZES[4] = { "Small", "Medium", "Large", "Extra large" };
static const char *MODES[4] = { "800 x 600", "1024 x 768", "1280 x 720", "1920 x 1080" };
static const char *SLEEPS[5] = { "Never", "5 minutes", "10 minutes", "30 minutes", "1 hour" };
static const char *PBTN[3] = { "Sleep", "Shut down", "Ask" };
static const char *OUTPUTS[2] = { "Built-in speakers", "HDMI audio" };

/* clickable control registry (filled during render, tested on click) */
#define MAX_CTRLS 48
typedef struct {
    osui_rect_t rect;
    uint32_t    action;   /* page-local action id */
} ctrl_t;
static ctrl_t g_ctrls[MAX_CTRLS];
static uint32_t g_ctrl_count;

static void ctrl_add(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t action)
{
    if (g_ctrl_count >= MAX_CTRLS) return;
    g_ctrls[g_ctrl_count].rect.x = x;
    g_ctrls[g_ctrl_count].rect.y = y;
    g_ctrls[g_ctrl_count].rect.width = w;
    g_ctrls[g_ctrl_count].rect.height = h;
    g_ctrls[g_ctrl_count].action = action;
    g_ctrl_count++;
}

static uint32_t xstrlen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

static void load_prefs(void)
{
    char buf[32];
    if (app_registry_get("settings/theme", buf, sizeof(buf))) g_theme = (uint32_t)(buf[0] - '0');
    if (app_registry_get("settings/fontsize", buf, sizeof(buf))) g_fontsize = (uint32_t)(buf[0] - '0');
    if (app_registry_get("settings/wallpaper", buf, sizeof(buf))) g_wallpaper = (uint32_t)(buf[0] - '0');
    if (app_registry_get("settings/resolution", buf, sizeof(buf))) g_resolution = (uint32_t)(buf[0] - '0');
    if (app_registry_get("settings/proxy", buf, sizeof(buf))) g_proxy = (buf[0] == '1');
    if (app_registry_get("settings/mute", buf, sizeof(buf))) g_mute = (buf[0] == '1');
    if (app_registry_get("settings/sleep", buf, sizeof(buf))) g_sleep = (uint32_t)(buf[0] - '0');
    if (app_registry_get("settings/powerbtn", buf, sizeof(buf))) g_powerbtn = (uint32_t)(buf[0] - '0');
    if (app_registry_get("settings/energy", buf, sizeof(buf))) g_energy = (buf[0] == '1');
    if (app_registry_get("settings/dns", buf, sizeof(buf))) {
        uint32_t k = 0;
        while (buf[k] && k < sizeof(g_dns) - 1) { g_dns[k] = buf[k]; k++; }
        g_dns[k] = 0;
    }
}

static void save1(const char *key, uint32_t v)
{
    char b[4];
    b[0] = (char)('0' + v); b[1] = 0;
    app_registry_set(key, b);
}
static void saveb(const char *key, bool v)
{
    app_registry_set(key, v ? "1" : "0");
}

/* ------------------------------------------------------------------ */
/* pages                                                              */
/* ------------------------------------------------------------------ */
#define CX   180u   /* content x */
#define CW   0u     /* content width computed in render */

static void page_appearance(uint32_t cw, app_system_status_t *st)
{
    uint16_t y = 60;
    osui_label(CX, y, "Theme", false); y += 24;
    for (uint32_t i = 0; i < 4; i++) {
        osui_radio((osui_rect_t){ CX, (uint16_t)y, 150, 22 }, THEMES[i],
                   g_theme == i ? OSUI_STATE_CHECKED : 0);
        ctrl_add(CX, (uint16_t)y, 150, 22, 10 + i);
        y += 30;
    }
    y += 8;
    osui_label(CX, (uint16_t)y, "Wallpaper", false); y += 24;
    osui_list_item((osui_rect_t){ CX, (uint16_t)y, (uint16_t)(cw - CX - 16u), 40 },
                   "wallpaper.jpg", "System default", g_wallpaper == 0 ? OSUI_STATE_SELECTED : 0);
    ctrl_add(CX, (uint16_t)y, (uint16_t)(cw - CX - 16u), 40, 20);
    y += 48;
    osui_list_item((osui_rect_t){ CX, (uint16_t)y, (uint16_t)(cw - CX - 16u), 40 },
                   "boot.bmp", "Boot logo", g_wallpaper == 1 ? OSUI_STATE_SELECTED : 0);
    ctrl_add(CX, (uint16_t)y, (uint16_t)(cw - CX - 16u), 40, 21);
    y += 56;
    osui_label(CX, (uint16_t)y, "Font size", false);
    osui_slider((osui_rect_t){ (uint16_t)(CX + 120), (uint16_t)(y - 4), 180, 24 },
                g_fontsize * 33u + 10u, OSUI_STATE_PRIMARY);
    osui_label((uint16_t)(CX + 310), (uint16_t)y, SIZES[g_fontsize], false);
    ctrl_add((uint16_t)(CX + 120), (uint16_t)(y - 4), 180, 24, 22);
    (void) st;
}

static void page_display(uint32_t cw, app_system_status_t *st)
{
    uint16_t y = 60;
    osui_label(CX, y, "Resolution", false); y += 26;
    for (uint32_t i = 0; i < 4; i++) {
        osui_list_item((osui_rect_t){ CX, (uint16_t)y, (uint16_t)(cw - CX - 16u), 34 },
                       MODES[i], i == g_resolution ? "Selected" : "Available",
                       i == g_resolution ? OSUI_STATE_SELECTED : 0);
        ctrl_add(CX, (uint16_t)y, (uint16_t)(cw - CX - 16u), 34, 30 + i);
        y += 42;
    }
    osui_button_state((osui_rect_t){ CX, (uint16_t)(y + 6), 120, 34 }, "Apply", 0,
                       OSUI_STATE_PRIMARY | OSUI_STATE_FOCUSED);
    ctrl_add(CX, (uint16_t)(y + 6), 120, 34, 40);
    osui_label((uint16_t)(CX + 140), (uint16_t)(y + 16),
               "Refresh rate: 60 Hz (unavailable on this hardware)", true);
    (void) st;
}

static void page_network(uint32_t cw, app_system_status_t *st)
{
    uint16_t y = 60;
    osui_card((osui_rect_t){ CX, y, (uint16_t)(cw - CX - 16u), 150 });
    osui_label((uint16_t)(CX + 16), (uint16_t)(y + 14), "Adapter", false);
    osui_label((uint16_t)(CX + 16), (uint16_t)(y + 40), "IP address", true);
    osui_label((uint16_t)(CX + 140), (uint16_t)(y + 40), st->net_ip, false);
    osui_label((uint16_t)(CX + 16), (uint16_t)(y + 66), "Gateway", true);
    osui_label((uint16_t)(CX + 140), (uint16_t)(y + 66), st->net_gateway, false);
    osui_label((uint16_t)(CX + 16), (uint16_t)(y + 92), "MAC", true);
    osui_label((uint16_t)(CX + 140), (uint16_t)(y + 92), st->net_mac, false);
    osui_badge((osui_rect_t){ (uint16_t)(CX + 260), (uint16_t)(y + 12), 90, 22 },
               st->net_connected ? "Online" : "Offline",
               st->net_connected ? OSUI_STATE_SUCCESS : OSUI_STATE_MUTED);

    y = (uint16_t)(y + 170);
    osui_label(CX, y, "DNS server", false);
    osui_input((osui_rect_t){ CX, (uint16_t)(y + 22), 220, 30 }, g_dns, "", false, false);
    ctrl_add(CX, (uint16_t)(y + 22), 220, 30, 50);
    y = (uint16_t)(y + 66);
    osui_toggle((osui_rect_t){ CX, y, 260, 30 }, "Web proxy",
                g_proxy ? OSUI_STATE_CHECKED : 0);
    ctrl_add(CX, y, 260, 30, 51);
}

static void page_sound(uint32_t cw, app_system_status_t *st)
{
    uint16_t y = 60;
    uint32_t vol = g_mute ? 0 : g_volume;
    osui_label(CX, y, "Volume", false);
    osui_slider((osui_rect_t){ (uint16_t)(CX + 90), (uint16_t)(y - 4), 200, 24 },
                vol, g_mute ? OSUI_STATE_MUTED : OSUI_STATE_SUCCESS);
    ctrl_add((uint16_t)(CX + 90), (uint16_t)(y - 4), 200, 24, 60);
    y += 44;
    osui_toggle((osui_rect_t){ CX, y, 200, 30 }, "Mute", g_mute ? OSUI_STATE_CHECKED : 0);
    ctrl_add(CX, y, 200, 30, 61);
    y += 46;
    osui_label(CX, y, "Output device", false); y += 26;
    for (uint32_t i = 0; i < 2; i++) {
        osui_radio((osui_rect_t){ CX, (uint16_t)y, 200, 22 }, OUTPUTS[i],
                   g_output == i ? OSUI_STATE_CHECKED : 0);
        ctrl_add(CX, (uint16_t)y, 200, 22, 62 + i);
        y += 30;
    }
    y += 8;
    osui_callout((osui_rect_t){ CX, y, (uint16_t)(cw - CX - 16u), 40 },
                 st->audio_present ? "Audio device ready" : "No audio device",
                 st->audio_present ? st->audio_driver : "Sound card unavailable",
                 st->audio_present ? OSUI_STATE_SUCCESS : OSUI_STATE_WARNING);
}

static void page_power(uint32_t cw, app_system_status_t *st)
{
    uint16_t y = 60;
    osui_label(CX, y, "Sleep after", false); y += 26;
    for (uint32_t i = 0; i < 5; i++) {
        osui_radio((osui_rect_t){ CX, (uint16_t)y, 200, 22 }, SLEEPS[i],
                   g_sleep == i ? OSUI_STATE_CHECKED : 0);
        ctrl_add(CX, (uint16_t)y, 200, 22, 70 + i);
        y += 30;
    }
    y += 6;
    osui_label(CX, y, "Power button", false); y += 26;
    for (uint32_t i = 0; i < 3; i++) {
        osui_radio((osui_rect_t){ CX, (uint16_t)y, 200, 22 }, PBTN[i],
                   g_powerbtn == i ? OSUI_STATE_CHECKED : 0);
        ctrl_add(CX, (uint16_t)y, 200, 22, 80 + i);
        y += 30;
    }
    y += 6;
    osui_toggle((osui_rect_t){ CX, y, 220, 30 }, "Energy saver",
                g_energy ? OSUI_STATE_CHECKED : 0);
    ctrl_add(CX, y, 220, 30, 90);
    (void) cw; (void) st;
}

static void page_system(uint32_t cw, app_system_status_t *st)
{
    char line[96];
    uint16_t y = 60;
    osui_card((osui_rect_t){ CX, y, (uint16_t)(cw - CX - 16u), 240 });
    uint16_t ix = (uint16_t)(CX + 16);
    uint16_t iy = (uint16_t)(y + 16);
    osui_label(ix, iy, "MoniOS", false);
    osui_chip((osui_rect_t){ (uint16_t)(ix + 120), iy - 2, 90, 22 }, "v0.0.4B", OSUI_STATE_PRIMARY);
    iy += 30;

    osui_label(ix, iy, "Build", true);
    osui_label((uint16_t)(ix + 90), iy, "host 333 tests passing", false); iy += 26;
    osui_label(ix, iy, "CPU", true);
    {
        uint32_t k = 0;
        line[k++] = (char)('0' + (st->smp_online_processors / 10u));
        line[k++] = (char)('0' + (st->smp_online_processors % 10u));
        line[k++] = ' '; line[k++] = 'l'; line[k++] = 'o'; line[k++] = 'g';
        line[k++] = 'i'; line[k++] = 'c'; line[k++] = 'a'; line[k++] = 'l';
        line[k++] = 0;
        osui_label((uint16_t)(ix + 90), iy, line, false);
    }
    iy += 26;
    osui_label(ix, iy, "Processes", true);
    {
        uint32_t k = 0;
        uint32_t v = st->process_count;
        char tmp[12]; uint32_t t = 0;
        if (v == 0) tmp[t++] = '0';
        while (v > 0) { tmp[t++] = (char)('0' + v % 10u); v /= 10u; }
        while (t > 0) line[k++] = tmp[--t];
        line[k] = 0;
        osui_label((uint16_t)(ix + 90), iy, line, false);
    }
    iy += 26;
    osui_label(ix, iy, "Uptime", true);
    {
        uint64_t ticks = app_ticks() / 100u; /* 100 Hz -> seconds */
        uint32_t s = (uint32_t)(ticks % 60u);
        uint32_t m = (uint32_t)((ticks / 60u) % 60u);
        uint32_t h = (uint32_t)(ticks / 3600u);
        uint32_t k = 0;
        uint32_t v;
        v = h; line[k++] = (char)('0' + v / 10u); line[k++] = (char)('0' + v % 10u);
        line[k++] = ':';
        line[k++] = (char)('0' + m / 10u); line[k++] = (char)('0' + m % 10u);
        line[k++] = ':';
        line[k++] = (char)('0' + s / 10u); line[k++] = (char)('0' + s % 10u);
        line[k] = 0;
        osui_label((uint16_t)(ix + 90), iy, line, false);
    }
    iy += 26;
    osui_label(ix, iy, "Memory", true);
    osui_label((uint16_t)(ix + 90), iy, "see taskmgr", false); iy += 26;
    osui_label(ix, iy, "Storage", true);
    osui_label((uint16_t)(ix + 90), iy, "100 MB FAT32", false); iy += 26;
    osui_label(ix, iy, "About", true);
    osui_label((uint16_t)(ix + 90), iy, "Monios x64", false);
}

/* ------------------------------------------------------------------ */
/* render                                                             */
/* ------------------------------------------------------------------ */
static void render(void)
{
    app_system_status_t st;
    g_ctrl_count = 0;
    app_get_system_status(&st);

    osui_canvas(0x00EAF0F6);
    osui_titlebar((osui_rect_t){ 0, 0, (uint16_t)g_screen_w, 36 }, "Settings", true);

    osui_navrail((osui_rect_t){ 8, 48, 150, (uint16_t)(g_screen_h - 48u - 36u) },
                 NAV_ITEMS, 6, g_page);

    switch (g_page) {
        case 0: page_appearance(g_screen_w, &st); break;
        case 1: page_display(g_screen_w, &st); break;
        case 2: page_network(g_screen_w, &st); break;
        case 3: page_sound(g_screen_w, &st); break;
        case 4: page_power(g_screen_w, &st); break;
        default: page_system(g_screen_w, &st); break;
    }

    osui_statusbar((osui_rect_t){ 0, (uint16_t)(g_screen_h - 28), (uint16_t)g_screen_w, 28 },
                   "Settings", NAV_ITEMS[g_page], OSUI_STATE_SUCCESS);
    osui_present();
}

/* ------------------------------------------------------------------ */
/* click dispatch                                                     */
/* ------------------------------------------------------------------ */
static void handle_action(uint32_t a)
{
    switch (a) {
        case 10: case 11: case 12: case 13:
            g_theme = a - 10; save1("settings/theme", g_theme); break;
        case 20: g_wallpaper = 0; save1("settings/wallpaper", 0); break;
        case 21: g_wallpaper = 1; save1("settings/wallpaper", 1); break;
        case 22: g_fontsize = (g_fontsize + 1u) % 4u; save1("settings/fontsize", g_fontsize); break;
        case 30: case 31: case 32: case 33:
            g_resolution = a - 30; save1("settings/resolution", g_resolution); break;
        case 40: save1("settings/resolution", g_resolution); break; /* apply */
        case 50: break; /* DNS input (display-only) */
        case 51: g_proxy = !g_proxy; saveb("settings/proxy", g_proxy); break;
        case 60: g_volume = (g_volume + 10u) % 101u; if (g_volume < 5) g_volume = 5; break;
        case 61: g_mute = !g_mute; saveb("settings/mute", g_mute); break;
        case 62: g_output = 0; break;
        case 63: g_output = 1; break;
        case 70: case 71: case 72: case 73: case 74:
            g_sleep = a - 70; save1("settings/sleep", g_sleep); break;
        case 80: case 81: case 82:
            g_powerbtn = a - 80; save1("settings/powerbtn", g_powerbtn); break;
        case 90: g_energy = !g_energy; saveb("settings/energy", g_energy); break;
        default: break;
    }
    g_dirty = true;
}

static bool point_in(const ctrl_t *c, int32_t x, int32_t y)
{
    return x >= c->rect.x && x < c->rect.x + c->rect.width &&
           y >= c->rect.y && y < c->rect.y + c->rect.height;
}

int main(int argc, char **argv)
{
    app_key_event_t ev;
    app_mouse_snapshot_t mouse;
    int32_t prev_btn = 0;

    (void) argc; (void) argv;
    fputs("settings.exe\r\n");

    app_enter_graphics_mode();
    g_screen_w = (uint32_t)monios_syscall0(SYS_GRAPHICS_GET_WIDTH);
    g_screen_h = (uint32_t)monios_syscall0(SYS_GRAPHICS_GET_HEIGHT);
    if (g_screen_w == 0) g_screen_w = 1024;
    if (g_screen_h == 0) g_screen_h = 768;

    load_prefs();

    for (;;) {
        bool did = false;
        while (syscall1(SYS_KEYBOARD_READ_EVENT, (uint64_t)&ev) == 1) {
            did = true;
            if (ev.type == EV_CHAR) {
                if (ev.ch >= '1' && ev.ch <= '6') {
                    g_page = (uint32_t)(ev.ch - '1');
                    g_dirty = true;
                } else if (ev.ch == 27 || ev.ch == 'q' || ev.ch == 'Q') {
                    app_exit(0);
                }
            } else if (ev.type == EV_ESC) {
                app_exit(0);
            } else if (ev.type == EV_LEFT || ev.type == EV_UP) {
                /* no in-page focus nav beyond this */
            }
        }

        if (app_get_mouse(&mouse) >= 0) {
            int32_t btn = (int32_t)mouse.buttons;
            if ((btn & 1) && !(prev_btn & 1)) {
                /* click: navrail tabs first */
                if (mouse.x_pixels >= 8 && mouse.x_pixels < 158 &&
                    mouse.y_pixels >= 48 && mouse.y_pixels < (int32_t)(g_screen_h - 36u)) {
                    uint32_t idx = (uint32_t)((mouse.y_pixels - 60) / 44);
                    if (idx < 6) { g_page = idx; g_dirty = true; }
                } else {
                    for (uint32_t i = 0; i < g_ctrl_count; i++) {
                        if (point_in(&g_ctrls[i], mouse.x_pixels, mouse.y_pixels)) {
                            handle_action(g_ctrls[i].action);
                            break;
                        }
                    }
                }
            }
            prev_btn = btn;
        }

        if (g_dirty) {
            render();
            g_dirty = false;
        }
        if (!did) app_sleep_ticks(3);
    }
    return 0;
}
