#ifndef _WIFI_H_
#define _WIFI_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool initialized;
    bool present;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t irq;
    char status[64];
} wifi_info_t;

void wifi_init(void);
const wifi_info_t *wifi_info(void);
const char *wifi_status(void);

#endif
