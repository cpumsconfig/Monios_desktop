#include "common.h"
#include "hid.h"
#include "keyboard.h"
#include "mouse.h"
#include "usb.h"
#include "xhci.h"

static hid_info_t g_hid_info;
static uint8_t g_last_function;

static void hid_refresh(void)
{
    const keyboard_status_t *kbd = keyboard_status();
    mouse_snapshot_t mouse;

    mouse_get_snapshot(&mouse);
    g_hid_info.legacy_keyboard = true;
    g_hid_info.legacy_mouse = true;
    g_hid_info.usb_legacy = usb_legacy_compat();
    g_hid_info.xhci_present = usb_native_host_present() || xhci_info()->present;
    g_hid_info.mouse_x = mouse.x_pixels;
    g_hid_info.mouse_y = mouse.y_pixels;
    g_hid_info.mouse_buttons = mouse.buttons;
    if (kbd->last_function != g_last_function) {
        g_last_function = kbd->last_function;
        g_hid_info.key_events_seen++;
    }
    strcpy(g_hid_info.status, g_hid_info.xhci_present ? "hid: xhci/legacy bridge" : "hid: ps2/legacy input");
}

void hid_init(void)
{
    memset(&g_hid_info, 0, sizeof(g_hid_info));
    g_last_function = keyboard_status()->last_function;
    hid_refresh();
}

void hid_update(void)
{
    hid_refresh();
}

const hid_info_t *hid_info(void)
{
    hid_refresh();
    return &g_hid_info;
}

const char *hid_status(void)
{
    hid_refresh();
    return g_hid_info.status;
}
