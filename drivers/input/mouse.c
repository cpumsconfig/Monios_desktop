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
#define PS2_WAIT_LIMIT 100000U

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
    bool five_button;
    bool present;
    int32_t sensitivity;
    uint16_t draw_row;
    uint16_t draw_col;
    uint16_t saved_cell;
    bool drawn;
} mouse_driver_state_t;

static mouse_driver_state_t mouse_state;

static bool ps2_wait_input_empty(void)
{
    for (uint32_t i = 0; i < PS2_WAIT_LIMIT; i++) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0) {
            return true;
        }
    }
    return false;
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

static bool ps2_write_command(uint8_t value)
{
    if (!ps2_wait_input_empty()) {
        return false;
    }
    outb(PS2_COMMAND_PORT, value);
    return true;
}

static bool ps2_write_data(uint8_t value)
{
    if (!ps2_wait_input_empty()) {
        return false;
    }
    outb(PS2_DATA_PORT, value);
    return true;
}

static uint8_t ps2_read_data(void)
{
    return inb(PS2_DATA_PORT);
}

static bool mouse_write_device(uint8_t value)
{
    return ps2_write_command(0xD4) && ps2_write_data(value);
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
    if (!mouse_write_device(0xF3)) {
        return false;
    }
    if (!mouse_expect_ack()) {
        return false;
    }
    if (!mouse_write_device(rate)) {
        return false;
    }
    return mouse_expect_ack();
}

static uint8_t mouse_get_device_id(void)
{
    if (!mouse_write_device(0xF2)) {
        return 0xFF;
    }
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
        uint32_t width = graphics_framebuffer_width();
        uint32_t height = graphics_framebuffer_height();
        uint16_t gx = (uint16_t) (mouse_state.x_pixels > (int32_t) width - 1 ? (int32_t) (width - 1) : mouse_state.x_pixels);
        uint16_t gy = (uint16_t) (mouse_state.y_pixels > (int32_t) height - 1 ? (int32_t) (height - 1) : mouse_state.y_pixels);
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
    if (mouse_state.sensitivity != 0) {
        dx = (dx * mouse_state.sensitivity) / 100;
        dy = (dy * mouse_state.sensitivity) / 100;
    }
    mouse_state.x_accum += dx;
    mouse_state.y_accum -= dy;

    mouse_state.x_pixels += mouse_state.x_accum;
    mouse_state.y_pixels += mouse_state.y_accum;
    mouse_state.x_accum = 0;
    mouse_state.y_accum = 0;

    if (mouse_state.x_pixels < 0) mouse_state.x_pixels = 0;
    if (mouse_state.y_pixels < 0) mouse_state.y_pixels = 0;
    if (graphics_active()) {
        int32_t width = (int32_t) graphics_framebuffer_width();
        int32_t height = (int32_t) graphics_framebuffer_height();

        if (width < 1) width = 1;
        if (height < 1) height = 1;
        if (mouse_state.x_pixels > width - 1) mouse_state.x_pixels = width - 1;
        if (mouse_state.y_pixels > height - 1) mouse_state.y_pixels = height - 1;
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
    mouse_state.five_button = false;
    mouse_state.sensitivity = 100;
    mouse_state.drawn = false;

    while ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0) {
        (void) ps2_read_data();
    }

    if (!ps2_write_command(0xA8) || !ps2_write_command(0x20)) {
        log_write("mouse: controller command timeout");
        mouse_redraw_cursor();
        return;
    }
    if (!ps2_wait_output_full_with_timeout(1000000)) {
        log_write("mouse: controller config timeout");
        mouse_redraw_cursor();
        return;
    }

    config = ps2_read_data();
    config |= 0x03;
    config &= (uint8_t) ~0x20;
    if (!ps2_write_command(0x60) || !ps2_write_data(config)) {
        log_write("mouse: controller config write timeout");
        mouse_redraw_cursor();
        return;
    }

    if (!mouse_write_device(0xF6) || !mouse_expect_ack()) {
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
        /* Second identification sequence promotes to 5-button (IntelliMouse Explorer). */
        if (mouse_set_sample_rate(200) &&
            mouse_set_sample_rate(200) &&
            mouse_set_sample_rate(80) &&
            mouse_get_device_id() == 0x04) {
            mouse_state.five_button = true;
            log_write("mouse: 5-button packet mode enabled");
        }
    }

    if (!mouse_write_device(0xF4) || !mouse_expect_ack()) {
        log_write("mouse: enable-streaming ack failed");
        mouse_redraw_cursor();
        return;
    }

    mouse_state.present = true;
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
        if (mouse_state.five_button) {
            /* extra buttons 4/5 live in byte[3] bits 4/5 */
            if (mouse_state.packet[3] & 0x10) mouse_state.buttons |= 0x08;
            if (mouse_state.packet[3] & 0x20) mouse_state.buttons |= 0x10;
        }
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

int32_t mouse_consume_wheel_delta(void)
{
    int32_t delta = mouse_state.wheel_delta;

    mouse_state.wheel_delta = 0;
    return delta;
}

/* --- USB HID boot-protocol mouse injection --------------------------- */
/* Called by drivers/usb/hid.c when a USB mouse interrupt-IN report
 * arrives.  dx/dy/wheel are signed 8-bit movement deltas; buttons is a
 * bitmask of MOUSE_BUTTON_* (left/right/middle).  Merges into the same
 * cursor state used by the PS/2 driver so the rest of the stack (UI,
 * windows) sees a single unified pointer. */
void mouse_inject_usb_report(int8_t dx, int8_t dy, int8_t wheel, uint8_t buttons)
{
    mouse_state.buttons = (uint8_t) (buttons & 0x07u);
    if (wheel != 0) {
        mouse_state.wheel_delta += wheel;
    }
    mouse_apply_movement((int32_t) dx, (int32_t) dy);
}

/* ------------------------------------------------------------------ */
/*  Probe / control / generic read-write interface                     */
/* ------------------------------------------------------------------ */

bool mouse_probe(void)
{
    uint8_t id;

    /* Flush the controller buffer. */
    while ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0) {
        (void) ps2_read_data();
    }

    if (!ps2_write_command(0xA8)) {
        log_write("mouse: not found (controller)");
        return false;
    }
    if (!mouse_write_device(0xF6) || !mouse_expect_ack()) {
        log_write("mouse: not found (no ack)");
        return false;
    }
    id = mouse_get_device_id();
    if (id == 0xFF) {
        log_write("mouse: not found");
        return false;
    }
    log_write("mouse: detected");
    return true;
}

bool mouse_present(void)
{
    return mouse_state.present;
}

bool mouse_write(uint8_t command)
{
    if (!mouse_write_device(command)) {
        return false;
    }
    return mouse_expect_ack();
}

bool mouse_set_resolution(uint8_t resolution)
{
    if (!mouse_write_device(0xE8)) {
        return false;
    }
    if (!mouse_expect_ack()) {
        return false;
    }
    if (!mouse_write_device(resolution)) {
        return false;
    }
    return mouse_expect_ack();
}

bool mouse_set_sample_rate_public(uint8_t rate)
{
    return mouse_set_sample_rate(rate);
}

void mouse_set_sensitivity(uint32_t percent)
{
    if (percent < 25) percent = 25;
    if (percent > 400) percent = 400;
    mouse_state.sensitivity = (int32_t) percent;
}

void mouse_read_state(mouse_snapshot_t *snapshot)
{
    mouse_get_snapshot(snapshot);
}

const char *mouse_status_text(void)
{
    return mouse_state.present ? "mouse: ready" : "mouse: not found";
}
