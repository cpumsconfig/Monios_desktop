#include "common.h"
#include "usb.h"
#include "usb_ext.h"
#include "xhci.h"

static usb_ext_info_t g_usb_ext_info;

void usb_ext_refresh(void)
{
    const xhci_info_t *xhci = xhci_info();

    g_usb_ext_info.legacy_ready = usb_legacy_compat();
    g_usb_ext_info.native_host_present = usb_native_host_present();
    g_usb_ext_info.xhci_present = xhci->present;
    g_usb_ext_info.root_ports = xhci->max_ports;
    g_usb_ext_info.max_slots = xhci->max_slots;
    strcpy(g_usb_ext_info.status,
           g_usb_ext_info.xhci_present ? "usbext: xhci root hub described" : "usbext: legacy usb bridge");
}

void usb_ext_init(void)
{
    memset(&g_usb_ext_info, 0, sizeof(g_usb_ext_info));
    g_usb_ext_info.initialized = true;
    usb_ext_refresh();
}

const usb_ext_info_t *usb_ext_info(void)
{
    usb_ext_refresh();
    return &g_usb_ext_info;
}

const char *usb_ext_status(void)
{
    usb_ext_refresh();
    return g_usb_ext_info.status;
}
