#ifndef _MOUSE_H_
#define _MOUSE_H_

#include "stdint.h"

#define MOUSE_BUTTON_LEFT   0x01
#define MOUSE_BUTTON_RIGHT  0x02
#define MOUSE_BUTTON_MIDDLE 0x04

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

#endif
