#include "common.h"
#include "kernel.h"
#include "pci.h"
#include "smbus.h"

static bool g_smbus_present;
static char g_smbus_status[64];

bool smbus_driver_init(void)
{
    pci_device_info_t info;

    g_smbus_present = pci_find_first(0x0C, 0x05, &info);
    if (g_smbus_present) {
        strcpy(g_smbus_status, "smbus: pci controller detected");
    } else {
        strcpy(g_smbus_status, "smbus: no controller detected");
    }
    log_write(g_smbus_status);
    return true;
}

void smbus_init(void)
{
}

void smbus_driver_shutdown(void)
{
    if (g_smbus_present) {
        strcpy(g_smbus_status, "smbus: shutdown");
        log_write(g_smbus_status);
    }
    g_smbus_present = false;
}

bool smbus_available(void)
{
    return g_smbus_present;
}

const char *smbus_status(void)
{
    return g_smbus_status;
}
