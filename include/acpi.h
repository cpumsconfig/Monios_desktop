#ifndef _ACPI_H_
#define _ACPI_H_

#include "stdbool.h"
#include "stdint.h"

#define ACPI_MADT_CPU_ID_MAX 32U

typedef struct {
    bool ready;
    bool madt_ready;
    bool power_button_ready;
    bool power_button_hooked;
    bool sleep_ready;
    bool reset_ready;
    uint32_t madt_lapic_address;
    uint32_t madt_processor_count;
    uint32_t madt_enabled_processor_count;
    uint32_t madt_enabled_lapic_id_count;
    uint32_t madt_enabled_lapic_ids[ACPI_MADT_CPU_ID_MAX];
    uint16_t sci_irq;
    uint16_t pm1a_cnt;
    uint16_t pm1b_cnt;
    uint16_t pm1a_evt;
    uint16_t pm1b_evt;
    uint16_t slp_typa;
    uint16_t slp_typb;
    uint8_t sleep_state;
    uint16_t sleep_slp_typa;
    uint16_t sleep_slp_typb;
    uint16_t s3_slp_typa;
    uint16_t s3_slp_typb;
} acpi_info_t;

void acpi_init(uint64_t boot_rsdp);
void acpi_enable_power_button(void);
bool acpi_poweroff(void);
bool acpi_reboot(void);
bool acpi_sleep(void);
const acpi_info_t *acpi_info(void);
const char *acpi_status(void);

#endif
