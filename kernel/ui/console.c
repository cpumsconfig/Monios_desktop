#include "common.h"
#include "console.h"
#include "kernel.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_ATTR 0x07
#define VGA_TEXT_BUFFER ((volatile uint16_t *) 0xB8000)

static uint16_t console_row;
static uint16_t console_col;
static uint16_t console_shadow[VGA_WIDTH * VGA_HEIGHT];

static void console_serial_write_char(char ch)
{
    char text[2];

    text[0] = ch;
    text[1] = '\0';
    serial_write(text);
}

static void console_update_hw_cursor(void)
{
    uint16_t pos = (uint16_t) (console_row * VGA_WIDTH + console_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t) (pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
}

static void console_put_entry(uint16_t row, uint16_t col, char ch)
{
    uint16_t value = ((uint16_t) VGA_ATTR << 8) | (uint8_t) ch;
    VGA_TEXT_BUFFER[row * VGA_WIDTH + col] = value;
    console_shadow[row * VGA_WIDTH + col] = value;
}

static void console_scroll(void)
{
    for (uint16_t row = 1; row < VGA_HEIGHT; row++) {
        for (uint16_t col = 0; col < VGA_WIDTH; col++) {
            VGA_TEXT_BUFFER[(row - 1) * VGA_WIDTH + col] = VGA_TEXT_BUFFER[row * VGA_WIDTH + col];
            console_shadow[(row - 1) * VGA_WIDTH + col] = console_shadow[row * VGA_WIDTH + col];
        }
    }
    for (uint16_t col = 0; col < VGA_WIDTH; col++) {
        console_put_entry(VGA_HEIGHT - 1, col, ' ');
    }
    console_row = VGA_HEIGHT - 1;
}

void console_clear(void)
{
    for (uint16_t row = 0; row < VGA_HEIGHT; row++) {
        for (uint16_t col = 0; col < VGA_WIDTH; col++) {
            console_put_entry(row, col, ' ');
        }
    }
    console_row = 0;
    console_col = 0;
    console_update_hw_cursor();
}

void console_set_cursor(uint16_t row, uint16_t col)
{
    console_row = row >= VGA_HEIGHT ? (VGA_HEIGHT - 1) : row;
    console_col = col >= VGA_WIDTH ? (VGA_WIDTH - 1) : col;
    console_update_hw_cursor();
}

void console_move_cursor(int32_t row_delta, int32_t col_delta)
{
    int32_t row = (int32_t) console_row + row_delta;
    int32_t col = (int32_t) console_col + col_delta;

    if (row < 0) row = 0;
    if (col < 0) col = 0;
    if (row >= VGA_HEIGHT) row = VGA_HEIGHT - 1;
    if (col >= VGA_WIDTH) col = VGA_WIDTH - 1;

    console_set_cursor((uint16_t) row, (uint16_t) col);
}

void console_write_char(char ch)
{
    if (ch == '\b') {
        console_backspace();
        return;
    }
    if (ch == '\r') {
        console_serial_write_char('\r');
        console_col = 0;
        console_update_hw_cursor();
        return;
    }
    if (ch == '\n') {
        console_serial_write_char('\n');
        console_col = 0;
        console_row++;
        if (console_row >= VGA_HEIGHT) {
            console_scroll();
        }
        console_update_hw_cursor();
        return;
    }

    console_serial_write_char(ch);
    console_put_entry(console_row, console_col, ch);
    console_col++;
    if (console_col >= VGA_WIDTH) {
        console_col = 0;
        console_row++;
        if (console_row >= VGA_HEIGHT) {
            console_scroll();
        }
    }
    console_update_hw_cursor();
}

void console_backspace(void)
{
    if (console_col == 0) {
        if (console_row == 0) {
            console_update_hw_cursor();
            return;
        }
        console_row--;
        console_col = VGA_WIDTH - 1;
    } else {
        console_col--;
    }

    serial_write("\b \b");
    console_put_entry(console_row, console_col, ' ');
    console_update_hw_cursor();
}

void console_write(const char *str)
{
    while (*str) {
        console_write_char(*str++);
    }
}

void console_write_at(uint16_t row, uint16_t col, const char *str)
{
    while (*str && row < VGA_HEIGHT && col < VGA_WIDTH) {
        console_put_entry(row, col, *str++);
        col++;
    }
}

uint16_t console_cursor_row(void)
{
    return console_row;
}

uint16_t console_cursor_col(void)
{
    return console_col;
}

const uint16_t *console_buffer(void)
{
    return console_shadow;
}
