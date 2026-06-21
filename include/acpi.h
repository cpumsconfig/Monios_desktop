#ifndef _ACPI_H_
#define _ACPI_H_

#include "stdbool.h"

void acpi_init(void);
void acpi_enable_power_button(void);
bool acpi_poweroff(void);

#endif
