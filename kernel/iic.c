#include "iic.h"
#include "common.h"
#include "pci.h"
#include "smbus.h"

static iic_info_t g_iic_info;

void iic_init(void)
{
    memset(&g_iic_info, 0, sizeof(g_iic_info));
    g_iic_info.available = smbus_available();
    g_iic_info.intel_style = smbus_is_intel();
    g_iic_info.adapters = g_iic_info.available ? 1u : 0u;
    strcpy(g_iic_info.status, g_iic_info.available ? "iic: smbus bridge online" : "iic: no adapter");
}

bool iic_probe(uint8_t address)
{
    g_iic_info.last_probe = address;
    if (!g_iic_info.available) {
        strcpy(g_iic_info.status, "iic: adapter unavailable");
        return false;
    }
    if (smbus_probe_device(address)) {
        strcpy(g_iic_info.status, "iic: device acknowledged");
        return true;
    }
    strcpy(g_iic_info.status, "iic: no response");
    return false;
}

uint32_t iic_scan(uint8_t *buffer, uint32_t capacity)
{
    uint32_t count = 0;

    if (!g_iic_info.available || buffer == NULL || capacity == 0) {
        return 0;
    }
    for (uint8_t address = 0x03; address <= 0x77 && count < capacity; address++) {
        if (smbus_probe_device(address)) {
            buffer[count++] = address;
        }
    }
    g_iic_info.found_count = (uint8_t) count;
    strcpy(g_iic_info.status, count > 0 ? "iic: scan complete" : "iic: scan empty");
    return count;
}

const iic_info_t *iic_info(void)
{
    g_iic_info.available = smbus_available();
    g_iic_info.intel_style = smbus_is_intel();
    g_iic_info.adapters = g_iic_info.available ? 1u : 0u;
    return &g_iic_info;
}

const char *iic_status(void)
{
    return iic_info()->status;
}
