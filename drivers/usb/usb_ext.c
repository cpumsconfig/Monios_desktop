#include "common.h"
#include "usb.h"
#include "usb_ext.h"
#include "xhci.h"
#include "hid.h"
#include "kernel.h"
#include "file.h"
#include "graphics.h"
#include "string.h"

static usb_ext_info_t g_usb_ext_info;

/* ── Feature 20: device tables ─────────────────────────────────── */
static usb_msc_device_t  g_msc[USB_MSC_MAX_DEVICES];
static usb_printer_t     g_printers[USB_PRINTER_MAX];
static char g_next_drive = USB_MSC_FIRST_DRIVE;   /* reserved for the blockdev bridge */

/* ── Enumerated device table (new) ──────────────────────────────── */
static usb_device_t g_usb_devs[USB_DEV_MAX];

static void msc_record_name(usb_msc_device_t *d)
{
    /* Build "USB-DISK-<slot>" style label */
    const char *base = "USB-DISK";
    uint32_t j = 0;
    while (base[j] && j < sizeof(d->name) - 2) { d->name[j] = base[j]; j++; }
    d->name[j++] = '0' + (char)d->slot_id;
    d->name[j] = '\0';
}

static void printer_record_name(usb_printer_t *p)
{
    const char *base = "USB-PRN-";
    uint32_t j = 0;
    while (base[j] && j < sizeof(p->name) - 2) { p->name[j] = base[j]; j++; }
    p->name[j++] = '0' + (char)p->slot_id;
    p->name[j] = '\0';
}

/* Derive device kind from class codes */
static uint8_t classify(uint8_t device_class, uint8_t iface_class)
{
    uint8_t c = (device_class == USB_DEV_CLASS_INTERFACE) ? iface_class : device_class;
    switch (c) {
    case USB_IF_CLASS_AUDIO:         return USBDEV_AUDIO;
    case USB_IF_CLASS_MASS_STORAGE: return USBDEV_MSC;
    case USB_IF_CLASS_PRINTER:       return USBDEV_PRINTER;
    case USB_IF_CLASS_HID:           return USBDEV_HID;
    case USB_IF_CLASS_HUB:           return USBDEV_HUB;
    default:                         return USBDEV_OTHER;
    }
}

/* Work out whether a HID interface is a keyboard or a mouse.
 *
 * The interface protocol field is the cheap answer - 1 = keyboard, 2 = mouse -
 * but 0 means the device declares no boot subclass and only speaks the report
 * protocol. In that case the only portable signal is the report descriptor:
 * every keyboard and mouse opens with Usage Page (Generic Desktop, 0x05 0x01)
 * followed by Usage (0x09) Keyboard (0x06) or Mouse (0x02).
 *
 * Returns USB_HID_PROTO_KEYBOARD, USB_HID_PROTO_MOUSE, or USB_HID_PROTO_NONE
 * when neither can be established (the caller then leaves the kind unset). */
static uint8_t usb_hid_sniff_protocol(uint8_t slot, uint8_t iface_protocol)
{
    uint8_t desc[48];
    int32_t n;

    if (iface_protocol == USB_HID_PROTO_KEYBOARD ||
        iface_protocol == USB_HID_PROTO_MOUSE) {
        return iface_protocol;
    }

    memset(desc, 0, sizeof(desc));
    n = hid_get_report_descriptor(slot, desc, sizeof(desc));
    if (n < 4) {
        return USB_HID_PROTO_NONE;
    }
    for (int32_t i = 0; i + 3 < n; i++) {
        if (desc[i] != 0x05u || desc[i + 1] != 0x01u || desc[i + 2] != 0x09u) {
            continue;
        }
        if (desc[i + 3] == 0x06u) {
            return USB_HID_PROTO_KEYBOARD;
        }
        if (desc[i + 3] == 0x02u) {
            return USB_HID_PROTO_MOUSE;
        }
    }
    return USB_HID_PROTO_NONE;
}

void usbdev_hotplug(uint8_t slot_id, uint8_t device_class, uint8_t iface_class,
                    uint8_t iface_protocol, uint16_t vendor_id,
                    uint16_t product_id, bool attached)
{
    uint8_t kind = classify(device_class, iface_class);

    if (!attached) {
        /* Look up by slot and clear it from whichever table it lives in. */
        for (int i = 0; i < USB_MSC_MAX_DEVICES; i++) {
            if (g_msc[i].present && g_msc[i].slot_id == slot_id) {
                char body[24];
                if (g_msc[i].mounted && g_msc[i].drive_letter != 0) {
                    strcpy(body, "U盘已从 X: 拔出");
                    body[8] = g_msc[i].drive_letter;
                    graphics_notification_post("USB大容量存储", body);
                }
                g_msc[i].present = false;
                g_msc[i].mounted = false;
                g_msc[i].drive_letter = 0;
            }
        }
        for (int i = 0; i < USB_PRINTER_MAX; i++) {
            if (g_printers[i].present && g_printers[i].slot_id == slot_id) {
                g_printers[i].present = false;
            }
        }
        /* Tear down any HID input device on this slot. */
        usb_input_unregister(slot_id);
        return;
    }

    if (kind == USBDEV_HID) {
        /* HID class (0x03): keyboard or mouse. The protocol used to be
         * hardwired to 2u here, which registered every USB keyboard as a
         * mouse - the device enumerated fine and then typed into nothing. */
        uint8_t proto = usb_hid_sniff_protocol(slot_id, iface_protocol);

        /* Boot protocol fixes the report layout that
         * hid_process_keyboard_report() / hid_process_mouse_report() decode.
         * Only ask for it when the interface actually declares the boot
         * subclass (protocol 1 or 2); a stalling SET_PROTOCOL would just add
         * noise to an otherwise healthy boot log. */
        if (iface_protocol == USB_HID_PROTO_KEYBOARD ||
            iface_protocol == USB_HID_PROTO_MOUSE) {
            (void) hid_set_boot_protocol(slot_id);
        }
        /* SET_IDLE is mandatory for every HID device, and idle-off makes the
         * device report on change instead of only on its poll interval. */
        (void) hid_set_idle(slot_id, 0u);
        (void) usb_input_register(slot_id, proto, vendor_id, product_id);
        return;
    }

    if (kind == USBDEV_MSC) {
        for (int i = 0; i < USB_MSC_MAX_DEVICES; i++) {
            if (!g_msc[i].present) {
                memset(&g_msc[i], 0, sizeof(g_msc[i]));
                g_msc[i].present = true;
                g_msc[i].slot_id = slot_id;
                g_msc[i].vendor_id = vendor_id;
                g_msc[i].product_id = product_id;
                g_msc[i].sector_size = 512;
                g_msc[i].sector_count = 0;   /* read from READ CAPACITY later */
                msc_record_name(&g_msc[i]);
                /* Positive evidence, for the same reason usb_probe() logs one
                 * line per enumerated device: a registered disk that never
                 * mounts is otherwise indistinguishable from no disk at all. */
                log_write_event("usb: mass-storage registered as ",
                                g_msc[i].name);
                kernel_log_hex_u32("usb: msc slot:vendor ",
                                   ((uint32_t) slot_id << 16) |
                                   (uint32_t) vendor_id);

                /* Mounting is deliberately NOT attempted here.
                 *
                 * file_mount(path, "fat32", -1) resolves the volume through the
                 * *block-device layer*, and a USB MSC device is not in that layer
                 * yet (see the integration seam documented in usb_ext.h). Calling
                 * it anyway does not mount the stick: the FAT32 driver probes
                 * block device 0, finds the boot disk, mounts it a *second* time
                 * under the next free drive letter and runs FAT32 WAL recovery on
                 * it again - two writers on one FAT, which is exactly the kind of
                 * thing that corrupts a volume. So the drive letter stays unset
                 * until blockdev.c fronts the bulk-only path. */
                log_write("usb: mass-storage awaiting blockdev bridge (no drive letter yet)");
                return;
            }
        }
    } else if (kind == USBDEV_PRINTER) {
        for (int i = 0; i < USB_PRINTER_MAX; i++) {
            if (!g_printers[i].present) {
                memset(&g_printers[i], 0, sizeof(g_printers[i]));
                g_printers[i].present = true;
                g_printers[i].slot_id = slot_id;
                g_printers[i].vendor_id = vendor_id;
                g_printers[i].product_id = product_id;
                g_printers[i].port_status = 0x00;
                printer_record_name(&g_printers[i]);
                return;
            }
        }
    }
}

/* In the skeleton there is no live xHCI transaction engine, so "rescan"
 * reconciles the tallies against the controller the hardware layer exposed.
 * A real roothub driver would walk PORTSC and call usbdev_hotplug() per port. */
void usb_msc_rescan(void)
{
    const xhci_info_t *xhci = xhci_info();
    (void)xhci;
    /* Tally present records */
    uint32_t msc = 0, mnt = 0, prn = 0;
    for (int i = 0; i < USB_MSC_MAX_DEVICES; i++) {
        if (g_msc[i].present) { msc++; if (g_msc[i].mounted) mnt++; }
    }
    for (int i = 0; i < USB_PRINTER_MAX; i++) {
        if (g_printers[i].present) prn++;
    }
    g_usb_ext_info.msc_devices = msc;
    g_usb_ext_info.msc_mounted = mnt;
    g_usb_ext_info.printer_devices = prn;
}

/* ── Mass storage mount / drive letters ─────────────────────────── */
int usb_msc_count(void)
{
    int n = 0;
    for (int i = 0; i < USB_MSC_MAX_DEVICES; i++) if (g_msc[i].present) n++;
    return n;
}

const usb_msc_device_t *usb_msc_get(int index)
{
    int seen = -1;
    for (int i = 0; i < USB_MSC_MAX_DEVICES; i++) {
        if (g_msc[i].present) {
            seen++;
            if (seen == index) return &g_msc[i];
        }
    }
    return NULL;
}

uint32_t usb_msc_sector_size(const usb_msc_device_t *d) { return d ? d->sector_size : 0; }
uint64_t usb_msc_sector_count(const usb_msc_device_t *d) { return d ? d->sector_count : 0; }

int usb_msc_mount_all(void)
{
    int present = 0;

    for (int i = 0; i < USB_MSC_MAX_DEVICES; i++) {
        if (g_msc[i].present && !g_msc[i].mounted) {
            present++;
        }
    }
    if (present > 0) {
        /* Refused on purpose - the same reason usbdev_hotplug() does not mount:
         * file_mount(..., -1) would resolve to block device 0, i.e. the boot
         * disk, and publish it under a second drive letter. Returning the
         * number of devices we *could* mount would be a lie; returning 0 with a
         * reason in the log is not. */
        log_write("usb: msc mount refused - no blockdev bridge for bulk-only transport");
        return 0;
    }
    usb_msc_rescan();
    return 0;
}

bool usb_msc_eject(char drive_letter)
{
    for (int i = 0; i < USB_MSC_MAX_DEVICES; i++) {
        if (g_msc[i].present && g_msc[i].mounted && g_msc[i].drive_letter == drive_letter) {
            g_msc[i].mounted = false;
            g_msc[i].drive_letter = 0;
            usb_msc_rescan();
            return true;
        }
    }
    return false;
}

/* ── Printer basics ────────────────────────────────────────────── */
int usb_printer_count(void)
{
    int n = 0;
    for (int i = 0; i < USB_PRINTER_MAX; i++) if (g_printers[i].present) n++;
    return n;
}

const usb_printer_t *usb_printer_get(int index)
{
    int seen = -1;
    for (int i = 0; i < USB_PRINTER_MAX; i++) {
        if (g_printers[i].present) {
            seen++;
            if (seen == index) return &g_printers[i];
        }
    }
    return NULL;
}

/* GET_PORT_STATUS class control transfer (0x01, interface recip).
 * Real xHCI enqueue fills a 1-byte status; skeleton models a sane idle state. */
int usb_printer_poll_status(int index)
{
    const usb_printer_t *p = usb_printer_get(index);
    if (p == NULL) return -1;
    /* bit0=not-error, bit1=select, bit2=paper-out(inverted). 0x18 = selected+on-line. */
    uint8_t status = 0x18;
    for (int i = 0; i < USB_PRINTER_MAX; i++) {
        if (g_printers[i].present &&
            g_printers[i].slot_id == p->slot_id) {
            g_printers[i].port_status = status;
        }
    }
    return (int)status;
}

int usb_printer_write(int index, const void *data, uint32_t len)
{
    (void)data;
    const usb_printer_t *p = usb_printer_get(index);
    if (p == NULL) return -1;
    if (!(p->port_status & 0x10)) return -2;   /* not selected */
    for (int i = 0; i < USB_PRINTER_MAX; i++) {
        if (g_printers[i].present && g_printers[i].slot_id == p->slot_id) {
            g_printers[i].jobs_completed++;
        }
    }
    return (int)len;
}

/* ── SYS_USB_MOUNT_CTL handler ─────────────────────────────────── */
uint64_t usb_mount_ctl(uint64_t sub, uint64_t arg1, uint64_t arg2)
{
    switch (sub) {
    case USB_MCTL_ENUM_MSC:
        return (uint64_t)usb_msc_count();
    case USB_MCTL_MOUNT_ALL:
        return (uint64_t)usb_msc_mount_all();
    case USB_MCTL_EJECT:
        return usb_msc_eject((char)arg1) ? 0 : (uint64_t)-1;
    case USB_MCTL_GET_DRIVE: {
        const usb_msc_device_t *d = usb_msc_get((int)arg1);
        if (d == NULL || !d->mounted) return 0;
        return (uint64_t)(uint8_t)d->drive_letter;
    }
    case USB_MCTL_ENUM_PRINTER:
        return (uint64_t)usb_printer_count();
    case USB_MCTL_PRINTER_STATUS:
        return (uint64_t)(uint32_t)usb_printer_poll_status((int)arg1);
    case USB_MCTL_PRINTER_WRITE: {
        /* arg2 points to {const void *data; uint32_t len;} in caller space.
         * Kernel context only (no app_memory translation here). */
        const uint64_t *p = (const uint64_t *)(uint64_t)arg2;
        if (p == 0) return (uint64_t)-1;
        const void *data = (const void *)(uint64_t)p[0];
        uint32_t len = (uint32_t)p[1];
        return (uint64_t)usb_printer_write((int)arg1, data, len);
    }
    default:
        return (uint64_t)-1;
    }
}

/* ── New device-stack: control / bulk / interrupt transfers ─────── */
int32_t usb_control_request(uint8_t slot, uint8_t bmRequestType,
                            uint8_t bRequest, uint16_t wValue,
                            uint16_t wIndex, void *data, uint16_t wLength)
{
    xhci_setup_packet_t setup;

    if (slot == 0u) {
        return -1;
    }
    setup.bmRequestType = bmRequestType;
    setup.bRequest = bRequest;
    setup.wValue = wValue;
    setup.wIndex = wIndex;
    setup.wLength = wLength;
    return xhci_control_transfer(slot, &setup, data, (uint32_t) wLength);
}

int32_t usb_get_device_descriptor(uint8_t slot, usb_device_desc_t *desc)
{
    if (desc == NULL) {
        return -1;
    }
    memset(desc, 0, sizeof(*desc));
    return usb_control_request(slot,
                               USB_REQ_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                               (uint16_t) (USB_DESC_DEVICE << 8), 0u,
                               desc, (uint16_t) sizeof(*desc));
}

int32_t usb_get_config_descriptor(uint8_t slot, uint8_t index,
                                  void *buf, uint32_t len)
{
    if (buf == NULL || len == 0u) {
        return -1;
    }
    return usb_control_request(slot,
                               USB_REQ_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                               (uint16_t) ((USB_DESC_CONFIGURATION << 8) | index), 0u,
                               buf, (uint16_t) len);
}

int32_t usb_set_configuration(uint8_t slot, uint8_t config)
{
    return usb_control_request(slot,
                               USB_REQ_DIR_OUT, USB_REQ_SET_CONFIGURATION,
                               (uint16_t) config, 0u, NULL, 0u);
}

int32_t usb_bulk_transfer(uint8_t slot, uint8_t endpoint,
                          void *data, uint32_t len, bool is_in)
{
    return xhci_bulk_transfer(slot, endpoint, data, len, is_in);
}

int32_t usb_interrupt_transfer(uint8_t slot, uint8_t endpoint,
                               void *data, uint32_t len)
{
    return xhci_interrupt_transfer(slot, endpoint, data, len);
}

int32_t usb_read(uint8_t slot, uint8_t endpoint, void *data, uint32_t len)
{
    return xhci_bulk_transfer(slot, endpoint, data, len, true);
}

int32_t usb_write(uint8_t slot, uint8_t endpoint, const void *data, uint32_t len)
{
    return xhci_bulk_transfer(slot, endpoint, (void *) (uintptr_t) data, len, false);
}

uint32_t usb_device_count(void)
{
    uint32_t n = 0;
    for (int i = 0; i < USB_DEV_MAX; i++) {
        if (g_usb_devs[i].present) n++;
    }
    return n;
}

const usb_device_t *usb_device_get(uint32_t index)
{
    uint32_t seen = 0;
    for (int i = 0; i < USB_DEV_MAX; i++) {
        if (g_usb_devs[i].present) {
            if (seen == index) return &g_usb_devs[i];
            seen++;
        }
    }
    return NULL;
}

/* Bound on how much of a configuration descriptor we are willing to pull. A
 * device's config block is a few dozen bytes; 512 covers a multi-interface
 * composite device without needing a dynamic allocation. The buffers are
 * static because the kernel stack does not want another half kilobyte. */
#define USB_CONFIG_DESC_MAX   512u

/* Bring the device on `slot` from Addressed to Configured:
 *   1. read the configuration descriptor (header first, then the whole block)
 *   2. walk the descriptor chain and collect every endpoint it advertises
 *   3. SET_CONFIGURATION
 *   4. hand the endpoint list to the xHCI driver, which enables them
 *
 * Step 4 is what actually makes non-zero endpoints usable: Address Device alone
 * leaves the device with EP0 only, so bulk and interrupt transfers cannot
 * succeed no matter how correct the transfer ring is.
 *
 * The first IN and OUT endpoint addresses found are reported back so the device
 * table records the addresses the device really has instead of a guess, together
 * with interface 0's class / subclass / protocol - the fields that identify a
 * per-interface device to its class driver.
 * Returns the number of endpoints enabled, or negative on failure. */
static int usb_configure_device(uint8_t slot, uint8_t *out_ep_in,
                                uint8_t *out_ep_out,
                                uint8_t *out_iface_class,
                                uint8_t *out_iface_subclass,
                                uint8_t *out_iface_protocol)
{
    static uint8_t buf[USB_CONFIG_DESC_MAX] __attribute__((aligned(16)));
    static xhci_ep_config_t eps[XHCI_MAX_CONFIGURED_EPS];
    const usb_config_desc_t *cfg = (const usb_config_desc_t *) (const void *) buf;
    uint16_t total;
    uint32_t off;
    uint8_t n_eps = 0u;
    bool seen_iface = false;

    *out_ep_in = 0u;
    *out_ep_out = 0u;
    *out_iface_class = 0u;
    *out_iface_subclass = 0u;
    *out_iface_protocol = 0u;

    /* Header first: asking for more than a device holds makes some of them
     * stall the request outright, so learn wTotalLength before the full read. */
    memset(buf, 0, sizeof(buf));
    if (usb_get_config_descriptor(slot, 0u, buf, 9u) < 0) {
        return -1;
    }
    if (cfg->bLength < 9u || cfg->bDescriptorType != USB_DESC_CONFIGURATION) {
        return -1;
    }
    total = cfg->wTotalLength;
    if (total < 9u || total > (uint16_t) sizeof(buf)) {
        return -1;
    }

    memset(buf, 0, sizeof(buf));
    if (usb_get_config_descriptor(slot, 0u, buf, (uint32_t) total) < 0) {
        return -1;
    }

    /* Walk the descriptor chain. Every entry starts with bLength /
     * bDescriptorType and the chain is a flat concatenation, so advancing by
     * bLength visits interface, endpoint and class-specific descriptors alike. */
    off = cfg->bLength;
    while (off + 2u <= (uint32_t) total) {
        const uint8_t *d = buf + off;
        uint8_t dlen = d[0];
        uint8_t dtype = d[1];

        if (dlen < 2u || off + (uint32_t) dlen > (uint32_t) total) {
            break;   /* malformed tail: stop rather than read past the buffer */
        }
        if (dtype == USB_DESC_INTERFACE && dlen >= 9u && !seen_iface) {
            /* Remember the first interface's class. Devices that put their
             * class on the interface (bDeviceClass == 0) - which is nearly all
             * HID and mass-storage devices - are otherwise classified as
             * "per-interface with class 0" and never reach their class driver. */
            const usb_interface_desc_t *ifd =
                (const usb_interface_desc_t *) (const void *) d;

            *out_iface_class = ifd->bInterfaceClass;
            *out_iface_subclass = ifd->bInterfaceSubClass;
            *out_iface_protocol = ifd->bInterfaceProtocol;
            seen_iface = true;
        }
        if (dtype == USB_DESC_ENDPOINT && dlen >= 7u &&
            n_eps < (uint8_t) XHCI_MAX_CONFIGURED_EPS) {
            const usb_endpoint_desc_t *ep =
                (const usb_endpoint_desc_t *) (const void *) d;
            uint8_t num = (uint8_t) (ep->bEndpointAddress & 0x0Fu);
            bool in = (ep->bEndpointAddress & 0x80u) != 0u;

            if (num != 0u) {
                /* bmAttributes bits 1:0 are the transfer type and share the
                 * numbering the xHCI driver expects (0 ctrl, 1 isoc, 2 bulk,
                 * 3 interrupt), so they pass through unchanged. */
                eps[n_eps].ep_number = num;
                eps[n_eps].direction_in = in;
                eps[n_eps].xfer_type = (uint8_t) (ep->bmAttributes & 0x03u);
                /* wMaxPacketSize bits 10:0 are the packet size; bits 12:11
                 * carry the high-speed / super-speed burst value. */
                eps[n_eps].max_packet = (uint16_t) (ep->wMaxPacketSize & 0x07FFu);
                eps[n_eps].interval = ep->bInterval;
                eps[n_eps].max_burst =
                    (uint8_t) ((ep->wMaxPacketSize >> 11) & 0x03u);
                n_eps++;

                if (in && *out_ep_in == 0u) {
                    *out_ep_in = ep->bEndpointAddress;
                }
                if (!in && *out_ep_out == 0u) {
                    *out_ep_out = ep->bEndpointAddress;
                }
            }
        }
        off += dlen;
    }

    /* Most devices will not respond on a non-zero endpoint until their
     * configuration has been selected, so this goes before Configure Endpoint. */
    if (usb_set_configuration(slot, cfg->bConfigurationValue) < 0) {
        return -1;
    }

    if (n_eps == 0u) {
        return 0;   /* no endpoints to enable - a hub or a degenerate device */
    }
    if (xhci_configure_endpoint(slot, eps, n_eps) < 0) {
        return -1;
    }
    return (int) n_eps;
}

/* Human-readable USB class name (device class or interface class - the two
 * registries share their common values), for the one-line enumeration record in
 * the boot log. Values from the USB-IF class code registry. */
static const char *usb_class_name(uint8_t cls)
{
    switch (cls) {
    case 0x00u: return "per-interface";
    case 0x01u: return "audio";
    case 0x02u: return "communications";
    case 0x03u: return "hid";
    case 0x07u: return "printer";
    case 0x08u: return "mass-storage";
    case 0x09u: return "hub";
    case 0x0Au: return "cdc-data";
    case 0x0Bu: return "smart-card";
    case 0x0Eu: return "video";
    case 0x0Fu: return "healthcare";
    case 0xE0u: return "wireless";
    case 0xEFu: return "misc";
    case 0xFEu: return "app-specific";
    case 0xFFu: return "vendor";
    default:    return "unknown";
    }
}

/* Walk the xHCI root ports and enumerate every attached device. */
bool usb_probe(void)
{
    const xhci_info_t *xhci = xhci_info();
    int ports_found;

    memset(g_usb_devs, 0, sizeof(g_usb_devs));

    if (!xhci->present) {
        strcpy(g_usb_ext_info.status, "usb: not found");
        log_write("usb: not found");
        return false;
    }

    ports_found = xhci_roothub_poll();
    if (ports_found == 0) {
        strcpy(g_usb_ext_info.status, "usb: no devices attached");
        log_write("usb: no devices attached");
    }

    /* Enumerate exactly the slots the controller has addressed. Sweeping every
     * possible slot id instead would fire a GET_DESCRIPTOR at slots that were
     * never enabled, and each of those now reports a "slot not enabled"
     * completion - turning a healthy boot into a wall of failures. */
    {
        uint32_t dev_idx = 0;
        uint32_t present = xhci_device_count();

        for (uint32_t i = 0u; i < present && dev_idx < USB_DEV_MAX; i++) {
            const xhci_device_t *xdev = xhci_get_device(i);
            usb_device_desc_t desc;
            uint8_t slot;
            uint8_t ep_in = 0u;
            uint8_t ep_out = 0u;
            uint8_t if_cls = 0u;    /* class of interface 0 */
            uint8_t if_sub = 0u;    /* subclass of interface 0 */
            uint8_t if_proto = 0u;  /* protocol of interface 0 */
            int32_t ret;

            if (xdev == NULL) {
                continue;
            }
            slot = xdev->slot_id;

            memset(&desc, 0, sizeof(desc));
            ret = usb_get_device_descriptor(slot, &desc);
            if (ret < 0 || desc.bLength < 18u) {
                continue;
            }
            g_usb_devs[dev_idx].present = true;
            g_usb_devs[dev_idx].slot_id = slot;
            g_usb_devs[dev_idx].device_class = desc.bDeviceClass;
            g_usb_devs[dev_idx].vendor_id = desc.idVendor;
            g_usb_devs[dev_idx].product_id = desc.idProduct;
            g_usb_devs[dev_idx].max_packet0 = desc.bMaxPacketSize0;

            /* Address Device left this device with EP0 only. Read its
             * configuration descriptor, select the configuration and enable the
             * endpoints it advertises - before the class hook below, because
             * the HID path starts issuing interrupt transfers immediately. */
            if (usb_configure_device(slot, &ep_in, &ep_out, &if_cls, &if_sub,
                                     &if_proto) >= 0) {
                g_usb_devs[dev_idx].ep_in = ep_in;
                g_usb_devs[dev_idx].ep_out = ep_out;
            } else {
                /* Keep descriptor-plausible defaults so the record stays
                 * usable for diagnostics, but say so: this device will fail
                 * its first transfer. */
                g_usb_devs[dev_idx].ep_in = 0x81u;
                g_usb_devs[dev_idx].ep_out = 0x01u;
                log_write("usb: endpoint setup failed; device has EP0 only");
            }

            /* Interface 0's class / subclass / protocol, not bDeviceClass.
             * Nearly every HID and mass-storage device reports
             * bDeviceClass == 0 ("per-interface") and puts its real identity on
             * the interface descriptor. Recording bDeviceSubClass here - which
             * is what this line used to do - left every such device classified
             * as "per-interface with class 0", so no class driver ever claimed
             * it. Fall back to the device descriptor only when there was no
             * interface descriptor to read. */
            g_usb_devs[dev_idx].iface_class =
                (if_cls != 0u) ? if_cls : desc.bDeviceClass;
            g_usb_devs[dev_idx].iface_subclass = if_sub;
            g_usb_devs[dev_idx].iface_protocol = if_proto;

            /* Class-match: HID / MSC / Hub / Audio -> hotplug hook */
            usbdev_hotplug(slot, desc.bDeviceClass,
                           g_usb_devs[dev_idx].iface_class, if_proto,
                           desc.idVendor, desc.idProduct, true);

            /* One positive line per device that made it all the way through
             * Enable Slot -> Address Device -> Configure Endpoint. Without it a
             * fully successful enumeration is invisible in the boot log: command
             * completions are consumed silently and the class drivers only speak
             * up when something fails, so "no output" is indistinguishable from
             * "nothing enumerated". */
            log_write_event("usb: device enumerated, class ",
                            usb_class_name(g_usb_devs[dev_idx].iface_class));
            kernel_log_hex_u32("usb: vid:pid ",
                               ((uint32_t) desc.idVendor << 16) |
                               (uint32_t) desc.idProduct);
            kernel_log_hex_u32("usb: iface cls:sub:proto ",
                               ((uint32_t) g_usb_devs[dev_idx].iface_class << 16) |
                               ((uint32_t) if_sub << 8) | (uint32_t) if_proto);
            kernel_log_hex_u32("usb: ep in:out ",
                               ((uint32_t) g_usb_devs[dev_idx].ep_in << 8) |
                               (uint32_t) g_usb_devs[dev_idx].ep_out);
            dev_idx++;
        }
    }

    usb_ext_refresh();
    return true;
}

void usb_shutdown(void)
{
    for (int i = 0; i < USB_DEV_MAX; i++) {
        g_usb_devs[i].present = false;
    }
    strcpy(g_usb_ext_info.status, "usb: shutdown");
}

/* ── Existing status aggregation ──────────────────────────────── */
void usb_ext_refresh(void)
{
    const xhci_info_t *xhci = xhci_info();

    g_usb_ext_info.legacy_ready = usb_legacy_compat();
    g_usb_ext_info.native_host_present = usb_native_host_present();
    g_usb_ext_info.xhci_present = xhci->present;
    g_usb_ext_info.root_ports = xhci->max_ports;
    g_usb_ext_info.max_slots = xhci->max_slots;
    usb_msc_rescan();
    strcpy(g_usb_ext_info.status,
           g_usb_ext_info.xhci_present ? "usbext: xhci root hub described" : "usbext: legacy usb bridge");
}

void usb_ext_init(void)
{
    memset(&g_usb_ext_info, 0, sizeof(g_usb_ext_info));
    memset(g_msc, 0, sizeof(g_msc));
    memset(g_printers, 0, sizeof(g_printers));
    g_next_drive = USB_MSC_FIRST_DRIVE;
    g_usb_ext_info.initialized = true;
    usb_ext_refresh();

    /* Enumerate now. usb_probe() is the only entry point that walks the root
     * hub, issues Address Device, reads the configuration descriptor and runs
     * Configure Endpoint; without this call the entire USB device stack is dead
     * code and no HID / mass-storage device is ever registered, even though the
     * host controller is up and running. */
    (void) usb_probe();

    /* Then re-assert HID bring-up (boot protocol + idle-off) over everything
     * that just enumerated. usb_probe() registers HID devices as it walks the
     * bus, but this pass also covers devices that were already addressed before
     * hid_init() ran, and it is what reports "hid: HID devices enumerated".
     * hid_device_connected() matches on slot, so nothing is registered twice. */
    (void) hid_probe();
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
