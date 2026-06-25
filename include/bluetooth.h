#ifndef _BLUETOOTH_H_
#define _BLUETOOTH_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool initialized;
    bool controller_present;
    bool usb_transport_ready;
    bool hci_ready;
    uint32_t controllers;
    char status[64];
} bluetooth_info_t;

void bluetooth_init(void);
const bluetooth_info_t *bluetooth_info(void);
const char *bluetooth_status(void);

#endif
