#include "kernel.h"
#include "usb.h"
#include "xhci.h"
#include "hid.h"
#include "string.h"

static bool legacy_compat;
static bool native_host;

/* Track registered USB input devices so we can enumerate/unregister on
 * hot-unplug.  Up to 4 USB mice + 4 USB keyboards. */
#define USB_INPUT_MAX 8

typedef struct {
    bool     present;
    uint8_t  slot_id;
    uint8_t  iface_protocol;   /* 1=keyboard, 2=mouse */
    uint16_t vendor_id;
    uint16_t product_id;
} usb_input_dev_t;

static usb_input_dev_t g_usb_inputs[USB_INPUT_MAX];
static uint32_t g_usb_input_count;

bool init_usb(void)
{
    native_host = xhci_info()->present;
    legacy_compat = true;
    memset(g_usb_inputs, 0, sizeof(g_usb_inputs));
    g_usb_input_count = 0;
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

/* --- USB input device registration (hotplug) ------------------------- */

/* Register a newly-attached USB input device (mouse or keyboard).
 * Returns the slot index, or -1 on failure. */
int usb_input_register(uint8_t slot_id, uint8_t iface_protocol,
                       uint16_t vendor_id, uint16_t product_id)
{
    uint32_t i;

    for (i = 0; i < USB_INPUT_MAX; i++) {
        if (!g_usb_inputs[i].present) {
            break;
        }
    }
    if (i >= USB_INPUT_MAX) {
        return -1;
    }
    g_usb_inputs[i].present = true;
    g_usb_inputs[i].slot_id = slot_id;
    g_usb_inputs[i].iface_protocol = iface_protocol;
    g_usb_inputs[i].vendor_id = vendor_id;
    g_usb_inputs[i].product_id = product_id;
    g_usb_input_count++;
    /* Delegate to the HID layer, which posts a desktop toast and wires the
     * report stream into the mouse/keyboard event queues. */
    hid_device_connected(slot_id, iface_protocol, vendor_id, product_id);
    return (int) i;
}

/* Unregister a USB input device on hot-unplug. */
void usb_input_unregister(uint8_t slot_id)
{
    uint32_t i;

    for (i = 0; i < USB_INPUT_MAX; i++) {
        if (g_usb_inputs[i].present && g_usb_inputs[i].slot_id == slot_id) {
            g_usb_inputs[i].present = false;
            g_usb_input_count--;
            hid_device_disconnected(slot_id);
            return;
        }
    }
}

/* Number of currently-attached USB input devices. */
uint32_t usb_input_count(void)
{
    return g_usb_input_count;
}
