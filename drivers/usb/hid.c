#include "common.h"
#include "hid.h"
#include "keyboard.h"
#include "mouse.h"
#include "usb.h"
#include "usb_ext.h"
#include "xhci.h"
#include "graphics.h"
#include "kernel.h"
#include "string.h"

static hid_info_t g_hid_info;
static uint8_t g_last_function;

/* Per-device USB HID state. */
#define HID_MAX_DEVICES 8
typedef enum { HID_NONE = 0, HID_MOUSE, HID_KEYBOARD } hid_dev_kind_t;

typedef struct {
    bool        present;
    uint8_t     slot_id;
    hid_dev_kind_t kind;
    uint16_t    vendor_id;
    uint16_t    product_id;
    bool        boot_protocol;
    uint8_t     prev_mouse[4];
} hid_device_t;

static hid_device_t g_hid_devs[HID_MAX_DEVICES];

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
    memset(g_hid_devs, 0, sizeof(g_hid_devs));
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

/* =========================================================================
 * HID report-descriptor parsing (minimal).
 *
 * A real HID driver walks the 8-bit items and builds a field/usage table.
 * For boot-protocol devices (mouse / keyboard) the firmware is required to
 * emit fixed report formats, so we short-circuit: if the interface protocol
 * is 0x01 (keyboard) or 0x02 (mouse) we use the boot report layout directly
 * without fully decoding the descriptor.  The descriptor walk below still
 * validates that the first HID class-specific descriptor looks sane.
 * ========================================================================= */

/* HID main item tags. */
#define HID_ITEM_INPUT        0x80u
#define HID_ITEM_OUTPUT       0x90u
#define HID_ITEM_COLLECTION   0xA0u
#define HID_ITEM_END_COL      0xC0u

/* Parse a report descriptor buffer.  Returns true if it looks like a valid
 * HID report descriptor (has at least one collection item). */
bool hid_parse_report_descriptor(const uint8_t *desc, uint32_t len)
{
    uint32_t i = 0;
    bool saw_collection = false;

    if (desc == NULL || len < 2u) {
        return false;
    }
    while (i < len) {
        uint8_t item = desc[i];
        uint8_t tag = (uint8_t) (item & 0xF0u);
        uint8_t size = (uint8_t) (item & 0x03u);

        if (tag == HID_ITEM_COLLECTION) {
            saw_collection = true;
        }
        if (size == 3u && item == 0x7Bu) {
            size = 4u;
        }
        i += (uint32_t) size + 1u;
    }
    return saw_collection;
}

/* =========================================================================
 * Boot-protocol report processing.
 * ========================================================================= */

/* Process a USB mouse boot report (4 bytes: buttons, dx, dy, wheel).
 * Buttons byte: bit0=left bit1=right bit2=middle. */
void hid_process_mouse_report(const uint8_t *report, uint32_t len)
{
    int8_t dx, dy, wheel;
    uint8_t buttons;

    if (report == NULL || len < 4u) {
        return;
    }
    buttons = (uint8_t) (report[0] & 0x07u);
    dx = (int8_t) report[1];
    dy = (int8_t) report[2];
    wheel = (int8_t) report[3];
    mouse_inject_usb_report(dx, dy, wheel, buttons);
    g_hid_info.mouse_buttons = buttons;
}

/* Process a USB keyboard boot report (8 bytes, see keyboard.c). */
void hid_process_keyboard_report(const uint8_t *report, uint32_t len)
{
    keyboard_inject_usb_boot(report, len);
    g_hid_info.key_events_seen++;
}

/* =========================================================================
 * Hotplug: called by usb_ext.c when a HID interface attaches/detaches.
 * ========================================================================= */

/* iface_protocol is the HID interface descriptor's bInterfaceProtocol:
 * 1 = keyboard, 2 = mouse, 0 = no boot subclass (kind stays HID_NONE until a
 * report-descriptor sniff resolves it upstream).  These are the same values
 * usb.h documents for usb_input_register(). */
void hid_device_connected(uint8_t slot_id, uint8_t iface_protocol,
                          uint16_t vendor_id, uint16_t product_id)
{
    uint32_t i;
    bool already = false;
    hid_dev_kind_t kind = HID_NONE;
    const char *name = NULL;

    /* Match by slot first: a re-probe of an already-present device must update
     * that record, not allocate a second one - otherwise the device table fills
     * up with duplicates and every re-probe posts another desktop toast. */
    for (i = 0; i < HID_MAX_DEVICES; i++) {
        if (g_hid_devs[i].present && g_hid_devs[i].slot_id == slot_id) {
            already = true;
            break;
        }
    }
    if (!already) {
        for (i = 0; i < HID_MAX_DEVICES; i++) {
            if (!g_hid_devs[i].present) {
                break;
            }
        }
    }
    if (i >= HID_MAX_DEVICES) {
        return;
    }

    switch (iface_protocol) {
    case 1u: kind = HID_KEYBOARD; name = "USB键盘已连接"; break;
    case 2u: kind = HID_MOUSE;    name = "USB鼠标已连接"; break;
    default: kind = HID_NONE; break;
    }

    g_hid_devs[i].present = true;
    g_hid_devs[i].slot_id = slot_id;
    g_hid_devs[i].kind = kind;
    g_hid_devs[i].vendor_id = vendor_id;
    g_hid_devs[i].product_id = product_id;
    g_hid_devs[i].boot_protocol = (kind != HID_NONE);

    if (name != NULL && !already) {
        graphics_notification_post("USB输入设备", name);
        log_write("hid: usb HID interface attached");
    }
}

void hid_device_disconnected(uint8_t slot_id)
{
    uint32_t i;

    for (i = 0; i < HID_MAX_DEVICES; i++) {
        if (g_hid_devs[i].present && g_hid_devs[i].slot_id == slot_id) {
            const char *name = (g_hid_devs[i].kind == HID_MOUSE) ? "USB鼠标已拔出" :
                               (g_hid_devs[i].kind == HID_KEYBOARD) ? "USB键盘已拔出" : "USB设备已拔出";
            graphics_notification_post("USB输入设备", name);
            g_hid_devs[i].present = false;
            g_hid_devs[i].kind = HID_NONE;
            return;
        }
    }
}

/* =========================================================================
 * New unified HID driver interface.
 * ========================================================================= */

/* HID class request bmRequestType bits */
#define HID_RT_CLASS_OUT   0x21u
#define HID_RT_CLASS_IN    0xA1u
#define HID_RT_STD_IN_IF   0x81u

/* HID bRequest codes (class) */
#define HID_REQ_GET_REPORT     0x01u
#define HID_REQ_GET_IDLE       0x02u
#define HID_REQ_GET_PROTOCOL   0x03u
#define HID_REQ_SET_REPORT     0x09u
#define HID_REQ_SET_IDLE       0x0Au
#define HID_REQ_SET_PROTOCOL   0x0Bu

/* HID report types for SET_REPORT */
#define HID_REPORT_INPUT       0x01u
#define HID_REPORT_OUTPUT     0x02u
#define HID_REPORT_FEATURE    0x03u

int32_t hid_set_boot_protocol(uint8_t slot)
{
    /* bRequest=SET_PROTOCOL(0x0B), wValue=0 (boot), wIndex=interface */
    return usb_control_request(slot, HID_RT_CLASS_OUT, HID_REQ_SET_PROTOCOL,
                                0u, 0u, NULL, 0u);
}

int32_t hid_set_idle(uint8_t slot, uint8_t duration)
{
    return usb_control_request(slot, HID_RT_CLASS_OUT, HID_REQ_SET_IDLE,
                               (uint16_t) duration << 8, 0u, NULL, 0u);
}

int32_t hid_get_report_descriptor(uint8_t slot, void *buf, uint32_t len)
{
    /* Standard GET_DESCRIPTOR, descriptor type 0x22 (HID Report) */
    return usb_control_request(slot, HID_RT_STD_IN_IF, USB_REQ_GET_DESCRIPTOR,
                               (uint16_t) (0x22u << 8), 0u, buf,
                               (uint16_t) (len > 0xFFu ? 0xFFu : len));
}

int32_t hid_set_output_report(uint8_t slot, uint8_t report_id,
                              const void *data, uint32_t len)
{
    uint8_t buf[64];
    uint32_t total = len + 1u;

    if (total > sizeof(buf)) {
        total = (uint32_t) sizeof(buf);
    }
    buf[0] = report_id;
    if (data != NULL && len > 0u) {
        memcpy(buf + 1, data, len);
    }
    /* SET_REPORT: wValue = (Output report type << 8) | report_id */
    return usb_control_request(slot, HID_RT_CLASS_OUT, HID_REQ_SET_REPORT,
                               (uint16_t) ((HID_REPORT_OUTPUT << 8) | report_id),
                               0u, buf, (uint16_t) total);
}

int32_t hid_set_keyboard_led(uint8_t slot, uint8_t leds)
{
    return hid_set_output_report(slot, 0u, &leds, 1u);
}

/* ── Report descriptor field parser ─────────────────────────────── */
bool hid_parse_report_info(const uint8_t *desc, uint32_t len,
                           hid_report_info_t *out)
{
    uint32_t i = 0;
    uint16_t usage_page = 0u;
    uint8_t report_size = 0u;
    uint8_t report_count = 0u;
    uint16_t in_bits = 0u, out_bits = 0u, feat_bits = 0u;
    bool saw_collection = false;

    if (desc == NULL || out == NULL || len < 2u) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    while (i < len) {
        uint8_t item = desc[i];
        uint8_t tag = (uint8_t) (item & 0xF0u);
        uint8_t bsize = (uint8_t) (item & 0x03u);
        uint32_t data = 0u;
        uint32_t j;

        if (bsize == 3u) bsize = 4u;
        for (j = 0; j < bsize && (i + 1u + j) < len; j++) {
            data |= (uint32_t) desc[i + 1u + j] << (j * 8u);
        }
        i += (uint32_t) bsize + 1u;

        /* Global items (tag high nibble == 0) */
        if (tag == 0x04u) {            /* Usage Page */
            usage_page = (uint16_t) data;
        } else if (tag == 0x74u) {     /* Report Size */
            report_size = (uint8_t) data;
        } else if (tag == 0x94u) {     /* Report Count */
            report_count = (uint8_t) data;
        } else if (tag == 0x80u) {     /* Input main item */
            in_bits += (uint16_t) report_size * (uint16_t) report_count;
        } else if (tag == 0x90u) {     /* Output main item */
            out_bits += (uint16_t) report_size * (uint16_t) report_count;
        } else if (tag == 0xB0u) {     /* Feature main item */
            feat_bits += (uint16_t) report_size * (uint16_t) report_count;
        } else if (tag == 0xA0u) {     /* Collection */
            saw_collection = true;
        }
    }

    out->usage_page = usage_page;
    out->report_size_bits = report_size;
    out->report_count = report_count;
    out->input_report_bytes = (uint16_t) ((in_bits + 7u) / 8u);
    out->output_report_bytes = (uint16_t) ((out_bits + 7u) / 8u);
    out->feature_report_bytes = (uint16_t) ((feat_bits + 7u) / 8u);
    out->has_data = saw_collection;
    return saw_collection;
}

/* ── Generic interrupt read / write ────────────────────────────── */
int32_t hid_read(uint8_t slot, void *report, uint32_t len)
{
    if (slot == 0u || report == NULL || len == 0u) {
        return -1;
    }
    /* Interrupt IN on endpoint 0x81 (default HID interrupt-IN) */
    return usb_interrupt_transfer(slot, 0x81u, report, len);
}

int32_t hid_write(uint8_t slot, const void *report, uint32_t len)
{
    if (slot == 0u || report == NULL || len == 0u) {
        return -1;
    }
    /* Interrupt OUT on endpoint 0x01 */
    return usb_interrupt_transfer(slot, 0x01u,
                                  (void *) (uintptr_t) report, len);
}

/* ── Probe: enumerate HID interfaces on the USB stack ───────────── */
/* Re-assert bring-up for every HID device already in the enumeration table:
 * boot protocol + idle-off, then register with the input subsystem. Called
 * once after usb_probe() so a device that enumerated before hid_init() ran
 * still gets its class-specific setup. hid_device_connected() is idempotent
 * per slot, so the earlier hotplug registration is updated, not duplicated. */
bool hid_probe(void)
{
    uint32_t count = 0;

    for (uint32_t i = 0; i < USB_DEV_MAX; i++) {
        const usb_device_t *dev = usb_device_get(i);
        if (dev == NULL) {
            continue;
        }
        if (dev->iface_class == USB_IF_CLASS_HID ||
            dev->device_class == USB_IF_CLASS_HID) {
            count++;
            /* Bring the HID device up: boot protocol + idle off */
            if (dev->iface_protocol == USB_HID_PROTO_KEYBOARD ||
                dev->iface_protocol == USB_HID_PROTO_MOUSE) {
                (void) hid_set_boot_protocol(dev->slot_id);
            }
            (void) hid_set_idle(dev->slot_id, 0u);
            hid_device_connected(dev->slot_id, dev->iface_protocol,
                                 dev->vendor_id, dev->product_id);
        }
    }

    g_hid_info.xhci_present = xhci_info()->present;
    if (count == 0u) {
        strcpy(g_hid_info.status, "hid: not found");
        log_write("hid: not found");
        return false;
    }
    strcpy(g_hid_info.status, "hid: HID devices enumerated");
    kernel_log_hex_u32("hid: HID devices enumerated ",
                       (uint32_t) count);
    return true;
}
