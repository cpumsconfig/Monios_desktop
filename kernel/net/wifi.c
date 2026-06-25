#include "common.h"
#include "pci.h"
#include "wifi.h"

static wifi_info_t g_wifi_info;

static bool wifi_probe_cb(const pci_device_info_t *info, void *ctx)
{
    wifi_info_t *wifi = (wifi_info_t *) ctx;

    if (info->class_code == 0x02 && info->subclass == 0x80) {
        wifi->present = true;
        wifi->vendor_id = info->vendor_id;
        wifi->device_id = info->device_id;
        wifi->bus = info->bus;
        wifi->slot = info->slot;
        wifi->func = info->func;
        wifi->irq = info->interrupt_line;
        return false;
    }
    return true;
}

void wifi_init(void)
{
    memset(&g_wifi_info, 0, sizeof(g_wifi_info));
    g_wifi_info.initialized = true;
    pci_enumerate(wifi_probe_cb, &g_wifi_info);
    strcpy(g_wifi_info.status, g_wifi_info.present ? "wifi: controller detected" : "wifi: no controller");
}

const wifi_info_t *wifi_info(void)
{
    return &g_wifi_info;
}

const char *wifi_status(void)
{
    return g_wifi_info.status;
}
