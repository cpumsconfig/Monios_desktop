#include "common.h"
#include "console.h"
#include "graphics.h"
#include "kernel.h"
#include "mouse.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_TEXT_BUFFER ((volatile uint16_t *) 0xB8000)

#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_COMMAND_PORT 0x64

#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_INPUT_FULL  0x02

#define MOUSE_CURSOR_CHAR 0xDB
#define MOUSE_CURSOR_ATTR 0x0F

typedef struct {
    int32_t x_pixels;
    int32_t y_pixels;
    int32_t x_accum;
    int32_t y_accum;
    int32_t wheel_delta;
    uint8_t buttons;
    uint8_t packet[4];
    uint8_t packet_index;
    uint8_t packet_size;
    bool wheel_enabled;
    uint16_t draw_row;
    uint16_t draw_col;
    uint16_t saved_cell;
    bool drawn;
} mouse_driver_state_t;

static mouse_driver_state_t mouse_state;

static void ps2_wait_input_empty(void)
{
    while ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) != 0) {
    }
}

static bool ps2_wait_output_full_with_timeout(uint32_t timeout)
{
    while (timeout-- > 0) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0) {
            return true;
        }
    }
    return false;
}

static void ps2_write_command(uint8_t value)
{
    ps2_wait_input_empty();
    outb(PS2_COMMAND_PORT, value);
}

static void ps2_write_data(uint8_t value)
{
    ps2_wait_input_empty();
    outb(PS2_DATA_PORT, value);
}

static uint8_t ps2_read_data(void)
{
    return inb(PS2_DATA_PORT);
}

static void mouse_write_device(uint8_t value)
{
    ps2_write_command(0xD4);
    ps2_write_data(value);
}

static bool mouse_expect_ack(void)
{
    if (!ps2_wait_output_full_with_timeout(1000000)) {
        return false;
    }
    return ps2_read_data() == 0xFA;
}

static bool mouse_set_sample_rate(uint8_t rate)
{
    mouse_write_device(0xF3);
    if (!mouse_expect_ack()) {
        return false;
    }
    mouse_write_device(rate);
    return mouse_expect_ack();
}

static uint8_t mouse_get_device_id(void)
{
    mouse_write_device(0xF2);
    if (!mouse_expect_ack()) {
        return 0xFF;
    }
    if (!ps2_wait_output_full_with_timeout(1000000)) {
        return 0xFF;
    }
    return ps2_read_data();
}

static void mouse_restore_cursor(void)
{
    if (graphics_active()) {
        return;
    }
    if (!mouse_state.drawn) {
        return;
    }

    VGA_TEXT_BUFFER[mouse_state.draw_row * VGA_WIDTH + mouse_state.draw_col] = mouse_state.saved_cell;
    mouse_state.drawn = false;
}

void mouse_redraw_cursor(void)
{
    if (graphics_active()) {
        uint16_t gx = (uint16_t) (mouse_state.x_pixels > GRAPHICS_WIDTH - 1 ? GRAPHICS_WIDTH - 1 : mouse_state.x_pixels);
        uint16_t gy = (uint16_t) (mouse_state.y_pixels > GRAPHICS_HEIGHT - 1 ? GRAPHICS_HEIGHT - 1 : mouse_state.y_pixels);
        graphics_mouse_redraw(gx, gy);
        return;
    }

    uint16_t row;
    uint16_t col;
    uint16_t index;

    mouse_restore_cursor();

    col = (uint16_t) (mouse_state.x_pixels / 8);
    row = (uint16_t) (mouse_state.y_pixels / 16);

    if (col >= VGA_WIDTH) col = VGA_WIDTH - 1;
    if (row >= VGA_HEIGHT) row = VGA_HEIGHT - 1;

    index = (uint16_t) (row * VGA_WIDTH + col);
    mouse_state.saved_cell = VGA_TEXT_BUFFER[index];
    VGA_TEXT_BUFFER[index] = ((uint16_t) MOUSE_CURSOR_ATTR << 8) | MOUSE_CURSOR_CHAR;
    mouse_state.draw_row = row;
    mouse_state.draw_col = col;
    mouse_state.drawn = true;
}

static void mouse_apply_movement(int32_t dx, int32_t dy)
{
    mouse_state.x_accum += dx;
    mouse_state.y_accum -= dy;

    mouse_state.x_pixels += mouse_state.x_accum;
    mouse_state.y_pixels += mouse_state.y_accum;
    mouse_state.x_accum = 0;
    mouse_state.y_accum = 0;

    if (mouse_state.x_pixels < 0) mouse_state.x_pixels = 0;
    if (mouse_state.y_pixels < 0) mouse_state.y_pixels = 0;
    if (graphics_active()) {
        if (mouse_state.x_pixels > GRAPHICS_WIDTH - 1) mouse_state.x_pixels = GRAPHICS_WIDTH - 1;
        if (mouse_state.y_pixels > GRAPHICS_HEIGHT - 1) mouse_state.y_pixels = GRAPHICS_HEIGHT - 1;
    } else {
        if (mouse_state.x_pixels > 639) mouse_state.x_pixels = 639;
        if (mouse_state.y_pixels > 399) mouse_state.y_pixels = 399;
    }

    mouse_redraw_cursor();
}

void init_mouse(void)
{
    uint8_t config;

    mouse_state.x_pixels = 320;
    mouse_state.y_pixels = 200;
    mouse_state.wheel_delta = 0;
    mouse_state.buttons = 0;
    mouse_state.packet_index = 0;
    mouse_state.packet_size = 3;
    mouse_state.wheel_enabled = false;
    mouse_state.drawn = false;

    while ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0) {
        (void) ps2_read_data();
    }

    ps2_write_command(0xA8);
    ps2_write_command(0x20);
    if (!ps2_wait_output_full_with_timeout(1000000)) {
        log_write("mouse: controller config timeout");
        mouse_redraw_cursor();
        return;
    }

    config = ps2_read_data();
    config |= 0x03;
    config &= (uint8_t) ~0x20;
    ps2_write_command(0x60);
    ps2_write_data(config);

    mouse_write_device(0xF6);
    if (!mouse_expect_ack()) {
        log_write("mouse: reset-defaults ack failed");
        mouse_redraw_cursor();
        return;
    }

    if (mouse_set_sample_rate(200) &&
        mouse_set_sample_rate(100) &&
        mouse_set_sample_rate(80) &&
        mouse_get_device_id() == 0x03) {
        mouse_state.packet_size = 4;
        mouse_state.wheel_enabled = true;
        log_write("mouse: wheel packet mode enabled");
    }

    mouse_write_device(0xF4);
    if (!mouse_expect_ack()) {
        log_write("mouse: enable-streaming ack failed");
        mouse_redraw_cursor();
        return;
    }

    log_write("mouse: ps/2 mouse ready");
    mouse_redraw_cursor();
}

void mouse_interrupt_dispatch(void)
{
    uint8_t data;
    uint8_t status;
    int32_t dx;
    int32_t dy;
    int8_t wheel = 0;

    status = inb(PS2_STATUS_PORT);
    if ((status & PS2_STATUS_OUTPUT_FULL) == 0) {
        return;
    }

    data = ps2_read_data();
    if (mouse_state.packet_index == 0 && (data & 0x08) == 0) {
        return;
    }

    mouse_state.packet[mouse_state.packet_index++] = data;
    if (mouse_state.packet_index < mouse_state.packet_size) {
        return;
    }
    mouse_state.packet_index = 0;

    dx = (int32_t) (int8_t) mouse_state.packet[1];
    dy = (int32_t) (int8_t) mouse_state.packet[2];
    mouse_state.buttons = (uint8_t) (mouse_state.packet[0] & 0x07);
    if (mouse_state.wheel_enabled && mouse_state.packet_size == 4) {
        wheel = (int8_t) ((mouse_state.packet[3] & 0x08) != 0 ? (mouse_state.packet[3] | 0xF0) : (mouse_state.packet[3] & 0x0F));
        mouse_state.wheel_delta += wheel;
    }
    mouse_apply_movement(dx, dy);
}

void mouse_get_snapshot(mouse_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    snapshot->x_pixels = mouse_state.x_pixels;
    snapshot->y_pixels = mouse_state.y_pixels;
    snapshot->wheel_delta = mouse_state.wheel_delta;
    snapshot->buttons = mouse_state.buttons;
    snapshot->packet_size = mouse_state.packet_size;
    snapshot->wheel_enabled = mouse_state.wheel_enabled ? 1 : 0;
    snapshot->reserved = 0;
}
