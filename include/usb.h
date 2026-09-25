#ifndef _USB_H_
#define _USB_H_

#include "stdbool.h"
#include "stdint.h"

bool init_usb(void);
bool usb_legacy_compat(void);
bool usb_native_host_present(void);

/* USB input device (mouse/keyboard) hotplug registration.
 * iface_protocol: 1 = keyboard, 2 = mouse. */
int  usb_input_register(uint8_t slot_id, uint8_t iface_protocol,
                       uint16_t vendor_id, uint16_t product_id);
void usb_input_unregister(uint8_t slot_id);
uint32_t usb_input_count(void);

#endif
