#ifndef _USB_EXT_H_
#define _USB_EXT_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool initialized;
    bool legacy_ready;
    bool native_host_present;
    bool xhci_present;
    uint32_t root_ports;
    uint32_t max_slots;
    char status[64];
} usb_ext_info_t;

void usb_ext_init(void);
void usb_ext_refresh(void);
const usb_ext_info_t *usb_ext_info(void);
const char *usb_ext_status(void);

#endif
