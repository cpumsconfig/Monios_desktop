/*
 * snake.c -- Monios classic Snake game (Task 20a).
 *
 * Controls:
 *   arrow keys   steer the snake
 *   space        pause / resume
 *   R            restart after game over (or at any time)
 *   Esc          quit
 *
 * Score high-water mark is persisted to
 *   C:\Users\root\Games\snake_highscore.txt
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

#define GRID_W   20u
#define GRID_H   20u
#define CELL     24u
#define MAX_LEN  (GRID_W * GRID_H)

#define C_BG      0x00101826u
#define C_BOARD   0x001B2A41u
#define C_GRID    0x0024364Fu
#define C_SNAKE   0x003DD68Cu
#define C_HEAD    0x007CFFB2u
#define C_FOOD    0x00F87171u
#define C_TEXT    0x00E5E7EBu
#define C_DIM     0x0094A3B8u
#define C_PANEL   0x0016213Cu

static uint16_t g_board_x;
static uint16_t g_board_y;

typedef struct {
    int16_t x;
    int16_t y;
} cell_t;

static cell_t g_snake[MAX_LEN];
static uint32_t g_len;
static int8_t g_dir_x;
static int8_t g_dir_y;
static int8_t g_next_x;
static int8_t g_next_y;
static cell_t g_food;
static uint32_t g_score;
static uint32_t g_food_eaten;
static uint32_t g_highscore;
static uint32_t g_move_ticks;   /* ticks between moves (100 Hz tick) */
static uint64_t g_last_move;

enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_OVER };
static uint32_t g_state;

static uint32_t g_rng;

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
    /* reverse */
    for (int a = 0, b = i - 1; a < b; a++, b--) {
        char t = buf[a];
        buf[a] = buf[b];
        buf[b] = t;
    }
    app_graphics_draw_text(x, y, buf, color);
}

static void load_highscore(void)
{
    char buf[16];
    int n;

    g_highscore = 0;
    if (!app_file_exists("C:\\Users\\root\\Games")) {
        app_file_mkdir("C:\\Users");
        app_file_mkdir("C:\\Users\\root");
        app_file_mkdir("C:\\Users\\root\\Games");
    }
    n = app_file_read("C:\\Users\\root\\Games\\snake_highscore.txt", buf, sizeof(buf) - 1u);
    if (n > 0) {
        uint32_t v = 0;

        for (int i = 0; i < n; i++) {
            if (buf[i] >= '0' && buf[i] <= '9') {
                v = v * 10u + (uint32_t)(buf[i] - '0');
            }
        }
        g_highscore = v;
    }
}

static void save_highscore(void)
{
    char buf[16];
    int i = 0;
    uint32_t v = g_highscore;

    if (v == 0) {
        buf[i++] = '0';
    } else {
        while (v > 0 && i < (int) sizeof(buf) - 1) {
            buf[i++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
    }
    buf[i] = '\0';
    for (int a = 0, b = i - 1; a < b; a++, b--) {
        char t = buf[a];
        buf[a] = buf[b];
        buf[b] = t;
    }
    app_file_write("C:\\Users\\root\\Games\\snake_highscore.txt", buf, (uint32_t) i);
}

static void place_food(void)
{
    for (;;) {
        uint32_t rx = rand_u32() % GRID_W;
        uint32_t ry = rand_u32() % GRID_H;
        bool on_snake = false;

        for (uint32_t i = 0; i < g_len; i++) {
            if ((uint32_t) g_snake[i].x == rx && (uint32_t) g_snake[i].y == ry) {
                on_snake = true;
                break;
            }
        }
        if (!on_snake) {
            g_food.x = (int16_t) rx;
            g_food.y = (int16_t) ry;
            return;
        }
    }
}

static void reset_game(void)
{
    g_len = 5;
    g_dir_x = 1;
    g_dir_y = 0;
    g_next_x = 1;
    g_next_y = 0;
    g_score = 0;
    g_food_eaten = 0;
    g_move_ticks = 15; /* ~150 ms per step at 100 Hz */
    for (uint32_t i = 0; i < g_len; i++) {
        g_snake[i].x = (int16_t)(5 - i);
        g_snake[i].y = 8;
    }
    place_food();
    g_last_move = app_ticks();
    g_state = ST_PLAY;
}

static void draw_cell(uint16_t gx, uint16_t gy, uint32_t color)
{
    app_graphics_fill_rect((uint16_t)(g_board_x + gx * CELL + 1u),
                           (uint16_t)(g_board_y + gy * CELL + 1u),
                           (uint16_t)(CELL - 2u),
                           (uint16_t)(CELL - 2u),
                           color);
}

static void render(void)
{
    uint16_t sw = (uint16_t) monios_syscall0(SYS_GRAPHICS_GET_WIDTH);
    uint16_t sh = (uint16_t) monios_syscall0(SYS_GRAPHICS_GET_HEIGHT);

    app_graphics_fill_rect(0, 0, sw, sh, C_BG);

    if (g_state == ST_TITLE) {
        app_graphics_draw_text(sw / 2u - 90u, sh / 2u - 80u,  "SNAKE", C_HEAD);
        app_graphics_draw_text(sw / 2u - 130u, sh / 2u - 20u, "Arrow keys: steer", C_TEXT);
        app_graphics_draw_text(sw / 2u - 130u, sh / 2u + 10u, "Space: pause", C_TEXT);
        app_graphics_draw_text(sw / 2u - 130u, sh / 2u + 40u, "High score:", C_DIM);
        draw_uint(sw / 2u + 30u, sh / 2u + 40u, g_highscore, C_FOOD);
        app_graphics_draw_text(sw / 2u - 130u, sh / 2u + 90u, "Press any key to start", C_DIM);
        app_graphics_present();
        return;
    }

    /* board background */
    app_graphics_fill_rect(g_board_x - 4u, g_board_y - 4u,
                           (uint16_t)(GRID_W * CELL + 8u),
                           (uint16_t)(GRID_H * CELL + 8u),
                           C_BOARD);
    for (uint32_t gy = 0; gy < GRID_H; gy++) {
        for (uint32_t gx = 0; gx < GRID_W; gx++) {
            draw_cell((uint16_t) gx, (uint16_t) gy, C_GRID);
        }
    }

    draw_cell((uint16_t) g_food.x, (uint16_t) g_food.y, C_FOOD);

    for (uint32_t i = 0; i < g_len; i++) {
        draw_cell((uint16_t) g_snake[i].x, (uint16_t) g_snake[i].y,
                  i == 0 ? C_HEAD : C_SNAKE);
    }

    /* side panel */
    {
        uint16_t px = (uint16_t)(g_board_x + GRID_W * CELL + 24u);

        app_graphics_fill_rect(px, g_board_y, 180u, 300u, C_PANEL);
        app_graphics_draw_text(px + 12u, g_board_y + 12u, "SCORE", C_DIM);
        draw_uint(px + 12u, g_board_y + 34u, g_score, C_TEXT);
        app_graphics_draw_text(px + 12u, g_board_y + 70u, "BEST", C_DIM);
        draw_uint(px + 12u, g_board_y + 92u, g_highscore, C_FOOD);
        app_graphics_draw_text(px + 12u, g_board_y + 130u, "LENGTH", C_DIM);
        draw_uint(px + 12u, g_board_y + 152u, g_len, C_TEXT);
        app_graphics_draw_text(px + 12u, g_board_y + 190u, "R: restart", C_DIM);
        app_graphics_draw_text(px + 12u, g_board_y + 212u, "Space: pause", C_DIM);
        app_graphics_draw_text(px + 12u, g_board_y + 234u, "Esc: quit", C_DIM);
    }

    if (g_state == ST_PAUSE) {
        app_graphics_fill_rect(g_board_x, g_board_y + GRID_H * CELL / 2u - 30u,
                               (uint16_t)(GRID_W * CELL), 60u, C_PANEL);
        app_graphics_draw_text(g_board_x + 180u, g_board_y + GRID_H * CELL / 2u - 16u,
                               "PAUSED", C_TEXT);
    }
    if (g_state == ST_OVER) {
        app_graphics_fill_rect(g_board_x, g_board_y + GRID_H * CELL / 2u - 40u,
                               (uint16_t)(GRID_W * CELL), 90u, C_PANEL);
        app_graphics_draw_text(g_board_x + 150u, g_board_y + GRID_H * CELL / 2u - 28u,
                               "GAME OVER", C_FOOD);
        app_graphics_draw_text(g_board_x + 120u, g_board_y + GRID_H * CELL / 2u + 4u,
                               "R: restart   Esc: quit", C_TEXT);
    }
    app_graphics_present();
}

static void step_snake(void)
{
    cell_t new_head;

    g_dir_x = g_next_x;
    g_dir_y = g_next_y;
    new_head.x = (int16_t)(g_snake[0].x + g_dir_x);
    new_head.y = (int16_t)(g_snake[0].y + g_dir_y);

    /* wall collision */
    if (new_head.x < 0 || new_head.y < 0 ||
        (uint32_t) new_head.x >= GRID_W || (uint32_t) new_head.y >= GRID_H) {
        g_state = ST_OVER;
        if (g_score > g_highscore) {
            g_highscore = g_score;
            save_highscore();
        }
        return;
    }
    /* self collision (tail moves away, so ignore the very last cell unless
     * we grow this step) */
    for (uint32_t i = 0; i < g_len; i++) {
        if (g_snake[i].x == new_head.x && g_snake[i].y == new_head.y) {
            if (!(i == g_len - 1 &&
                  !(new_head.x == g_food.x && new_head.y == g_food.y))) {
                g_state = ST_OVER;
                if (g_score > g_highscore) {
                    g_highscore = g_score;
                    save_highscore();
                }
                return;
            }
        }
    }

    /* body shift */
    for (uint32_t i = g_len; i > 0; i--) {
        g_snake[i] = g_snake[i - 1];
    }
    g_snake[0] = new_head;

    if (new_head.x == g_food.x && new_head.y == g_food.y) {
        g_len++;
        g_score += 10;
        g_food_eaten++;
        if (g_food_eaten % 5u == 0 && g_move_ticks > 5u) {
            g_move_ticks--;
        }
        place_food();
    } else {
        /* tail already shifted; shrink back */
        g_len--;
    }
}

int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    app_enter_graphics_mode();
    {
        uint16_t sw = (uint16_t) monios_syscall0(SYS_GRAPHICS_GET_WIDTH);
        uint16_t sh = (uint16_t) monios_syscall0(SYS_GRAPHICS_GET_HEIGHT);

        g_board_x = (uint16_t)((sw - GRID_W * CELL) / 2u);
        g_board_y = (uint16_t)((sh - GRID_H * CELL) / 2u);
    }
    g_rng = (uint32_t) app_ticks() ^ 0x9E3779B9u;
    load_highscore();
    g_state = ST_TITLE;

    for (;;) {
        app_key_event_t ev;

        while (syscall1(SYS_KEYBOARD_READ_EVENT, (uint64_t) &ev) == 1) {
            if (g_state == ST_TITLE) {
                reset_game();
                break;
            }
            switch (ev.type) {
            case EV_UP:    if (g_dir_y != 1)  { g_next_x = 0; g_next_y = -1; } break;
            case EV_DOWN:  if (g_dir_y != -1) { g_next_x = 0; g_next_y = 1; } break;
            case EV_LEFT:  if (g_dir_x != 1)  { g_next_x = -1; g_next_y = 0; } break;
            case EV_RIGHT: if (g_dir_x != -1) { g_next_x = 1; g_next_y = 0; } break;
            case EV_CHAR:
                if (ev.ch == ' ' || ev.ch == 0) {
                    if (g_state == ST_PLAY) g_state = ST_PAUSE;
                    else if (g_state == ST_PAUSE) g_state = ST_PLAY;
                } else if (ev.ch == 'r' || ev.ch == 'R') {
                    reset_game();
                } else if (ev.ch == 27 || ev.ch == 0) {
                    /* handled below via ESC */
                }
                break;
            case EV_ESC:
                app_exit(0);
                break;
            default:
                break;
            }
            if (ev.type == EV_ESC) {
                app_exit(0);
            }
        }

        if (g_state == ST_PLAY && (app_ticks() - g_last_move) >= g_move_ticks) {
            g_last_move = app_ticks();
            step_snake();
        }
        render();
        app_sleep_ticks(2);
    }
    return 0;
}
