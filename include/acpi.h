#ifndef _ACPI_H_
#define _ACPI_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool ready;
    bool power_button_ready;
    bool power_button_hooked;
    uint16_t sci_irq;
    uint16_t pm1a_cnt;
    uint16_t pm1b_cnt;
    uint16_t pm1a_evt;
    uint16_t pm1b_evt;
    uint16_t slp_typa;
    uint16_t slp_typb;
} acpi_info_t;

void acpi_init(void);
void acpi_enable_power_button(void);
bool acpi_poweroff(void);
const acpi_info_t *acpi_info(void);
const char *acpi_status(void);

#endif
