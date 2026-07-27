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
    g_power_info.sleep_ready = acpi->sleep_ready;
    g_power_info.reset_ready = acpi->reset_ready;
    g_power_info.cpu_frequency_detected = cpu->has_msr && cpu->has_tsc;
    g_power_info.device_power_ready = g_power_info.acpi_ready;
    g_power_info.sci_irq = acpi->sci_irq;
    g_power_info.sleep_state = acpi->sleep_state;
    if (g_power_info.acpi_ready && g_power_info.sleep_ready && g_power_info.reset_ready) {
        strcpy(g_power_info.status, "power: acpi shutdown/reboot/sleep ready");
    } else if (g_power_info.acpi_ready && g_power_info.sleep_ready) {
        strcpy(g_power_info.status, "power: acpi shutdown/sleep ready");
    } else if (g_power_info.acpi_ready) {
        strcpy(g_power_info.status, "power: acpi shutdown ready");
    } else {
        strcpy(g_power_info.status, "power: fallback pm");
    }
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
