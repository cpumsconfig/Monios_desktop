#ifndef _MOUSE_H_
#define _MOUSE_H_

#include "stdbool.h"
#include "stdint.h"

#define MOUSE_BUTTON_LEFT   0x01
#define MOUSE_BUTTON_RIGHT  0x02
#define MOUSE_BUTTON_MIDDLE 0x04
#define MOUSE_BUTTON_SIDE1  0x08
#define MOUSE_BUTTON_SIDE2  0x10

typedef struct {
    int32_t x_pixels;
    int32_t y_pixels;
    int32_t wheel_delta;
    uint8_t buttons;
    uint8_t packet_size;
    uint8_t wheel_enabled;
    uint8_t reserved;
} mouse_snapshot_t;

void init_mouse(void);
void mouse_interrupt_dispatch(void);
void mouse_redraw_cursor(void);
void mouse_get_snapshot(mouse_snapshot_t *snapshot);
int32_t mouse_consume_wheel_delta(void);

/* USB HID boot-protocol mouse report injection (drivers/usb/hid.c). */
void mouse_inject_usb_report(int8_t dx, int8_t dy, int8_t wheel, uint8_t buttons);

/* Probe / control / generic read-write interface */
bool mouse_probe(void);
bool mouse_present(void);
bool mouse_write(uint8_t command);
bool mouse_set_resolution(uint8_t resolution);
bool mouse_set_sample_rate_public(uint8_t rate);
void mouse_set_sensitivity(uint32_t percent);
void mouse_read_state(mouse_snapshot_t *snapshot);
const char *mouse_status_text(void);

#endif
