#ifndef _USB_H_
#define _USB_H_

#include "stdbool.h"

bool init_usb(void);
bool usb_legacy_compat(void);
bool usb_native_host_present(void);

#endif
