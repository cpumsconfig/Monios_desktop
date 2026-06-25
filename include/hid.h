#ifndef _HID_H_
#define _HID_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool legacy_keyboard;
    bool legacy_mouse;
    bool usb_legacy;
    bool xhci_present;
    uint32_t key_events_seen;
    int32_t mouse_x;
    int32_t mouse_y;
    uint8_t mouse_buttons;
    char status[64];
} hid_info_t;

void hid_init(void);
void hid_update(void);
const hid_info_t *hid_info(void);
const char *hid_status(void);

#endif
