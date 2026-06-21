#include "kernel.h"
#include "usb.h"
#include "xhci.h"

static bool legacy_compat;
static bool native_host;

bool init_usb(void)
{
    native_host = xhci_info()->present;
    legacy_compat = true;
    if (native_host) {
        log_write("usb: xhci native host detected");
    } else {
        log_write("usb: native host stack not present");
    }
    log_write("usb: legacy emulation compatibility mode enabled");
    return legacy_compat;
}

bool usb_legacy_compat(void)
{
    return legacy_compat;
}

bool usb_native_host_present(void)
{
    return native_host;
}
