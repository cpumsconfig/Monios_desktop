#include "bluetooth.h"
#include "common.h"
#include "usb.h"
#include "xhci.h"

static bluetooth_info_t g_bluetooth_info;

void bluetooth_init(void)
{
    memset(&g_bluetooth_info, 0, sizeof(g_bluetooth_info));
    g_bluetooth_info.initialized = true;
    g_bluetooth_info.usb_transport_ready = usb_native_host_present() || xhci_info()->present;
    g_bluetooth_info.controller_present = false;
    g_bluetooth_info.hci_ready = false;
    g_bluetooth_info.controllers = 0;
    strcpy(g_bluetooth_info.status,
           g_bluetooth_info.usb_transport_ready ? "bluetooth: usb hci transport ready" : "bluetooth: waiting for usb host");
}

const bluetooth_info_t *bluetooth_info(void)
{
    return &g_bluetooth_info;
}

const char *bluetooth_status(void)
{
    return g_bluetooth_info.status;
}
