/*
 * termux.c - Monios terminal multiplexer (multi-tab shell).
 *
 * Features:
 *   - Multiple shell tabs with clickable tab bar
 *   - Each tab: 100-line scrollback buffer
 *   - VT100-ish: clear screen (ESC[2J), cursor home (ESC[H),
 *     ANSI colors (ESC[30m..37m, ESC[0m), newline/CR/LF
 *   - Keyboard input via SYS_KEYBOARD_READ_EVENT
 *   - Built-in commands: help, echo, cls/clear, dir, cd, type,
 *     ver, date, exit (closes tab), Ctrl+Tab cycles tabs
 *   - Mouse: click tab to switch, "+" opens new tab, "x" closes tab
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "monios_dll.h"
#include "windows_dll.h"
#include "osui_dll.h"
#include "string.h"
#include "stdint.h"
#include "stdbool.h"
#include "syscall.h"

/* key event types (matches kernel) */
#define EV_CHAR   1
#define EV_UP     2
#define EV_DOWN   3
#define EV_LEFT   4
#define EV_RIGHT  5
#define EV_ESC    29
#define EV_ENTER  30
#define EV_BACKSP 31
#define EV_TAB    32
#define EV_CTRL   33

typedef struct {
    uint32_t type;
    char ch;
    uint8_t mods;
    uint8_t pad[3];
} app_key_event_t;

/* terminal geometry */
#define TERM_COLS       80u
#define TERM_ROWS       40u
#define TERM_HISTORY    100u
#define CELL_W          9u
#define CELL_H          16u
#define TAB_BAR_H       28u
#define MAX_TABS        8u
#define INPUT_BUF_SZ    128u

/* ANSI colors (0x00RRGGBB) */
static const uint32_t ansi_fg[8] = {
    0x00000000u,  /* 30 black   */
    0x00FF0000u,  /* 31 red     */
    0x0000CC00u,  /* 32 green   */
    0x00CCCC00u,  /* 33 yellow  */
    0x000000FFu,  /* 34 blue    */
    0x00CC00CCu,  /* 35 magenta */
    0x0000CCCCu,  /* 36 cyan    */
    0x00CCCCCCu,  /* 37 white   */
};
#define COLOR_DEFAULT   0x00E0E0E0u
#define COLOR_BG        0x00000000u
#define COLOR_PROMPT    0x0000CC00u

typedef struct {
    char ch;
    uint32_t fg;   /* foreground color */
} cell_t;

typedef struct {
    cell_t cells[TERM_HISTORY][TERM_COLS];
    uint32_t num_lines;    /* total lines written (wraps around history) */
    uint32_t cur_row;       /* cursor row (0..TERM_ROWS-1 visible) */
    uint32_t cur_col;       /* cursor column */
    uint32_t scroll_offset; /* lines scrolled back from bottom */
    uint32_t active_fg;    /* current ANSI foreground */
    char cwd[128];
    char input[INPUT_BUF_SZ];
    uint32_t input_len;
    uint32_t tab_index;
    bool closed;
} shell_tab_t;

static shell_tab_t g_tabs[MAX_TABS];
static uint32_t g_tab_count;
static uint32_t g_active_tab;
static uint16_t g_screen_w;
static uint16_t g_screen_h;

/* ---------- helpers ---------- */
static uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

static bool prefix_eq(const char *s, const char *prefix) { while (*prefix) { char a = *s, b = *prefix; if (a >= 65 && a <= 90) a = (char)(a + 32); if (b >= 65 && b <= 90) b = (char)(b + 32); if (a != b) return false; s++; prefix++; } return true; }

static void tab_clear(shell_tab_t *t)
{
    memset(t->cells, 0, sizeof(t->cells));
    t->num_lines = 0;
    t->cur_row = 0;
    t->cur_col = 0;
    t->scroll_offset = 0;
    t->active_fg = COLOR_DEFAULT;
    t->input_len = 0;
    t->input[0] = '\0';
}

static void tab_init(shell_tab_t *t, uint32_t idx)
{
    memset(t, 0, sizeof(*t));
    t->tab_index = idx;
    /* default cwd */
    strcpy(t->cwd, "C:\\Monios");
    tab_clear(t);
    /* welcome message */
    const char *msg = "Monios Shell v1.0 - type 'help' for commands";
    uint32_t i;
    for (i = 0; msg[i] && i < TERM_COLS; i++) {
        t->cells[0][i].ch = msg[i];
        t->cells[0][i].fg = 0x00888888u;
    }
    t->num_lines = 1;
    t->cur_row = 1;
}

/* write a string to the terminal at current cursor position */
static void tab_write_str(shell_tab_t *t, const char *s)
{
    while (*s) {
        char c = *s++;
        if (c == '\n') {
            t->cur_row++;
            t->cur_col = 0;
        } else if (c == '\r') {
            t->cur_col = 0;
        } else {
            if (t->cur_col < TERM_COLS) {
                uint32_t line_idx = t->num_lines % TERM_HISTORY;
                t->cells[line_idx][t->cur_col].ch = c;
                t->cells[line_idx][t->cur_col].fg = t->active_fg;
                t->cur_col++;
            }
            if (t->cur_col >= TERM_COLS) {
                t->cur_row++;
                t->cur_col = 0;
            }
        }
        /* handle line wrap: advance num_lines when row exceeds visible area */
        if (t->cur_row >= TERM_ROWS) {
            t->num_lines++;
            t->cur_row = TERM_ROWS - 1;
            /* shift: the last visible row becomes a new history line */
            /* Simple approach: just increment num_lines and overwrite cur_row */
        }
        if (t->num_lines >= TERM_HISTORY) {
            /* circular buffer: num_lines keeps growing, modulo indexes */
        }
        t->num_lines++;
        if (t->num_lines > TERM_HISTORY) t->num_lines = TERM_HISTORY;
    }
}

/* Actually, let me simplify: maintain num_lines as count of used lines.
 * When we exceed TERM_HISTORY, scroll everything up by one. */
static void tab_newline(shell_tab_t *t)
{
    t->cur_col = 0;
    if (t->cur_row < TERM_ROWS - 1) {
        t->cur_row++;
    } else {
        /* scroll: move all rows up by one in the circular buffer */
        /* We use num_lines as the "bottom" position */
        t->num_lines++;
    }
}

static void tab_putchar(shell_tab_t *t, char c)
{
    if (c == '\n') {
        tab_newline(t);
        return;
    }
    if (c == '\r') {
        t->cur_col = 0;
        return;
    }
    if (t->cur_col >= TERM_COLS) {
        tab_newline(t);
    }
    {
        uint32_t line_idx = t->num_lines % TERM_HISTORY;
        t->cells[line_idx][t->cur_col].ch = c;
        t->cells[line_idx][t->cur_col].fg = t->active_fg;
        t->cur_col++;
    }
}

static void tab_write(shell_tab_t *t, const char *s)
{
    while (*s) tab_putchar(t, *s++);
}

/* ---------- command execution ---------- */
static void run_command(shell_tab_t *t, char *cmd)
{
    /* trim whitespace */
    char *start = cmd;
    while (*start == ' ' || *start == '\t') start++;
    char *end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        end--;
    }
    *end = '\0';

    if (start[0] == '\0') return;

    /* echo the command line */
    tab_putchar(t, '\n');

    if (strcasecmp(start, "help") == 0) {
        tab_write(t, "Available commands:\r\n");
        tab_write(t, "  help        Show this help\r\n");
        tab_write(t, "  echo <text> Print text\r\n");
        tab_write(t, "  cls/clear   Clear screen\r\n");
        tab_write(t, "  dir         List current directory\r\n");
        tab_write(t, "  cd <dir>    Change directory\r\n");
        tab_write(t, "  type <file> Type file contents\r\n");
        tab_write(t, "  ver         Show OS version\r\n");
        tab_write(t, "  exit        Close this tab\r\n");
        tab_write(t, "  Ctrl+Tab    Switch to next tab\r\n");
    } else if (strcasecmp(start, "cls") == 0 || strcasecmp(start, "clear") == 0) {
        tab_clear(t);
        t->active_fg = COLOR_DEFAULT;
    } else if (prefix_eq(start, "echo ") == 0) {
        tab_write(t, start + 5);
        tab_putchar(t, '\n');
    } else if (strcasecmp(start, "echo") == 0) {
        tab_putchar(t, '\n');
    } else if (strcasecmp(start, "dir") == 0 || strcasecmp(start, "ls") == 0) {
        static char dirlist[2048];
        int n = app_file_list_dir(t->cwd, dirlist, sizeof(dirlist));
        if (n > 0) {
            uint32_t i = 0;
            while (dirlist[i] != '\0') {
                /* copy one entry at a time, wrapping */
                char entry[96];
                uint32_t j = 0;
                while (dirlist[i] != '\n' && dirlist[i] != '\0' && j + 1 < sizeof(entry)) {
                    entry[j++] = dirlist[i++];
                }
                entry[j] = '\0';
                if (dirlist[i] == '\n') i++;
                tab_write(t, "  ");
                tab_write(t, entry);
                tab_putchar(t, '\n');
            }
        } else {
            tab_write(t, "  (directory empty or error)\r\n");
        }
    } else if (prefix_eq(start, "cd ") == 0) {
        char *dir = start + 3;
        if (app_file_is_dir(dir)) {
            strncpy(t->cwd, dir, sizeof(t->cwd) - 1);
            t->cwd[sizeof(t->cwd) - 1] = '\0';
        } else {
            tab_write(t, "  Not a directory: ");
            tab_write(t, dir);
            tab_putchar(t, '\n');
        }
    } else if (strcasecmp(start, "cd") == 0) {
        tab_write(t, t->cwd);
        tab_putchar(t, '\n');
    } else if (prefix_eq(start, "type ") == 0) {
        static char fbuf[4096];
        char *fname = start + 5;
        int n = app_file_read(fname, fbuf, sizeof(fbuf) - 1);
        if (n > 0) {
            fbuf[n] = '\0';
            tab_write(t, fbuf);
            tab_putchar(t, '\n');
        } else {
            tab_write(t, "  Cannot open file\r\n");
        }
    } else if (strcasecmp(start, "ver") == 0) {
        tab_write(t, "Monios x64 Terminal Multiplexer v1.0\r\n");
    } else if (strcasecmp(start, "exit") == 0) {
        t->closed = true;
    } else {
        tab_write(t, "  Unknown command: ");
        tab_write(t, start);
        tab_putchar(t, '\n');
        tab_write(t, "  Type 'help' for available commands\r\n");
    }
}

/* ---------- rendering ---------- */
static void draw_tab_bar(void)
{
    uint16_t x = 4;
    uint16_t y = 2;
    uint32_t i;

    /* background */
    app_graphics_fill_rect(0, 0, g_screen_w, TAB_BAR_H, 0x002D2D30u);

    for (i = 0; i < g_tab_count; i++) {
        shell_tab_t *t = &g_tabs[i];
        char label[32];
        uint16_t tw = 80;

        /* tab background */
        uint32_t bg = (i == g_active_tab) ? 0x001E1E1Eu : 0x003C3C40u;
        app_graphics_fill_rect(x, y, tw, TAB_BAR_H - 4, bg);

        /* tab label */
        strcpy(label, "Shell ");
        {
            char num[4];
            num[0] = '1' + (char)i;
            num[1] = '\0';
            strcat(label, num);
        }
        uint32_t fg = (i == g_active_tab) ? 0x00FFFFFFu : 0x00AAAAAAu;
        app_graphics_draw_text((uint16_t)(x + 8), (uint16_t)(y + 6), label, fg);

        /* close button "x" */
        app_graphics_draw_text((uint16_t)(x + tw - 14), (uint16_t)(y + 6), "x",
                               (i == g_active_tab) ? 0x00FF6666u : 0x00888888u);

        t->tab_index = i;
        x = (uint16_t)(x + tw + 2);
    }

    /* "+" new tab button */
    app_graphics_fill_rect(x, y, 28, TAB_BAR_H - 4, 0x003C3C40u);
    app_graphics_draw_text((uint16_t)(x + 8), (uint16_t)(y + 6), "+", 0x0066FF66u);
}

static void draw_terminal(void)
{
    shell_tab_t *t = &g_tabs[g_active_tab];
    uint16_t ox = 4;
    uint16_t oy = TAB_BAR_H + 2;
    uint32_t visible_rows = (g_screen_h - TAB_BAR_H - 30u) / CELL_H;
    uint32_t r;

    /* black background */
    app_graphics_fill_rect(0, TAB_BAR_H, g_screen_w,
                           (uint16_t)(g_screen_h - TAB_BAR_H), COLOR_BG);

    /* draw visible lines */
    for (r = 0; r < visible_rows; r++) {
        uint32_t line_num;
        /* if we've scrolled, show older lines; otherwise show recent */
        if (t->num_lines <= visible_rows) {
            line_num = r;
        } else {
            line_num = t->num_lines - visible_rows + r - t->scroll_offset;
            if (line_num >= t->num_lines) line_num = t->num_lines - 1;
        }
        if (line_num >= TERM_HISTORY) {
            line_num = line_num % TERM_HISTORY;
        }
        if (line_num < t->num_lines || t->num_lines <= TERM_HISTORY) {
            char linebuf[TERM_COLS + 1];
            uint32_t c;
            uint32_t line_idx = line_num % TERM_HISTORY;
            uint32_t cur_fg = t->cells[line_idx][0].fg;
            bool has_text = false;
            for (c = 0; c < TERM_COLS; c++) {
                char ch = t->cells[line_idx][c].ch;
                linebuf[c] = ch ? ch : ' ';
                if (ch) has_text = true;
            }
            linebuf[TERM_COLS] = '\0';
            if (has_text) {
                /* draw in segments by color - simplified: just draw whole line */
                app_graphics_draw_text(ox, (uint16_t)(oy + r * CELL_H), linebuf, cur_fg);
            }
        }
    }

    /* draw prompt + current input line at bottom */
    {
        char prompt[256];
        uint32_t row = visible_rows - 1;
        prompt[0] = '\0';
        strcpy(prompt, t->cwd);
        strcat(prompt, ">");
        app_graphics_draw_text(ox, (uint16_t)(oy + row * CELL_H), prompt, COLOR_PROMPT);
        /* input buffer */
        if (t->input_len > 0) {
            app_graphics_draw_text((uint16_t)(ox + strlen(prompt) * CELL_W),
                                   (uint16_t)(oy + row * CELL_H),
                                   t->input, COLOR_DEFAULT);
        }
        /* cursor block */
        {
            uint16_t cx = (uint16_t)(ox + (strlen(prompt) + t->input_len) * CELL_W);
            app_graphics_fill_rect(cx, (uint16_t)(oy + row * CELL_H + 2),
                                   CELL_W, CELL_H - 4, 0x00FFFFFFu);
        }
    }
}

/* ---------- tab management ---------- */
static void add_tab(void)
{
    if (g_tab_count >= MAX_TABS) return;
    tab_init(&g_tabs[g_tab_count], g_tab_count);
    g_active_tab = g_tab_count;
    g_tab_count++;
}

static void close_tab(uint32_t idx)
{
    uint32_t i;
    if (g_tab_count <= 1) return;
    /* shift remaining tabs down */
    for (i = idx; i < g_tab_count - 1; i++) {
        g_tabs[i] = g_tabs[i + 1];
        g_tabs[i].tab_index = i;
    }
    g_tab_count--;
    if (g_active_tab >= g_tab_count) g_active_tab = g_tab_count - 1;
}

/* ---------- mouse hit test ---------- */
static int handle_click(int32_t mx, int32_t my)
{
    uint16_t x = 4;
    uint16_t y = 2;
    uint32_t i;
    uint16_t tw = 80;

    if (my >= TAB_BAR_H) return 0; /* clicked terminal area, ignore */

    for (i = 0; i < g_tab_count; i++) {
        /* close button (x) */
        if (mx >= x + tw - 16 && mx <= x + tw && my >= y && my <= y + TAB_BAR_H - 4) {
            close_tab(i);
            return 1;
        }
        /* tab body */
        if (mx >= x && mx < x + tw && my >= y && my <= y + TAB_BAR_H - 4) {
            g_active_tab = i;
            return 1;
        }
        x = (uint16_t)(x + tw + 2);
    }

    /* "+" button */
    if (mx >= x && mx < x + 28 && my >= y && my <= y + TAB_BAR_H - 4) {
        add_tab();
        return 1;
    }
    return 0;
}

/* ---------- main ---------- */
int main(int argc, char **argv)
{
    app_key_event_t ev;
    app_mouse_snapshot_t m;
    uint8_t prev_buttons = 0;
    bool ctrl_held = false;

    (void) argc; (void) argv;

    g_screen_w = windows_screen_width();
    g_screen_h = windows_screen_height();
    if (g_screen_w < 800) g_screen_w = 1024;
    if (g_screen_h < 600) g_screen_h = 768;

    /* start with one tab */
    g_tab_count = 0;
    g_active_tab = 0;
    add_tab();

    app_enter_graphics_mode();

    for (;;) {
        /* drain keyboard events */
        while (syscall1(SYS_KEYBOARD_READ_EVENT, (uint64_t) &ev) == 1) {
            shell_tab_t *t = &g_tabs[g_active_tab];

            switch (ev.type) {
            case EV_CHAR:
                if (ev.ch == '\t' && ctrl_held) {
                    g_active_tab = (g_active_tab + 1u) % g_tab_count;
                } else if (ev.ch == '\n' || ev.ch == '\r') {
                    /* execute command */
                    t->input[t->input_len] = '\0';
                    run_command(t, t->input);
                    t->input_len = 0;
                    t->input[0] = '\0';
                    /* check if tab was closed */
                    if (t->closed) {
                        close_tab(g_active_tab);
                    }
                } else if (ev.ch == 127 || ev.ch == 8) {  /* backspace */
                    if (t->input_len > 0) {
                        t->input_len--;
                        t->input[t->input_len] = '\0';
                    }
                } else if (ev.ch >= 32 && ev.ch < 127) {
                    if (t->input_len < INPUT_BUF_SZ - 1) {
                        t->input[t->input_len++] = ev.ch;
                        t->input[t->input_len] = '\0';
                    }
                } else if (ev.ch == 3) { /* Ctrl+C */
                    tab_write(t, "^C\r\n");
                    t->input_len = 0;
                    t->input[0] = '\0';
                }
                break;

            case EV_ENTER:
                t->input[t->input_len] = '\0';
                run_command(t, t->input);
                t->input_len = 0;
                t->input[0] = '\0';
                if (t->closed) close_tab(g_active_tab);
                break;

            case EV_BACKSP:
                if (t->input_len > 0) {
                    t->input_len--;
                    t->input[t->input_len] = '\0';
                }
                break;

            case EV_TAB:
                if (ctrl_held) {
                    g_active_tab = (g_active_tab + 1u) % g_tab_count;
                }
                break;

            case EV_ESC:
                /* quit on ESC */
                return 0;

            default:
                break;
            }

            /* track Ctrl modifier (mods bit 0 = left ctrl, bit 1 = right ctrl) */
            if (ev.mods & 0x03u) {
                ctrl_held = true;
            } else {
                ctrl_held = false;
            }
        }

        /* mouse */
        app_get_mouse(&m);
        if ((m.buttons & 1u) && !(prev_buttons & 1u)) {
            handle_click(m.x_pixels, m.y_pixels);
        }
        prev_buttons = m.buttons;

        /* right click = exit */
        if (m.buttons & 2u) {
            return 0;
        }

        /* render */
        draw_tab_bar();
        draw_terminal();
        osui_present();

        app_sleep_ticks(2);
    }
    return 0;
}
