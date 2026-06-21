#ifndef _POWER_H_
#define _POWER_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool initialized;
    bool acpi_ready;
    bool power_button_ready;
    bool cpu_frequency_detected;
    bool device_power_ready;
    uint16_t sci_irq;
    char status[64];
} power_info_t;

void power_init(void);
void power_refresh(void);
const power_info_t *power_info(void);
const char *power_status(void);

#endif
