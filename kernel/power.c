#include "acpi.h"
#include "common.h"
#include "cpu.h"
#include "power.h"

static power_info_t g_power_info;

void power_refresh(void)
{
    const acpi_info_t *acpi = acpi_info();
    const cpu_info_t *cpu = cpu_current_info();

    g_power_info.acpi_ready = acpi->ready;
    g_power_info.power_button_ready = acpi->power_button_ready;
    g_power_info.cpu_frequency_detected = cpu->has_msr && cpu->has_tsc;
    g_power_info.device_power_ready = g_power_info.acpi_ready;
    g_power_info.sci_irq = acpi->sci_irq;
    strcpy(g_power_info.status,
           g_power_info.acpi_ready ? "power: acpi/device pm ready" : "power: fallback pm");
}

void power_init(void)
{
    memset(&g_power_info, 0, sizeof(g_power_info));
    g_power_info.initialized = true;
    power_refresh();
}

const power_info_t *power_info(void)
{
    power_refresh();
    return &g_power_info;
}

const char *power_status(void)
{
    power_refresh();
    return g_power_info.status;
}
