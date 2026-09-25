/*
 * tetris.c -- Monios classic Tetris (Task 20b).
 *
 *   left/right   move
 *   up           rotate
 *   down         soft drop
 *   space        hard drop
 *   P            pause
 *   R            restart
 *   Esc          quit
 *
 * Board 10x20, next-piece preview, score / level display.
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

#define COLS   10u
#define ROWS   20u
#define CELL   26u

enum { P_I = 0, P_O, P_T, P_S, P_Z, P_J, P_L, P_COUNT };

static uint32_t g_color[P_COUNT] = {
    0x0022D3EEu, /* I cyan   */
    0x00FACC15u, /* O yellow */
    0x00A855F7u, /* T purple */
    0x0022C55Eu, /* S green  */
    0x00EF4444u, /* Z red    */
    0x003B82F6u, /* J blue   */
    0x00F97316u  /* L orange */
};

/* base cells in a 4x4 box, rotation computed at runtime */
static const uint8_t g_base[P_COUNT][4][2] = {
    [P_I] = { {0,1}, {1,1}, {2,1}, {3,1} },
    [P_O] = { {1,0}, {2,0}, {1,1}, {2,1} },
    [P_T] = { {1,0}, {0,1}, {1,1}, {2,1} },
    [P_S] = { {1,0}, {2,0}, {0,1}, {1,1} },
    [P_Z] = { {0,0}, {1,0}, {1,1}, {2,1} },
    [P_J] = { {0,0}, {0,1}, {1,1}, {2,1} },
    [P_L] = { {2,0}, {0,1}, {1,1}, {2,1} },
};

static uint8_t g_board[ROWS][COLS]; /* 0 = empty, else P_* + 1 */

typedef struct {
    uint8_t piece;
    uint8_t rot;
    int8_t x;
    int8_t y;
} piece_t;

static piece_t g_cur;
static uint8_t g_next;
static uint32_t g_score;
static uint32_t g_lines;
static uint32_t g_level;
static uint32_t g_drop_ticks;
static uint64_t g_last_drop;
static uint32_t g_rng;

enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_OVER };
static uint32_t g_state;

static uint16_t g_board_x;
static uint16_t g_board_y;

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

/* return cells of piece after rotation: box 4x4 */
static void piece_cells(uint8_t piece, uint8_t rot, int8_t out[4][2])
{
    for (int i = 0; i < 4; i++) {
        int x = g_base[piece][i][0];
        int y = g_base[piece][i][1];

        for (int r = 0; r < (rot & 3); r++) {
            int nx = 3 - y;
            int ny = x;
            x = nx;
            y = ny;
        }
        out[i][0] = (int8_t) x;
        out[i][1] = (int8_t) y;
    }
}

static bool collides(uint8_t piece, uint8_t rot, int8_t px, int8_t py)
{
    int8_t cells[4][2];

    piece_cells(piece, rot, cells);
    for (int i = 0; i < 4; i++) {
        int bx = px + cells[i][0];
        int by = py + cells[i][1];

        if (bx < 0 || bx >= (int) COLS || by >= (int) ROWS) {
            return true;
        }
        if (by >= 0 && g_board[by][bx] != 0) {
            return true;
        }
    }
    return false;
}

static void spawn_piece(void)
{
    g_cur.piece = g_next;
    g_next = (uint8_t)(rand_u32() % P_COUNT);
    g_cur.rot = 0;
    g_cur.x = 3;
    g_cur.y = 0;
    if (collides(g_cur.piece, g_cur.rot, g_cur.x, g_cur.y)) {
        g_state = ST_OVER;
    }
}

static void reset_game(void)
{
    memset(g_board, 0, sizeof(g_board));
    g_score = 0;
    g_lines = 0;
    g_level = 1;
    g_drop_ticks = 40; /* ~400 ms at 100 Hz */
    g_next = (uint8_t)(rand_u32() % P_COUNT);
    spawn_piece();
    g_last_drop = app_ticks();
    g_state = ST_PLAY;
}

static void lock_piece(void)
{
    int8_t cells[4][2];

    piece_cells(g_cur.piece, g_cur.rot, cells);
    for (int i = 0; i < 4; i++) {
        int bx = g_cur.x + cells[i][0];
        int by = g_cur.y + cells[i][1];

        if (by >= 0) {
            g_board[by][bx] = (uint8_t)(g_cur.piece + 1u);
        }
    }
    /* clear full lines */
    {
        uint32_t cleared = 0;

        for (int r = ROWS - 1; r >= 0; r--) {
            bool full = true;

            for (int c = 0; c < (int) COLS; c++) {
                if (g_board[r][c] == 0) {
                    full = false;
                    break;
                }
            }
            if (full) {
                for (int rr = r; rr > 0; rr--) {
                    memcpy(g_board[rr], g_board[rr - 1], COLS);
                }
                memset(g_board[0], 0, COLS);
                cleared++;
                r++; /* re-check this row */
            }
        }
        if (cleared > 0) {
            static const uint32_t table[5] = { 0, 100, 300, 500, 800 };
            g_score += table[cleared] * g_level;
            g_lines += cleared;
            g_level = g_lines / 10u + 1u;
            if (g_drop_ticks > 10u) {
                g_drop_ticks = 40u - (g_level - 1u) * 4u;
                if (g_drop_ticks < 10u) {
                    g_drop_ticks = 10u;
                }
            }
        }
    }
    spawn_piece();
}

static void move_down(void)
{
    if (!collides(g_cur.piece, g_cur.rot, g_cur.x, (int8_t)(g_cur.y + 1))) {
        g_cur.y++;
    } else {
        lock_piece();
    }
}

static void draw_cell(uint16_t px, uint16_t py, uint32_t color)
{
    app_graphics_fill_rect((uint16_t)(px + 1u), (uint16_t)(py + 1u),
                           (uint16_t)(CELL - 2u), (uint16_t)(CELL - 2u), color);
}

static void render(void)
{
    uint16_t sw = (uint16_t) monios_syscall0(SYS_GRAPHICS_GET_WIDTH);
    uint16_t sh = (uint16_t) monios_syscall0(SYS_GRAPHICS_GET_HEIGHT);

    app_graphics_fill_rect(0, 0, sw, sh, 0x000F172A);

    if (g_state == ST_TITLE) {
        app_graphics_draw_text(sw / 2u - 70u, sh / 2u - 80u, "TETRIS", 0x0022D3EE);
        app_graphics_draw_text(sw / 2u - 140u, sh / 2u - 10u, "Arrows: move / rotate", 0x00E5E7EB);
        app_graphics_draw_text(sw / 2u - 140u, sh / 2u + 20u, "Down: soft drop   Space: hard drop", 0x00E5E7EB);
        app_graphics_draw_text(sw / 2u - 140u, sh / 2u + 50u, "P: pause   R: restart", 0x00E5E7EB);
        app_graphics_draw_text(sw / 2u - 140u, sh / 2u + 90u, "Press any key to start", 0x0094A3B8);
        app_graphics_present();
        return;
    }

    /* board frame */
    app_graphics_fill_rect(g_board_x - 4u, g_board_y - 4u,
                           (uint16_t)(COLS * CELL + 8u),
                           (uint16_t)(ROWS * CELL + 8u),
                           0x001E293B);
    for (uint32_t r = 0; r < ROWS; r++) {
        for (uint32_t c = 0; c < COLS; c++) {
            uint32_t color = g_board[r][c] == 0 ? 0x0016213C : g_color[g_board[r][c] - 1u];

            draw_cell((uint16_t)(g_board_x + c * CELL),
                      (uint16_t)(g_board_y + r * CELL),
                      color);
        }
    }

    /* current piece */
    if (g_state == ST_PLAY || g_state == ST_PAUSE) {
        int8_t cells[4][2];

        piece_cells(g_cur.piece, g_cur.rot, cells);
        for (int i = 0; i < 4; i++) {
            int bx = g_cur.x + cells[i][0];
            int by = g_cur.y + cells[i][1];

            if (by >= 0) {
                draw_cell((uint16_t)(g_board_x + bx * CELL),
                          (uint16_t)(g_board_y + by * CELL),
                          g_color[g_cur.piece]);
            }
        }
    }

    /* side panel */
    {
        uint16_t px = (uint16_t)(g_board_x + COLS * CELL + 24u);
        uint16_t py = g_board_y;

        app_graphics_fill_rect(px, py, 170u, 420u, 0x0016213C);
        app_graphics_draw_text(px + 12u, py + 12u, "NEXT", 0x0094A3B8);

        /* next preview: 4x4 mini cells of 14px */
        {
            int8_t cells[4][2];
            uint16_t ox = (uint16_t)(px + 30u);
            uint16_t oy = (uint16_t)(py + 44u);

            piece_cells(g_next, 0, cells);
            for (int i = 0; i < 4; i++) {
                app_graphics_fill_rect((uint16_t)(ox + cells[i][0] * 16u),
                                       (uint16_t)(oy + cells[i][1] * 16u),
                                       14u, 14u, g_color[g_next]);
            }
        }
        app_graphics_draw_text(px + 12u, py + 130u, "SCORE", 0x0094A3B8);
        draw_uint(px + 12u, py + 152u, g_score, 0x00E5E7EB);
        app_graphics_draw_text(px + 12u, py + 190u, "LINES", 0x0094A3B8);
        draw_uint(px + 12u, py + 212u, g_lines, 0x00E5E7EB);
        app_graphics_draw_text(px + 12u, py + 250u, "LEVEL", 0x0094A3B8);
        draw_uint(px + 12u, py + 272u, g_level, 0x00FACC15);
        app_graphics_draw_text(px + 12u, py + 320u, "P: pause", 0x0094A3B8);
        app_graphics_draw_text(px + 12u, py + 342u, "R: restart", 0x0094A3B8);
        app_graphics_draw_text(px + 12u, py + 364u, "Esc: quit", 0x0094A3B8);
    }

    if (g_state == ST_PAUSE) {
        app_graphics_draw_text(g_board_x + 80u, g_board_y + ROWS * CELL / 2u,
                               "PAUSED", 0x00E5E7EB);
    }
    if (g_state == ST_OVER) {
        app_graphics_fill_rect(g_board_x, g_board_y + ROWS * CELL / 2u - 30u,
                               (uint16_t)(COLS * CELL), 80u, 0x000F172A);
        app_graphics_draw_text(g_board_x + 55u, g_board_y + ROWS * CELL / 2u - 20u,
                               "GAME OVER", 0x00EF4444);
        app_graphics_draw_text(g_board_x + 30u, g_board_y + ROWS * CELL / 2u + 14u,
                               "R: restart   Esc: quit", 0x00E5E7EB);
    }
    app_graphics_present();
}

int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    app_enter_graphics_mode();
    {
        uint16_t sw = (uint16_t) monios_syscall0(SYS_GRAPHICS_GET_WIDTH);
        uint16_t sh = (uint16_t) monios_syscall0(SYS_GRAPHICS_GET_HEIGHT);

        g_board_x = (uint16_t)((sw - COLS * CELL - 200u) / 2u);
        g_board_y = (uint16_t)((sh - ROWS * CELL) / 2u);
    }
    g_rng = (uint32_t) app_ticks() ^ 0x51ED270Bu;
    g_state = ST_TITLE;

    for (;;) {
        app_key_event_t ev;

        while (syscall1(SYS_KEYBOARD_READ_EVENT, (uint64_t) &ev) == 1) {
            if (g_state == ST_TITLE) {
                reset_game();
                break;
            }
            if (ev.type == EV_ESC) {
                app_exit(0);
            }
            if (g_state == ST_OVER) {
                if (ev.type == EV_CHAR && (ev.ch == 'r' || ev.ch == 'R')) {
                    reset_game();
                }
                continue;
            }
            switch (ev.type) {
            case EV_LEFT:
                if (g_state == ST_PLAY &&
                    !collides(g_cur.piece, g_cur.rot, (int8_t)(g_cur.x - 1), g_cur.y)) {
                    g_cur.x--;
                }
                break;
            case EV_RIGHT:
                if (g_state == ST_PLAY &&
                    !collides(g_cur.piece, g_cur.rot, (int8_t)(g_cur.x + 1), g_cur.y)) {
                    g_cur.x++;
                }
                break;
            case EV_DOWN:
                if (g_state == ST_PLAY) {
                    move_down();
                    g_last_drop = app_ticks();
                    g_score += 1;
                }
                break;
            case EV_UP:
                if (g_state == ST_PLAY) {
                    uint8_t nr = (uint8_t)((g_cur.rot + 1u) & 3u);
                    if (!collides(g_cur.piece, nr, g_cur.x, g_cur.y)) {
                        g_cur.rot = nr;
                    } else if (!collides(g_cur.piece, nr, (int8_t)(g_cur.x + 1), g_cur.y)) {
                        g_cur.x++; g_cur.rot = nr;
                    } else if (!collides(g_cur.piece, nr, (int8_t)(g_cur.x - 1), g_cur.y)) {
                        g_cur.x--; g_cur.rot = nr;
                    }
                }
                break;
            case EV_CHAR:
                if (ev.ch == ' ') {
                    if (g_state == ST_PLAY) {
                        while (!collides(g_cur.piece, g_cur.rot, g_cur.x, (int8_t)(g_cur.y + 1))) {
                            g_cur.y++;
                            g_score += 2;
                        }
                        lock_piece();
                        g_last_drop = app_ticks();
                    }
                } else if (ev.ch == 'p' || ev.ch == 'P') {
                    if (g_state == ST_PLAY) g_state = ST_PAUSE;
                    else if (g_state == ST_PAUSE) g_state = ST_PLAY;
                } else if (ev.ch == 'r' || ev.ch == 'R') {
                    reset_game();
                }
                break;
            default:
                break;
            }
        }

        if (g_state == ST_PLAY && (app_ticks() - g_last_drop) >= g_drop_ticks) {
            g_last_drop = app_ticks();
            move_down();
        }
        render();
        app_sleep_ticks(2);
    }
    return 0;
}
