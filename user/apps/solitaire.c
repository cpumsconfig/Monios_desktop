/*
 * solitaire.c -- Monios Klondike Solitaire (纸牌).
 *
 *   1024x768 graphics mode, green felt table.
 *
 *   Left click / drag : pick up and move cards
 *   Double-click card : auto-send to foundation
 *   Click stock       : draw one card (press D to toggle draw-three)
 *   N                 : new game
 *   Esc               : quit
 *
 *   Tableau: red/black alternating, descending K->A.
 *   Foundation: same suit, ascending A->K.
 *   Empty tableau slot accepts a King.
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

/* geometry */
#define CARD_W    72u
#define CARD_H    96u
#define COL_GAP   12u
#define ROW_DOWN  26u   /* vertical offset for face-down card overlap */
#define ROW_UP    28u   /* vertical offset for face-up card overlap   */

#define STOCK_X   16u
#define WASTE_X   (STOCK_X + CARD_W + COL_GAP)
#define TOP_Y     16u
#define TAB_Y     (TOP_Y + CARD_H + 18u)

/* 7 tableau columns left aligned */
#define TAB_X(i)  ((uint16_t)(STOCK_X + (uint32_t)(i) * (CARD_W + COL_GAP)))
/* 4 foundation piles on the right */
#define FOUND_X(i) ((uint16_t)(1024u - 16u - CARD_W - (uint32_t)(3u - (i)) * (CARD_W + COL_GAP)))

/* colors */
#define COL_FELT     0x001B5E20u
#define COL_CARD     0x00FFFFFFu
#define COL_CARD_BK  0x001565C0u
#define COL_CARD_BD  0x000D47A1u
#define COL_EMPTY    0x000D4A12u
#define COL_BLACK    0x00111111u
#define COL_RED      0x00D32F2Fu
#define COL_HILITE   0x00FDD835u

typedef struct {
    uint8_t suit;   /* 0=spade 1=heart 2=club 3=diamond */
    uint8_t rank;   /* 0=A .. 12=K */
    bool face_up;
} card_t;

#define MAX_PILE  20u

typedef struct {
    card_t c[MAX_PILE];
    uint8_t n;
} pile_t;

static pile_t g_stock;
static pile_t g_waste;
static pile_t g_foundation[4];
static pile_t g_tab[7];
static uint8_t g_draw3;       /* 0 = draw 1, 1 = draw 3 */
static bool g_won;
static uint64_t g_win_tick;
static uint32_t g_rng;

/* drag state: which pile / index are we lifting? */
#define SRC_NONE   0
#define SRC_WASTE  1
#define SRC_TAB    2
#define SRC_FOUND  3

typedef struct {
    uint8_t src;       /* SRC_* */
    uint8_t pile;      /* foundation idx or tableau col */
    uint8_t idx;       /* index within that pile of the lifted top card */
    uint8_t count;     /* number of cards lifted (a run) */
    int32_t dx, dy;    /* grab offset within the card */
    int32_t mx, my;    /* current mouse pos */
    bool active;
} drag_t;

static drag_t g_drag;

/* double-click detection */
static uint64_t g_last_click_tick;
static int32_t g_last_click_x;
static int32_t g_last_click_y;

static uint32_t rand_u32(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static bool is_red(uint8_t suit) { return suit == 1 || suit == 3; }

static void rank_text(uint8_t rank, char *out)
{
    if (rank == 0)      { out[0] = 'A'; }
    else if (rank < 9)  { out[0] = (char)('1' + rank); }
    else if (rank == 9) { out[0] = 'T'; }
    else if (rank == 10){ out[0] = 'J'; }
    else if (rank == 11){ out[0] = 'Q'; }
    else                { out[0] = 'K'; }
    out[1] = '\0';
}

static void suit_text(uint8_t suit, char *out)
{
    static const char s[4] = { 'S', 'H', 'C', 'D' };
    out[0] = s[suit];
    out[1] = '\0';
}

/* ---------- setup ---------- */
static void new_game(void)
{
    card_t deck[52];
    uint32_t i;

    memset(&g_stock, 0, sizeof(g_stock));
    memset(&g_waste, 0, sizeof(g_waste));
    memset(g_foundation, 0, sizeof(g_foundation));
    memset(g_tab, 0, sizeof(g_tab));
    memset(&g_drag, 0, sizeof(g_drag));
    g_won = false;
    g_win_tick = 0;

    for (i = 0; i < 52; i++) {
        deck[i].suit = (uint8_t)(i / 13);
        deck[i].rank = (uint8_t)(i % 13);
        deck[i].face_up = false;
    }
    /* Fisher-Yates */
    for (i = 52; i > 1; i--) {
        uint32_t j = rand_u32() % i;
        card_t t = deck[i - 1];
        deck[i - 1] = deck[j];
        deck[j] = t;
    }

    /* deal tableau: column i gets i+1 cards, bottom face up */
    {
        uint32_t k = 0;
        uint32_t col;
        for (col = 0; col < 7; col++) {
            uint32_t row;
            for (row = 0; row <= col; row++) {
                card_t c = deck[k++];
                c.face_up = (row == col);
                g_tab[col].c[g_tab[col].n++] = c;
            }
        }
        while (k < 52) {
            deck[k].face_up = false;
            g_stock.c[g_stock.n++] = deck[k++];
        }
    }
}

/* ---------- helpers: locate card rectangles ---------- */
static uint32_t tab_pixel_height(const pile_t *p)
{
    uint32_t h = 0;
    uint32_t i;
    for (i = 0; i < p->n; i++) {
        if (i == 0) h += CARD_H;
        else h += p->c[i].face_up ? ROW_UP : ROW_DOWN;
    }
    return h;
}

/* ---------- can we move card 'c' onto tableau column 'col'? ---------- */
static bool can_drop_tab(const card_t *c, uint8_t col)
{
    const pile_t *p = &g_tab[col];
    if (p->n == 0) {
        return c->rank == 12;   /* only King on empty */
    }
    const card_t *top = &p->c[p->n - 1];
    if (!top->face_up) return false;
    return is_red(c->suit) != is_red(top->suit) &&
           c->rank == (uint8_t)(top->rank - 1);
}

/* ---------- can we move card 'c' onto foundation 'f'? ---------- */
static bool can_drop_found(const card_t *c, uint8_t f)
{
    const pile_t *p = &g_foundation[f];
    if (p->n == 0) return c->rank == 0;   /* only Ace on empty */
    const card_t *top = &p->c[p->n - 1];
    return c->suit == top->suit && c->rank == (uint8_t)(top->rank + 1);
}

/* ---------- move 'count' cards from src pile to dst pile ---------- */
static void move_cards(pile_t *src, uint8_t idx, uint8_t count, pile_t *dst)
{
    uint8_t i;
    for (i = 0; i < count; i++) {
        dst->c[dst->n++] = src->c[idx + i];
    }
    src->n = idx;
    /* flip newly exposed tableau card */
    if (src->n > 0 && src->c[src->n - 1].face_up == false) {
        src->c[src->n - 1].face_up = true;
    }
}

/* ---------- try auto-moving a card to any foundation ---------- */
static bool try_auto_foundation(uint8_t src, uint8_t pile, uint8_t idx)
{
    const card_t *c;
    uint8_t f;
    pile_t *sp;

    if (src == SRC_WASTE) sp = &g_waste;
    else if (src == SRC_TAB) sp = &g_tab[pile];
    else return false;

    if (idx != sp->n - 1) return false;   /* only top card */
    c = &sp->c[idx];

    for (f = 0; f < 4; f++) {
        if (can_drop_found(c, f)) {
            move_cards(sp, idx, 1, &g_foundation[f]);
            /* win check */
            if (g_foundation[0].n == 13 && g_foundation[1].n == 13 &&
                g_foundation[2].n == 13 && g_foundation[3].n == 13) {
                g_won = true;
                g_win_tick = app_ticks();
            }
            return true;
        }
    }
    return false;
}

/* ---------- stock click ---------- */
static void on_stock_click(void)
{
    if (g_stock.n > 0) {
        uint8_t deal = g_draw3 ? 3u : 1u;
        while (deal-- > 0 && g_stock.n > 0) {
            card_t c = g_stock.c[--g_stock.n];
            c.face_up = true;
            g_waste.c[g_waste.n++] = c;
        }
    } else {
        /* recycle waste back to stock (face down) */
        while (g_waste.n > 0) {
            card_t c = g_waste.c[--g_waste.n];
            c.face_up = false;
            g_stock.c[g_stock.n++] = c;
        }
    }
}

/* ---------- pick up on mouse down: returns true if grabbed ---------- */
static bool pick_up(int32_t mx, int32_t my)
{
    uint8_t i;

    /* stock */
    if (mx >= STOCK_X && mx < (int32_t)(STOCK_X + CARD_W) &&
        my >= TOP_Y && my < (int32_t)(TOP_Y + CARD_H)) {
        on_stock_click();
        return false;
    }

    /* waste: top card */
    if (g_waste.n > 0) {
        uint16_t wx = (uint16_t)(WASTE_X + (g_waste.n > 1 ? (g_draw3 ? 24u : 12u) : 0u));
        if (mx >= WASTE_X && mx < (int32_t)(wx + CARD_W) &&
            my >= TOP_Y && my < (int32_t)(TOP_Y + CARD_H)) {
            g_drag.src = SRC_WASTE;
            g_drag.pile = 0;
            g_drag.idx = g_waste.n - 1;
            g_drag.count = 1;
            g_drag.dx = mx - WASTE_X;
            g_drag.dy = my - TOP_Y;
            g_drag.active = true;
            return true;
        }
    }

    /* foundations: clicking top card lifts it (rarely needed) */
    for (i = 0; i < 4; i++) {
        pile_t *p = &g_foundation[i];
        uint16_t fx = FOUND_X(i);
        if (p->n > 0 &&
            mx >= fx && mx < (int32_t)(fx + CARD_W) &&
            my >= TOP_Y && my < (int32_t)(TOP_Y + CARD_H)) {
            g_drag.src = SRC_FOUND;
            g_drag.pile = i;
            g_drag.idx = p->n - 1;
            g_drag.count = 1;
            g_drag.dx = mx - fx;
            g_drag.dy = my - TOP_Y;
            g_drag.active = true;
            return true;
        }
    }

    /* tableau: hit-test from top card down */
    for (i = 0; i < 7; i++) {
        pile_t *p = &g_tab[i];
        uint16_t tx = TAB_X(i);
        uint32_t y = TAB_Y;
        int32_t hit_idx = -1;
        uint32_t k;
        for (k = 0; k < p->n; k++) {
            uint32_t step = (k == 0) ? CARD_H : (p->c[k].face_up ? ROW_UP : ROW_DOWN);
            if (mx >= tx && mx < (int32_t)(tx + CARD_W) &&
                my >= (int32_t)y && my < (int32_t)(y + step)) {
                hit_idx = (int32_t)k;
            }
            y += step;
        }
        if (hit_idx >= 0) {
            if (!p->c[hit_idx].face_up) {
                /* click face-down tableau card flips it */
                if (hit_idx == (int32_t)(p->n - 1)) {
                    p->c[hit_idx].face_up = true;
                }
                return false;
            }
            /* lift the card and all face-up cards below it */
            g_drag.src = SRC_TAB;
            g_drag.pile = i;
            g_drag.idx = (uint8_t)hit_idx;
            g_drag.count = (uint8_t)(p->n - hit_idx);
            g_drag.dx = mx - (int32_t)tx;
            g_drag.dy = my - (int32_t)(TAB_Y + hit_idx * ROW_UP);
            g_drag.active = true;
            return true;
        }
    }
    return false;
}

/* ---------- drop: try to place the lifted run somewhere ---------- */
static void drop_at(int32_t mx, int32_t my)
{
    uint8_t i;
    pile_t *sp;
    const card_t *lead;

    if (!g_drag.active) return;

    if (g_drag.src == SRC_WASTE) sp = &g_waste;
    else if (g_drag.src == SRC_FOUND) sp = &g_foundation[g_drag.pile];
    else sp = &g_tab[g_drag.pile];

    lead = &sp->c[g_drag.idx];

    /* foundation targets: only single card drops */
    if (g_drag.count == 1) {
        for (i = 0; i < 4; i++) {
            uint16_t fx = FOUND_X(i);
            if (mx >= fx && mx < (int32_t)(fx + CARD_W) &&
                my >= TOP_Y && my < (int32_t)(TOP_Y + CARD_H)) {
                if (can_drop_found(lead, i)) {
                    move_cards(sp, g_drag.idx, 1, &g_foundation[i]);
                    if (g_foundation[0].n == 13 && g_foundation[1].n == 13 &&
                        g_foundation[2].n == 13 && g_foundation[3].n == 13) {
                        g_won = true;
                        g_win_tick = app_ticks();
                    }
                }
                g_drag.active = false;
                return;
            }
        }
    }

    /* tableau targets */
    for (i = 0; i < 7; i++) {
        uint16_t tx = TAB_X(i);
        uint32_t th = tab_pixel_height(&g_tab[i]);
        uint32_t ty = (g_tab[i].n == 0) ? TAB_Y : TAB_Y + th - ROW_UP;
        /* generous drop zone: whole column */
        if (mx >= tx - 8 && mx < (int32_t)(tx + CARD_W + 8) &&
            my >= (int32_t)(ty - 16) && my < (int32_t)(ty + CARD_H + 40)) {
            /* don't drop back onto our own source column */
            if (!(g_drag.src == SRC_TAB && g_drag.pile == i)) {
                if (can_drop_tab(lead, i)) {
                    move_cards(sp, g_drag.idx, g_drag.count, &g_tab[i]);
                    g_drag.active = false;
                    return;
                }
            }
        }
    }
    /* otherwise: snap back (drag cancelled) */
    g_drag.active = false;
}

/* ---------- double-click handling on mouse down ---------- */
static void handle_click(int32_t mx, int32_t my, uint8_t buttons)
{
    bool now_drag;
    uint64_t now = app_ticks();

    now_drag = pick_up(mx, my);

    /* double-click = two quick clicks near same spot */
    {
        int32_t dx = mx - g_last_click_x;
        int32_t dy = my - g_last_click_y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (!now_drag &&
            (now - g_last_click_tick) < 25u &&
            dx < 12 && dy < 12) {
        /* try to find the card under cursor and auto-send to foundation */
        uint8_t i;
        /* waste top */
        if (g_waste.n > 0 &&
            mx >= WASTE_X && mx < (int32_t)(WASTE_X + CARD_W) &&
            my >= TOP_Y && my < (int32_t)(TOP_Y + CARD_H)) {
            try_auto_foundation(SRC_WASTE, 0, g_waste.n - 1);
            g_last_click_tick = 0;
            return;
        }
        /* tableau top */
        for (i = 0; i < 7; i++) {
            pile_t *p = &g_tab[i];
            if (p->n > 0 && p->c[p->n - 1].face_up) {
                uint16_t tx = TAB_X(i);
                uint32_t th = tab_pixel_height(p);
                if (mx >= tx && mx < (int32_t)(tx + CARD_W) &&
                    my >= (int32_t)(TAB_Y + th - ROW_UP - 8) &&
                    my < (int32_t)(TAB_Y + th + 8)) {
                    try_auto_foundation(SRC_TAB, i, p->n - 1);
                    g_last_click_tick = 0;
                    return;
                }
            }
        }
    }
    } /* end double-click block */

    g_last_click_tick = now;
    g_last_click_x = mx;
    g_last_click_y = my;

    (void)buttons;
}

/* ---------- drawing ---------- */
static void draw_card_face(uint16_t x, uint16_t y, const card_t *c)
{
    char buf[4];
    uint32_t ink = is_red(c->suit) ? COL_RED : COL_BLACK;

    app_graphics_fill_rect(x, y, CARD_W, CARD_H, COL_CARD);
    app_graphics_fill_rect(x, y, CARD_W, 2, COL_EMPTY);
    app_graphics_fill_rect(x, (uint16_t)(y + CARD_H - 2), CARD_W, 2, COL_EMPTY);

    rank_text(c->rank, buf);
    app_graphics_draw_text((uint16_t)(x + 4u), (uint16_t)(y + 3u), buf, ink);
    suit_text(c->suit, buf);
    app_graphics_draw_text((uint16_t)(x + 28u), (uint16_t)(y + 38u), buf, ink);
    /* bottom right rank, rotated visually by drawing near corner */
    rank_text(c->rank, buf);
    app_graphics_draw_text((uint16_t)(x + CARD_W - 18u), (uint16_t)(y + CARD_H - 16u), buf, ink);
}

static void draw_card_back(uint16_t x, uint16_t y)
{
    app_graphics_fill_rect(x, y, CARD_W, CARD_H, COL_CARD_BK);
    app_graphics_fill_rect((uint16_t)(x + 4u), (uint16_t)(y + 4u),
                          (uint16_t)(CARD_W - 8u), (uint16_t)(CARD_H - 8u),
                          COL_CARD_BD);
}

static void draw_slot(uint16_t x, uint16_t y)
{
    app_graphics_fill_rect(x, y, CARD_W, CARD_H, COL_EMPTY);
    app_graphics_fill_rect(x, y, CARD_W, 2, COL_CARD_BD);
    app_graphics_fill_rect(x, (uint16_t)(y + CARD_H - 2), CARD_W, 2, COL_CARD_BD);
    app_graphics_fill_rect(x, y, 2, CARD_H, COL_CARD_BD);
    app_graphics_fill_rect((uint16_t)(x + CARD_W - 2), y, 2, CARD_H, COL_CARD_BD);
}

static void render(void)
{
    uint16_t i;
    uint8_t k;

    app_graphics_fill_rect(0, 0, 1024, 768, COL_FELT);

    /* top bar text */
    app_graphics_draw_text(16u, 2u, "Solitaire - N:new D:draw3 Esc:quit", 0x00BBDEFBu);

    /* stock */
    if (g_stock.n > 0) {
        draw_card_back(STOCK_X, TOP_Y);
    } else {
        draw_slot(STOCK_X, TOP_Y);
        app_graphics_draw_text((uint16_t)(STOCK_X + 20u), (uint16_t)(TOP_Y + 40u),
                               "<", 0x00FFFFFFu);
    }

    /* waste: draw up to 3 visible */
    {
        uint8_t start = g_waste.n;
        uint8_t show = g_draw3 ? 3u : 1u;
        uint8_t j;
        if (start > show) start = show;
        for (j = 0; j < start; j++) {
            uint8_t idx = (uint8_t)(g_waste.n - start + j);
            uint16_t ox = (uint16_t)(WASTE_X + j * (g_draw3 ? 12u : 0u));
            draw_card_face(ox, TOP_Y, &g_waste.c[idx]);
        }
    }

    /* foundations */
    for (i = 0; i < 4; i++) {
        uint16_t fx = FOUND_X(i);
        if (g_foundation[i].n == 0) {
            draw_slot(fx, TOP_Y);
        } else {
            draw_card_face(fx, TOP_Y, &g_foundation[i].c[g_foundation[i].n - 1]);
        }
    }

    /* tableau */
    for (i = 0; i < 7; i++) {
        uint16_t tx = TAB_X(i);
        uint32_t y = TAB_Y;
        const pile_t *p = &g_tab[i];
        if (p->n == 0) {
            draw_slot(tx, TAB_Y);
            continue;
        }
        for (k = 0; k < p->n; k++) {
            /* skip cards being dragged */
            if (g_drag.active && g_drag.src == SRC_TAB && g_drag.pile == i &&
                k >= g_drag.idx) {
                break;
            }
            if (p->c[k].face_up) {
                draw_card_face(tx, (uint16_t)y, &p->c[k]);
                y += ROW_UP;
            } else {
                draw_card_back(tx, (uint16_t)y);
                y += ROW_DOWN;
            }
        }
    }

    /* dragged cards: follow mouse */
    if (g_drag.active) {
        pile_t *sp;
        uint8_t j;
        uint16_t dx = (uint16_t)(g_drag.mx - g_drag.dx);
        uint16_t dy = (uint16_t)(g_drag.my - g_drag.dy);

        if (g_drag.src == SRC_WASTE) sp = &g_waste;
        else if (g_drag.src == SRC_FOUND) sp = &g_foundation[g_drag.pile];
        else sp = &g_tab[g_drag.pile];

        for (j = 0; j < g_drag.count; j++) {
            draw_card_face((uint16_t)(dx + j * 0),
                          (uint16_t)(dy + j * ROW_UP),
                          &sp->c[g_drag.idx + j]);
        }
        /* highlight border */
        app_graphics_fill_rect(dx, (uint16_t)(dy - 2), CARD_W, 2, COL_HILITE);
    }

    /* win overlay */
    if (g_won) {
        uint64_t elapsed = app_ticks() - g_win_tick;
        uint32_t blink = (uint32_t)(elapsed / 30u) & 1u;
        app_graphics_fill_rect(262u, 300u, 500u, 120u, 0x001B5E20u);
        if (blink) {
            app_graphics_draw_text(360u, 330u, "You Win! Bien Joue!", COL_HILITE);
        } else {
            app_graphics_draw_text(360u, 330u, "You Win! Bien Joue!", 0x00FFFFFFu);
        }
        app_graphics_draw_text(380u, 370u, "Press N for new game, Esc to quit", 0x00BBDEFBu);
    }

    app_graphics_present();
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    app_enter_graphics_mode();
    g_rng = (uint32_t)app_ticks() ^ 0x51ED270Bu;
    g_draw3 = 0;
    new_game();

    for (;;) {
        app_key_event_t ev;
        app_mouse_snapshot_t mouse;
        static uint8_t prev_buttons = 0;

        while (syscall1(SYS_KEYBOARD_READ_EVENT, (uint64_t)&ev) == 1) {
            if (ev.type == EV_ESC) {
                app_exit(0);
            }
            if (ev.type == EV_CHAR) {
                if (ev.ch == 'n' || ev.ch == 'N') {
                    new_game();
                } else if (ev.ch == 'd' || ev.ch == 'D') {
                    g_draw3 = (uint8_t)(1u - g_draw3);
                }
            }
        }

        app_get_mouse(&mouse);

        /* left press: pick up */
        if ((mouse.buttons & 0x01u) && !(prev_buttons & 0x01u)) {
            handle_click(mouse.x_pixels, mouse.y_pixels, mouse.buttons);
        }
        /* track mouse while dragging */
        if (g_drag.active) {
            g_drag.mx = mouse.x_pixels;
            g_drag.my = mouse.y_pixels;
        }
        /* left release: drop */
        if (!(mouse.buttons & 0x01u) && (prev_buttons & 0x01u) && g_drag.active) {
            drop_at(mouse.x_pixels, mouse.y_pixels);
        }

        prev_buttons = mouse.buttons;
        render();
        app_sleep_ticks(2);
    }
    return 0;
}
