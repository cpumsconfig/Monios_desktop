#include "common.h"
#include "console.h"
#include "kernel.h"

#define VGA_WIDTH CONSOLE_COLUMNS
#define VGA_HEIGHT 25U
#define VGA_ATTR 0x07
#define VGA_TEXT_BUFFER ((volatile uint16_t *) 0xB8000)
#define CONSOLE_DEFAULT_PID (-1)
#define CONSOLE_HISTORY_ROWS 1024U
#define CONSOLE_SCHEME_COUNT 5U

typedef struct {
    uint32_t bg;
    uint32_t fg;
} console_scheme_def_t;

/* Color schemes: default / solarized-dark / dracula / monokai / light */
static const console_scheme_def_t g_console_schemes[CONSOLE_SCHEME_COUNT] = {
    { 0x00161D27u, 0x00E8EEF5u },
    { 0x00002B36u, 0x00EEE8D5u },
    { 0x00282A36u, 0x00F8F8F2u },
    { 0x00272822u, 0x00F8F8E0u },
    { 0x00FFFFFFu, 0x0017232Eu }
};
static uint32_t g_console_scheme_idx;
/* scrollback ring buffer for the built-in shell console (g_consoles[0]) */
static uint32_t g_shell_history[CONSOLE_HISTORY_ROWS][CONSOLE_COLUMNS];
static uint32_t g_shell_history_count;
static uint32_t g_shell_history_oldest;

typedef struct {
    bool used;
    int32_t owner_pid;
    uint16_t row;
    uint16_t col;
    uint32_t lines_written;
    uint8_t utf8_pending[4];
    uint8_t utf8_pending_len;
    uint8_t utf8_expected_len;
    uint32_t cells[CONSOLE_ROWS * CONSOLE_COLUMNS];
} console_state_t;

static console_state_t g_consoles[CONSOLE_MAX_INSTANCES];
static uint16_t g_console_shadow[CONSOLE_COLUMNS * VGA_HEIGHT];

static void console_serial_write_char(char ch)
{
    char text[2];

    text[0] = ch;
    text[1] = '\0';
    serial_write(text);
}

static void console_update_hw_cursor(const console_state_t *console)
{
    uint16_t row;
    uint16_t col;
    uint16_t pos;

    if (console == &g_consoles[0]) {
        row = console->row >= VGA_HEIGHT ? (VGA_HEIGHT - 1U) : console->row;
        col = console->col >= VGA_WIDTH ? (VGA_WIDTH - 1U) : console->col;
        pos = (uint16_t) (row * VGA_WIDTH + col);
        outb(0x3D4, 0x0F);
        outb(0x3D5, (uint8_t) (pos & 0xFF));
        outb(0x3D4, 0x0E);
        outb(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
    }
}

static void console_put_vga_entry(uint16_t row, uint16_t col, uint32_t codepoint)
{
    uint8_t glyph = codepoint < 0x100U ? (uint8_t) codepoint : (uint8_t) '?';
    uint16_t value = ((uint16_t) VGA_ATTR << 8) | glyph;

    if (row >= VGA_HEIGHT || col >= VGA_WIDTH) {
        return;
    }
    VGA_TEXT_BUFFER[row * VGA_WIDTH + col] = value;
    g_console_shadow[row * VGA_WIDTH + col] = value;
}

static void console_clear_state(console_state_t *console)
{
    if (console == NULL) {
        return;
    }
    console->row = 0;
    console->col = 0;
    console->lines_written = 0;
    console->utf8_pending_len = 0;
    console->utf8_expected_len = 0;
    for (uint32_t i = 0; i < CONSOLE_ROWS * CONSOLE_COLUMNS; i++) {
        console->cells[i] = ' ';
    }
    if (console == &g_consoles[0]) {
        for (uint16_t row = 0; row < VGA_HEIGHT; row++) {
            for (uint16_t col = 0; col < VGA_WIDTH; col++) {
                console_put_vga_entry(row, col, ' ');
            }
        }
        console_update_hw_cursor(console);
    }
}

static void console_scroll(console_state_t *console)
{
    uint32_t slot;

    if (console == NULL) {
        return;
    }
    if (console == &g_consoles[0]) {
        slot = (g_shell_history_oldest + g_shell_history_count) % CONSOLE_HISTORY_ROWS;
        memcpy(g_shell_history[slot], console->cells, CONSOLE_COLUMNS * sizeof(uint32_t));
        if (g_shell_history_count < CONSOLE_HISTORY_ROWS) {
            g_shell_history_count++;
        } else {
            g_shell_history_oldest = (g_shell_history_oldest + 1U) % CONSOLE_HISTORY_ROWS;
        }
    }
    for (uint16_t row = 1; row < CONSOLE_ROWS; row++) {
        for (uint16_t col = 0; col < CONSOLE_COLUMNS; col++) {
            console->cells[(row - 1U) * CONSOLE_COLUMNS + col] =
                console->cells[row * CONSOLE_COLUMNS + col];
        }
    }
    for (uint16_t col = 0; col < CONSOLE_COLUMNS; col++) {
        console->cells[(CONSOLE_ROWS - 1U) * CONSOLE_COLUMNS + col] = ' ';
    }
    console->row = CONSOLE_ROWS - 1U;
}

static void console_put_codepoint(console_state_t *console, uint32_t codepoint)
{
    if (console == NULL || codepoint == 0) {
        return;
    }
    if (codepoint == '\b') {
        if (console->col > 0) {
            console->col--;
        } else if (console->row > 0) {
            console->row--;
            console->col = CONSOLE_COLUMNS - 1U;
        }
        console->cells[console->row * CONSOLE_COLUMNS + console->col] = ' ';
        if (console == &g_consoles[0]) {
            console_put_vga_entry(console->row, console->col, ' ');
            console_update_hw_cursor(console);
        }
        return;
    }
    if (codepoint == '\r') {
        console->col = 0;
        if (console == &g_consoles[0]) {
            console_update_hw_cursor(console);
        }
        return;
    }
    if (codepoint == '\n') {
        console->col = 0;
        console->row++;
        console->lines_written++;
        if (console->row >= CONSOLE_ROWS) {
            console_scroll(console);
        }
        if (console == &g_consoles[0]) {
            console_update_hw_cursor(console);
        }
        return;
    }
    if (codepoint == '\t') {
        uint16_t spaces = (uint16_t) (4U - (console->col & 3U));
        while (spaces-- > 0) {
            console_put_codepoint(console, ' ');
        }
        return;
    }
    if (console->row >= CONSOLE_ROWS) {
        console_scroll(console);
    }
    console->cells[console->row * CONSOLE_COLUMNS + console->col] = codepoint;
    if (console == &g_consoles[0]) {
        console_put_vga_entry(console->row, console->col, codepoint);
    }
    console->col++;
    if (console->col >= CONSOLE_COLUMNS) {
        console->col = 0;
        console->row++;
        if (console->row >= CONSOLE_ROWS) {
            console_scroll(console);
        }
    }
    if (console == &g_consoles[0]) {
        console_update_hw_cursor(console);
    }
}

static bool console_decode_pending(console_state_t *console)
{
    uint8_t *bytes;
    uint32_t codepoint;

    if (console == NULL || console->utf8_pending_len != console->utf8_expected_len) {
        return false;
    }
    bytes = console->utf8_pending;
    if (console->utf8_expected_len == 2U) {
        codepoint = ((uint32_t) (bytes[0] & 0x1FU) << 6) | (bytes[1] & 0x3FU);
        if (codepoint < 0x80U) {
            codepoint = 0xFFFDU;
        }
    } else if (console->utf8_expected_len == 3U) {
        codepoint = ((uint32_t) (bytes[0] & 0x0FU) << 12) |
                    ((uint32_t) (bytes[1] & 0x3FU) << 6) |
                    (bytes[2] & 0x3FU);
        if (codepoint < 0x800U || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
            codepoint = 0xFFFDU;
        }
    } else {
        codepoint = ((uint32_t) (bytes[0] & 0x07U) << 18) |
                    ((uint32_t) (bytes[1] & 0x3FU) << 12) |
                    ((uint32_t) (bytes[2] & 0x3FU) << 6) |
                    (bytes[3] & 0x3FU);
        if (codepoint < 0x10000U || codepoint > 0x10FFFFU) {
            codepoint = 0xFFFDU;
        }
    }
    console->utf8_pending_len = 0;
    console->utf8_expected_len = 0;
    console_put_codepoint(console, codepoint);
    return true;
}

static void console_feed_byte(console_state_t *console, uint8_t byte)
{
    if (console == NULL) {
        return;
    }
    if (console->utf8_pending_len == 0) {
        if (byte < 0x80U) {
            console_put_codepoint(console, byte);
        } else if (byte >= 0xC2U && byte <= 0xDFU) {
            console->utf8_pending[0] = byte;
            console->utf8_pending_len = 1;
            console->utf8_expected_len = 2;
        } else if (byte >= 0xE0U && byte <= 0xEFU) {
            console->utf8_pending[0] = byte;
            console->utf8_pending_len = 1;
            console->utf8_expected_len = 3;
        } else if (byte >= 0xF0U && byte <= 0xF4U) {
            console->utf8_pending[0] = byte;
            console->utf8_pending_len = 1;
            console->utf8_expected_len = 4;
        } else {
            console_put_codepoint(console, 0xFFFDU);
        }
        return;
    }
    if (byte < 0x80U || byte > 0xBFU) {
        console_put_codepoint(console, 0xFFFDU);
        console->utf8_pending_len = 0;
        console->utf8_expected_len = 0;
        console_feed_byte(console, byte);
        return;
    }
    console->utf8_pending[console->utf8_pending_len++] = byte;
    if (console->utf8_pending_len == console->utf8_expected_len) {
        (void) console_decode_pending(console);
    }
}

static console_state_t *console_find(int32_t pid)
{
    if (pid < 0) {
        return &g_consoles[0];
    }
    for (uint32_t i = 1; i < CONSOLE_MAX_INSTANCES; i++) {
        if (g_consoles[i].used && g_consoles[i].owner_pid == pid) {
            return &g_consoles[i];
        }
    }
    return &g_consoles[0];
}

void console_init(void)
{
    memset(g_consoles, 0, sizeof(g_consoles));
    memset(g_console_shadow, 0, sizeof(g_console_shadow));
    g_consoles[0].used = true;
    g_consoles[0].owner_pid = CONSOLE_DEFAULT_PID;
    console_clear_state(&g_consoles[0]);
}

bool console_create_for_pid(int32_t pid)
{
    if (pid < 0) {
        return false;
    }
    if (console_find(pid)->owner_pid == pid) {
        return true;
    }
    for (uint32_t i = 1; i < CONSOLE_MAX_INSTANCES; i++) {
        if (!g_consoles[i].used) {
            memset(&g_consoles[i], 0, sizeof(g_consoles[i]));
            g_consoles[i].used = true;
            g_consoles[i].owner_pid = pid;
            console_clear_state(&g_consoles[i]);
            return true;
        }
    }
    return false;
}

void console_destroy_for_pid(int32_t pid)
{
    if (pid < 0) {
        return;
    }
    for (uint32_t i = 1; i < CONSOLE_MAX_INSTANCES; i++) {
        if (g_consoles[i].used && g_consoles[i].owner_pid == pid) {
            memset(&g_consoles[i], 0, sizeof(g_consoles[i]));
            return;
        }
    }
}

void console_clear(void)
{
    console_clear_state(&g_consoles[0]);
}

void console_clear_for_pid(int32_t pid)
{
    console_clear_state(console_find(pid));
}

void console_set_cursor(uint16_t row, uint16_t col)
{
    console_state_t *console = &g_consoles[0];

    console->row = row >= CONSOLE_ROWS ? CONSOLE_ROWS - 1U : row;
    console->col = col >= CONSOLE_COLUMNS ? CONSOLE_COLUMNS - 1U : col;
    console_update_hw_cursor(console);
}

void console_move_cursor(int32_t row_delta, int32_t col_delta)
{
    int32_t row = (int32_t) g_consoles[0].row + row_delta;
    int32_t col = (int32_t) g_consoles[0].col + col_delta;

    if (row < 0) row = 0;
    if (col < 0) col = 0;
    if (row >= (int32_t) CONSOLE_ROWS) row = (int32_t) CONSOLE_ROWS - 1;
    if (col >= (int32_t) CONSOLE_COLUMNS) col = (int32_t) CONSOLE_COLUMNS - 1;
    console_set_cursor((uint16_t) row, (uint16_t) col);
}

void console_write_char(char ch)
{
    console_serial_write_char(ch);
    console_feed_byte(&g_consoles[0], (uint8_t) ch);
}

void console_write_buffer(const void *buffer, uint32_t size)
{
    const uint8_t *bytes = (const uint8_t *) buffer;

    if (bytes == NULL) {
        return;
    }
    for (uint32_t i = 0; i < size; i++) {
        console_serial_write_char((char) bytes[i]);
        console_feed_byte(&g_consoles[0], bytes[i]);
    }
}

void console_write_process_char(int32_t pid, char ch)
{
    console_write_process_buffer(pid, &ch, 1);
}

void console_write_process_buffer(int32_t pid, const void *buffer, uint32_t size)
{
    const uint8_t *bytes = (const uint8_t *) buffer;
    console_state_t *console = console_find(pid);

    if (bytes == NULL) {
        return;
    }
    for (uint32_t i = 0; i < size; i++) {
        console_serial_write_char((char) bytes[i]);
        console_feed_byte(console, bytes[i]);
    }
}

void console_write(const char *str)
{
    if (str == NULL) {
        return;
    }
    console_write_buffer(str, (uint32_t) strlen(str));
}

void console_write_at(uint16_t row, uint16_t col, const char *str)
{
    if (str == NULL || row >= VGA_HEIGHT) {
        return;
    }
    while (*str != '\0' && col < VGA_WIDTH) {
        g_consoles[0].cells[row * CONSOLE_COLUMNS + col] = (uint8_t) *str++;
        console_put_vga_entry(row, col++, (uint8_t) str[-1]);
    }
}

void console_backspace(void)
{
    console_put_codepoint(&g_consoles[0], '\b');
    serial_write("\b \b");
}

uint16_t console_cursor_row(void)
{
    return g_consoles[0].row;
}

uint16_t console_cursor_col(void)
{
    return g_consoles[0].col;
}

const uint32_t *console_buffer(void)
{
    return g_consoles[0].cells;
}

void console_snapshot_take(uint32_t *out_cells, uint16_t *row, uint16_t *col)
{
    memcpy(out_cells, g_consoles[0].cells, sizeof(g_consoles[0].cells));
    if (row != NULL) { *row = g_consoles[0].row; }
    if (col != NULL) { *col = g_consoles[0].col; }
}

void console_snapshot_restore(const uint32_t *cells, uint16_t row, uint16_t col)
{
    memcpy(g_consoles[0].cells, cells, sizeof(g_consoles[0].cells));
    g_consoles[0].row = row;
    g_consoles[0].col = col;
}

const uint32_t *console_buffer_for_pid(int32_t pid)
{
    return console_find(pid)->cells;
}

uint16_t console_cursor_row_for_pid(int32_t pid)
{
    return console_find(pid)->row;
}

uint16_t console_cursor_col_for_pid(int32_t pid)
{
    return console_find(pid)->col;
}

uint32_t console_lines_for_pid(int32_t pid)
{
    return console_find(pid)->lines_written;
}

uint32_t console_history_rows_for_pid(int32_t pid)
{
    if (pid < 0) {
        return g_shell_history_count;
    }
    return 0;
}

uint32_t console_history_cell_for_pid(int32_t pid, uint32_t row, uint32_t col)
{
    uint32_t idx;

    if (pid < 0 && row < g_shell_history_count && col < CONSOLE_COLUMNS) {
        idx = (g_shell_history_oldest + row) % CONSOLE_HISTORY_ROWS;
        return g_shell_history[idx][col];
    }
    return ' ';
}

uint32_t console_scheme_bg(void)
{
    return g_console_schemes[g_console_scheme_idx].bg;
}

uint32_t console_scheme_fg(void)
{
    return g_console_schemes[g_console_scheme_idx].fg;
}

int console_scheme(void)
{
    return (int)g_console_scheme_idx;
}

void console_set_scheme(int idx)
{
    if (idx >= 0 && (uint32_t)idx < CONSOLE_SCHEME_COUNT) {
        g_console_scheme_idx = (uint32_t)idx;
    }
}

int console_scheme_count(void)
{
    return (int)CONSOLE_SCHEME_COUNT;
}

const char *console_scheme_name(int idx)
{
    static const char *names[CONSOLE_SCHEME_COUNT] = {
        "default", "solarized", "dracula", "monokai", "light"
    };
    if (idx >= 0 && (uint32_t)idx < CONSOLE_SCHEME_COUNT) {
        return names[idx];
    }
    return "default";
}

