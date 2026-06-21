#include "kernel.h"
#include "usb.h"

static bool legacy_compat;

bool init_usb(void)
{
    legacy_compat = true;
    log_write("usb: native host stack not present");
    log_write("usb: legacy emulation compatibility mode enabled");
    return legacy_compat;
}

bool usb_legacy_compat(void)
{
    return legacy_compat;
}
