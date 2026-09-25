/*
 * minesweeper.c -- Monios Minesweeper (Task 20c).
 *
 *   1 / 2 / 3   pick beginner / intermediate / expert on the title screen
 *   left click  reveal a cell
 *   right click plant / remove a flag
 *   smiley (top) or R restart
 *   Esc quit
 *
 * Flood fill on empty cells, mine counter = mines - flags, elapsed timer.
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "monios_dll.h"
#include "string.h"
#include "stdint.h"
#include "stdbool.h"
#include "syscall.h"

#define EV_CHAR   1
#define EV_ESC    29

typedef struct {
    uint32_t type;
    char ch;
    uint8_t mods;
    uint8_t pad[3];
} app_key_event_t;

#define MAX_COLS 30u
#define MAX_ROWS 16u
#define CELL     30u

typedef struct {
    bool mine;
    bool open;
    bool flag;
    uint8_t count;
} ms_cell_t;

static ms_cell_t g_grid[MAX_ROWS][MAX_COLS];
static uint32_t g_cols;
static uint32_t g_rows;
static uint32_t g_mines;
static bool g_generated;
static bool g_dead;
static bool g_won;
static uint32_t g_flags;
static uint32_t g_open_count;
static uint64_t g_start_tick;
static uint64_t g_end_tick;
static uint32_t g_rng;
static uint8_t g_surprised; /* 0 idle, 1 mouse down on grid */

enum { ST_TITLE, ST_PLAY };
static uint32_t g_state;

static uint16_t g_grid_x;
static uint16_t g_grid_y;

static const uint32_t g_num_colors[9] = {
    0x00000000u,
    0x002020F0u, /* 1 blue   */
    0x00208020u, /* 2 green  */
    0x00F02020u, /* 3 red    */
    0x00202080u, /* 4 navy   */
    0x00802020u, /* 5 maroon */
    0x00208080u, /* 6 teal   */
    0x00202020u, /* 7 black  */
    0x00808080u  /* 8 gray   */
};

static uint32_t rand_u32(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static void draw_uint(uint16_t x, uint16_t y, uint32_t v, uint32_t color)
{
    char buf[12];
    int i = 0;

    if (v == 0) {
        app_graphics_draw_text(x, y, "0", color);
        return;
    }
    while (v > 0 && i < (int) sizeof(buf) - 1) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    buf[i] = '\0';
    for (int a = 0, b = i - 1; a < b; a++, b--) {
        char t = buf[a];
        buf[a] = buf[b];
        buf[b] = t;
    }
    app_graphics_draw_text(x, y, buf, color);
}

static void configure(uint32_t level)
{
    if (level == 1) { g_cols = 9;  g_rows = 9;  g_mines = 10; }
    else if (level == 2) { g_cols = 16; g_rows = 16; g_mines = 40; }
    else { g_cols = 30; g_rows = 16; g_mines = 99; }

    g_grid_x = (uint16_t)((1024u - g_cols * CELL) / 2u);
    g_grid_y = 70;
}

static void new_game(void)
{
    memset(g_grid, 0, sizeof(g_grid));
    g_generated = false;
    g_dead = false;
    g_won = false;
    g_flags = 0;
    g_open_count = 0;
    g_surprised = 0;
    g_start_tick = app_ticks();
    g_end_tick = 0;
    g_state = ST_PLAY;
}

static void generate_mines(uint32_t safe_r, uint32_t safe_c)
{
    uint32_t placed = 0;

    while (placed < g_mines) {
        uint32_t r = rand_u32() % g_rows;
        uint32_t c = rand_u32() % g_cols;

        if (g_grid[r][c].mine) {
            continue;
        }
        /* keep the first click and its neighbours safe */
        if (r >= safe_r - 1 && r <= safe_r + 1 &&
            c >= safe_c - 1 && c <= safe_c + 1) {
            continue;
        }
        g_grid[r][c].mine = true;
        placed++;
    }
    for (uint32_t r = 0; r < g_rows; r++) {
        for (uint32_t c = 0; c < g_cols; c++) {
            uint8_t n = 0;

            for (int dr = -1; dr <= 1; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                    int rr = (int) r + dr;
                    int cc = (int) c + dc;

                    if (rr >= 0 && rr < (int) g_rows &&
                        cc >= 0 && cc < (int) g_cols &&
                        g_grid[rr][cc].mine) {
                        n++;
                    }
                }
            }
            g_grid[r][c].count = n;
        }
    }
    g_generated = true;
}

/* iterative flood fill via a tiny stack */
static void open_cell(uint32_t r, uint32_t c)
{
    uint32_t stack_r[256];
    uint32_t stack_c[256];
    int sp = 0;

    stack_r[sp] = r;
    stack_c[sp] = c;
    sp++;
    while (sp > 0) {
        uint32_t cr;
        uint32_t cc;

        sp--;
        cr = stack_r[sp];
        cc = stack_c[sp];
        if (g_grid[cr][cc].open || g_grid[cr][cc].flag) {
            continue;
        }
        g_grid[cr][cc].open = true;
        g_open_count++;
        if (g_grid[cr][cc].count == 0 && !g_grid[cr][cc].mine) {
            for (int dr = -1; dr <= 1; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                    int rr = (int) cr + dr;
                    int cc2 = (int) cc + dc;

                    if (rr >= 0 && rr < (int) g_rows &&
                        cc2 >= 0 && cc2 < (int) g_cols &&
                        !g_grid[rr][cc2].open &&
                        !g_grid[rr][cc2].flag &&
                        sp < 256) {
                        stack_r[sp] = (uint32_t) rr;
                        stack_c[sp] = (uint32_t) cc2;
                        sp++;
                    }
                }
            }
        }
    }
}

static void check_win(void)
{
    if (g_open_count >= g_rows * g_cols - g_mines) {
        g_won = true;
        g_end_tick = app_ticks();
    }
}

static void reveal_all_mines(void)
{
    for (uint32_t r = 0; r < g_rows; r++) {
        for (uint32_t c = 0; c < g_cols; c++) {
            if (g_grid[r][c].mine) {
                g_grid[r][c].open = true;
            }
        }
    }
}

static void draw_face(uint16_t cx, uint16_t cy)
{
    uint32_t face = g_won ? 0x0022C55Eu : g_dead ? 0x00EF4444u
                  : g_surprised ? 0x00FACC15u : 0x00FDE047u;

    app_graphics_fill_rect(cx, cy, 40, 40, face);
    app_graphics_draw_text((uint16_t)(cx + 10u), (uint16_t)(cy + 10u),
                          g_won ? ":)" : g_dead ? ":(" : g_surprised ? "o" : ":|",
                          0x00111827);
}

static void render(void)
{
    app_graphics_fill_rect(0, 0, 1024, 768, 0x00CBD5E1);

    if (g_state == ST_TITLE) {
        app_graphics_draw_text(430, 200, "MINESWEEPER", 0x000F172A);
        app_graphics_draw_text(360, 280, "Press 1 = Beginner 9x9  (10 mines)", 0x001E293B);
        app_graphics_draw_text(360, 320, "Press 2 = Intermediate 16x16 (40 mines)", 0x001E293B);
        app_graphics_draw_text(360, 360, "Press 3 = Expert 30x16 (99 mines)", 0x001E293B);
        app_graphics_draw_text(360, 420, "Left click: open   Right click: flag", 0x00475569);
        app_graphics_draw_text(360, 460, "R: restart   Esc: quit", 0x00475569);
        app_graphics_present();
        return;
    }

    /* top status bar */
    app_graphics_fill_rect(g_grid_x, 16, (uint16_t)(g_cols * CELL), 40, 0x00E2E8F0);
    {
        uint32_t remaining = (g_mines >= g_flags) ? (g_mines - g_flags) : 0;

        app_graphics_draw_text((uint16_t)(g_grid_x + 8u), 28, "MINES", 0x0064748B);
        draw_uint((uint16_t)(g_grid_x + 60u), 28, remaining, 0x00DC2626);
        draw_face((uint16_t)(g_grid_x + g_cols * CELL / 2u - 20u), 16);
        {
            uint64_t end = g_end_tick ? g_end_tick : app_ticks();
            uint32_t secs = (uint32_t)((end - g_start_tick) / 100u);

            app_graphics_draw_text((uint16_t)(g_grid_x + g_cols * CELL - 90u), 28,
                                   "TIME", 0x0064748B);
            draw_uint((uint16_t)(g_grid_x + g_cols * CELL - 36u), 28, secs, 0x001D4ED8);
        }
    }

    /* grid */
    for (uint32_t r = 0; r < g_rows; r++) {
        for (uint32_t c = 0; c < g_cols; c++) {
            uint16_t px = (uint16_t)(g_grid_x + c * CELL);
            uint16_t py = (uint16_t)(g_grid_y + r * CELL);
            ms_cell_t *cell = &g_grid[r][c];

            if (!cell->open) {
                app_graphics_fill_rect(px, py, CELL, CELL, 0x00E2E8F0);
                app_graphics_fill_rect(px, py, CELL, 3, 0x00FFFFFF);
                app_graphics_fill_rect(px, py, 3, CELL, 0x00FFFFFF);
                if (cell->flag) {
                    app_graphics_fill_rect((uint16_t)(px + 10u), (uint16_t)(py + 6u), 6, 16, 0x00DC2626);
                    app_graphics_fill_rect((uint16_t)(px + 10u), (uint16_t)(py + 6u), 12, 6, 0x00F97316u);
                }
            } else {
                app_graphics_fill_rect(px, py, CELL, CELL,
                                       cell->mine ? 0x00EF4444u : 0x00F1F5F9u);
                if (cell->mine) {
                    app_graphics_fill_rect((uint16_t)(px + 10u), (uint16_t)(py + 10u), 10, 10, 0x00111827u);
                } else if (cell->count > 0) {
                    char d[2] = { (char)('0' + cell->count), 0 };
                    app_graphics_draw_text((uint16_t)(px + 10u), (uint16_t)(py + 8u),
                                           d, g_num_colors[cell->count]);
                }
            }
        }
    }
    if (g_won) {
        app_graphics_draw_text((uint16_t)(g_grid_x + g_cols * CELL / 2u - 40u),
                               (uint16_t)(g_grid_y + g_rows * CELL + 20u),
                               "YOU WIN! R=restart", 0x00166534);
    }
    app_graphics_present();
}

int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    app_enter_graphics_mode();
    g_rng = (uint32_t) app_ticks() ^ 0xABCDEF01u;
    g_state = ST_TITLE;

    for (;;) {
        app_key_event_t ev;
        app_mouse_snapshot_t mouse;
        static uint8_t prev_buttons = 0;

        while (syscall1(SYS_KEYBOARD_READ_EVENT, (uint64_t) &ev) == 1) {
            if (ev.type == EV_ESC) {
                app_exit(0);
            }
            if (g_state == ST_TITLE) {
                if (ev.type == EV_CHAR && ev.ch >= '1' && ev.ch <= '3') {
                    configure((uint32_t)(ev.ch - '0'));
                    new_game();
                }
            } else if (ev.type == EV_CHAR && (ev.ch == 'r' || ev.ch == 'R')) {
                new_game();
            }
        }

        app_get_mouse(&mouse);
        if (g_state == ST_PLAY && !g_dead && !g_won) {
            g_surprised = (mouse.buttons & 0x01u) ? 1u : 0u;

            /* smiley hit = restart */
            {
                uint16_t fx = (uint16_t)(g_grid_x + g_cols * CELL / 2u - 20u);

                if ((mouse.buttons & 0x01u) && !(prev_buttons & 0x01u) &&
                    mouse.x_pixels >= fx && mouse.x_pixels < fx + 40 &&
                    mouse.y_pixels >= 16 && mouse.y_pixels < 56) {
                    new_game();
                }
            }

            if (!(mouse.buttons & 0x01u) && (prev_buttons & 0x01u)) {
                /* left release: open cell */
                uint32_t c = (uint32_t)(mouse.x_pixels - g_grid_x) / CELL;
                uint32_t r = (uint32_t)(mouse.y_pixels - g_grid_y) / CELL;

                if (r < g_rows && c < g_cols) {
                    if (!g_generated) {
                        generate_mines(r, c);
                    }
                    if (!g_grid[r][c].flag) {
                        if (g_grid[r][c].mine) {
                            g_dead = true;
                            g_end_tick = app_ticks();
                            reveal_all_mines();
                        } else {
                            open_cell(r, c);
                            check_win();
                        }
                    }
                }
            }
            if (!(mouse.buttons & 0x02u) && (prev_buttons & 0x02u)) {
                /* right release: toggle flag */
                uint32_t c = (uint32_t)(mouse.x_pixels - g_grid_x) / CELL;
                uint32_t r = (uint32_t)(mouse.y_pixels - g_grid_y) / CELL;

                if (r < g_rows && c < g_cols && !g_grid[r][c].open) {
                    g_grid[r][c].flag = !g_grid[r][c].flag;
                    g_flags += g_grid[r][c].flag ? 1u : (uint32_t)-1u;
                }
            }
        }
        prev_buttons = mouse.buttons;

        render();
        app_sleep_ticks(2);
    }
    return 0;
}
