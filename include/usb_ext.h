#ifndef _USB_EXT_H_
#define _USB_EXT_H_

#include "stdbool.h"
#include "stdint.h"

/* ── USB interface / device class codes (USB-IF) ────────────────── */
#define USB_DEV_CLASS_INTERFACE   0x00
#define USB_IF_CLASS_AUDIO         0x01
#define USB_IF_CLASS_CDC           0x02
#define USB_IF_CLASS_HID           0x03
#define USB_IF_CLASS_PRINTER       0x07
#define USB_IF_CLASS_MASS_STORAGE  0x08
#define USB_IF_CLASS_HUB           0x09
#define USB_IF_CLASS_VENDOR        0xFF

/* USB mass storage subclass / protocol */
#define USB_MSC_SUBCLASS_SCSI      0x06   /* SCSI transparent command set */
#define USB_MSC_PROTOCOL_BULK_ONLY 0x50   /* Bulk-Only Transport (BBB) */

/* Standard USB bmRequestType / bRequest for control transfers */
#define USB_REQ_DIR_IN             0x80
#define USB_REQ_DIR_OUT            0x00
#define USB_REQ_TYPE_CLASS         0x20
#define USB_REQ_RECIP_INTERFACE    0x01
#define USB_REQ_GET_STATUS         0x00
#define USB_REQ_CLEAR_FEATURE      0x01
#define USB_REQ_SET_CONFIGURATION  0x09
/* MSC class requests */
#define USB_MSC_REQ_BULK_ONLY_RESET  0xFF
#define USB_MSC_REQ_GET_MAX_LUN       0xFE
/* Printer class requests */
#define USB_PRINTER_REQ_GET_DEVICE_ID  0x00
#define USB_PRINTER_REQ_GET_PORT_STATUS 0x01
#define USB_PRINTER_REQ_SOFT_RESET      0x02

typedef struct {
    bool initialized;
    bool legacy_ready;
    bool native_host_present;
    bool xhci_present;
    uint32_t root_ports;
    uint32_t max_slots;
    char status[64];
    /* Aggregate device-class tallies (feature 20) */
    uint32_t msc_devices;       /* mass storage devices currently present */
    uint32_t msc_mounted;       /* MSC devices mounted with a drive letter */
    uint32_t printer_devices;   /* USB printers currently present */
} usb_ext_info_t;

/* ── Enumerated USB device record (feature 20) ─────────────────────
 * A real xHCI roothub/enumeration driver would fill these records on
 * port-change events via usbdev_hotplug().  In the current skeleton
 * the table is populated from xHCI root-port status polling.
 */
typedef enum {
    USBDEV_NONE = 0,
    USBDEV_MSC,         /* bulk-only mass storage (U 盘) */
    USBDEV_PRINTER,     /* USB printer */
    USBDEV_HID,
    USBDEV_HUB,
    USBDEV_AUDIO,
    USBDEV_OTHER
} usbdev_kind_t;

typedef struct {
    bool        present;
    uint8_t     slot_id;        /* xHCI slot address */
    uint8_t     kind;           /* usbdev_kind_t */
    uint16_t    vendor_id;
    uint16_t    product_id;
    uint32_t    sector_size;     /* MSC only, bytes (512 typical) */
    uint64_t    sector_count;    /* MSC only */
    char        drive_letter;    /* 'D','E',... for mounted MSC; 0 otherwise */
    bool        mounted;
    char        name[24];
} usb_msc_device_t;

#define USB_MSC_MAX_DEVICES   8
#define USB_MSC_FIRST_DRIVE   'D'   /* A: floppy B: reserved, C: system */

typedef struct {
    bool        present;
    uint8_t     slot_id;
    uint16_t    vendor_id;
    uint16_t    product_id;
    uint8_t     port_status;    /* 0=selected/idle, bit0=present, bit1=paper, bit2=error */
    uint32_t    jobs_completed;
    char        name[24];
} usb_printer_t;

#define USB_PRINTER_MAX       4

/* ── Lifecycle / status API (existing) ─────────────────────────── */
void usb_ext_init(void);
void usb_ext_refresh(void);
const usb_ext_info_t *usb_ext_info(void);
const char *usb_ext_status(void);

/* ── Feature 20: device-class detection & hotplug ──────────────── */
/* Called by the xHCI roothub layer when a device is enumerated/de-enumerated.
 * device_class is the bDeviceClass from the device descriptor; when it is
 * USB_DEV_CLASS_INTERFACE (0x00) the class lives on interface 0 instead, and
 * iface_class / iface_protocol carry that descriptor's bInterfaceClass and
 * bInterfaceProtocol. kind is derived automatically. */
void usbdev_hotplug(uint8_t slot_id, uint8_t device_class, uint8_t iface_class,
                    uint8_t iface_protocol, uint16_t vendor_id,
                    uint16_t product_id, bool attached);

/* Walk the xHCI roothub ports and reconcile the device table. */
void usb_msc_rescan(void);

/* ── Feature 20: mass-storage -> drive-letter integration ──────── */
int  usb_msc_count(void);
const usb_msc_device_t *usb_msc_get(int index);
/* Mount every present-but-unmounted MSC device, assigning the next free
 * drive letter (D:, E:, ...).  Returns the number newly mounted. */
int  usb_msc_mount_all(void);
/* Eject by drive letter ('D'..).  Returns true on success. */
bool usb_msc_eject(char drive_letter);

/* Block-device layer integration seam (blockdev.c owned by perf group).
 * Perf group should, from blockdev_init(), for each mounted MSC device,
 * construct a blockdev_t whose read/write sector funcs front a USB BBB
 * bulk-only transfer to usb_msc_get(i).  The accessors below are the
 * stable contract; we intentionally do not call into blockdev.c here. */
uint32_t usb_msc_sector_size(const usb_msc_device_t *dev);
uint64_t usb_msc_sector_count(const usb_msc_device_t *dev);

/* ── Feature 20: USB printer (basic) ────────────────────────────── */
int  usb_printer_count(void);
const usb_printer_t *usb_printer_get(int index);
/* Send the class control transfer that fetches port status; updates record. */
int  usb_printer_poll_status(int index);
/* Submit a raw byte burst to the printer's bulk-out endpoint (basic).
 * Returns bytes accepted or negative errno. */
int  usb_printer_write(int index, const void *data, uint32_t len);

/* ── SYS_USB_MOUNT_CTL (65) handler ───────────────────────────────
 * rbx = subcommand, rcx = arg1, rdx = arg2
 *   0 USB_MCTL_ENUM_MSC      -> return number of MSC devices present
 *   1 USB_MCTL_MOUNT_ALL     -> mount all, return count mounted
 *   2 USB_MCTL_EJECT         -> arg1=letter(char), return 0/err
 *   3 USB_MCTL_GET_DRIVE     -> arg1=MSC index, return drive letter or 0
 *   4 USB_MCTL_ENUM_PRINTER  -> return number of printers
 *   5 USB_MCTL_PRINTER_STATUS-> arg1=printer index, return port_status byte
 *   6 USB_MCTL_PRINTER_WRITE -> arg1=idx, arg2=ptr{data,len}, return bytes
 */
#define USB_MCTL_ENUM_MSC        0
#define USB_MCTL_MOUNT_ALL       1
#define USB_MCTL_EJECT           2
#define USB_MCTL_GET_DRIVE       3
#define USB_MCTL_ENUM_PRINTER    4
#define USB_MCTL_PRINTER_STATUS  5
#define USB_MCTL_PRINTER_WRITE   6

/* ── Standard USB bRequest codes ─────────────────────────────────── */
#define USB_REQ_GET_STATUS         0x00
#define USB_REQ_CLEAR_FEATURE      0x01
#define USB_REQ_SET_FEATURE        0x03
#define USB_REQ_SET_ADDRESS        0x05
#define USB_REQ_GET_DESCRIPTOR     0x06
#define USB_REQ_SET_DESCRIPTOR     0x07
#define USB_REQ_GET_CONFIGURATION  0x08
#define USB_REQ_SET_CONFIGURATION  0x09
#define USB_REQ_GET_INTERFACE      0x0A
#define USB_REQ_SET_INTERFACE      0x0B

/* Standard descriptor types */
#define USB_DESC_DEVICE            0x01
#define USB_DESC_CONFIGURATION     0x02
#define USB_DESC_STRING            0x03
#define USB_DESC_INTERFACE         0x04
#define USB_DESC_ENDPOINT          0x05
#define USB_DESC_HID               0x21
#define USB_DESC_HID_REPORT        0x22

/* USB device descriptor (18 bytes) */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed)) usb_device_desc_t;

/* Configuration descriptor (9 bytes header) */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed)) usb_config_desc_t;

/* Interface descriptor (9 bytes) */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} __attribute__((packed)) usb_interface_desc_t;

/* Endpoint descriptor (7 bytes) */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;  /* bit7=in, bits0-3=ep */
    uint8_t  bmAttributes;       /* bits0-1: 0=ctrl,1=isoc,2=bulk,3=int */
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed)) usb_endpoint_desc_t;

/* ── Enumerated USB device record (new) ─────────────────────────── */
/* HID bInterfaceProtocol values (USB HID 1.11, 7.2.6). 0 means "no boot
 * subclass" - the device only speaks the report protocol, so keyboard vs mouse
 * cannot be told from the descriptor alone. */
#define USB_HID_PROTO_NONE     0x00
#define USB_HID_PROTO_KEYBOARD 0x01
#define USB_HID_PROTO_MOUSE    0x02

#define USB_DEV_MAX   16
typedef struct {
    bool     present;
    uint8_t  slot_id;
    uint8_t  device_class;
    /* class / subclass / protocol of interface 0. Nearly every HID and
     * mass-storage device reports bDeviceClass == 0 ("per-interface"), so these
     * - not bDeviceClass - are what identify the device to a class driver. */
    uint8_t  iface_class;
    uint8_t  iface_subclass;
    uint8_t  iface_protocol;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t  configuration;
    uint8_t  interface;
    uint8_t  ep_in;
    uint8_t  ep_out;
    uint8_t  max_packet0;
} usb_device_t;

uint64_t usb_mount_ctl(uint64_t sub, uint64_t arg1, uint64_t arg2);

/* ── New unified device-stack interface ──────────────────────────── */
/* Enumerate every xHCI root port, populate the device table. */
bool usb_probe(void);
/* Generic control request: SETUP transfer on EP0. */
int32_t usb_control_request(uint8_t slot, uint8_t bmRequestType,
                            uint8_t bRequest, uint16_t wValue,
                            uint16_t wIndex, void *data, uint16_t wLength);
/* Standard descriptor helpers. */
int32_t usb_get_device_descriptor(uint8_t slot, usb_device_desc_t *desc);
int32_t usb_get_config_descriptor(uint8_t slot, uint8_t index,
                                  void *buf, uint32_t len);
int32_t usb_set_configuration(uint8_t slot, uint8_t config);
/* Data transfers. */
int32_t usb_bulk_transfer(uint8_t slot, uint8_t endpoint,
                          void *data, uint32_t len, bool is_in);
int32_t usb_interrupt_transfer(uint8_t slot, uint8_t endpoint,
                               void *data, uint32_t len);
/* Generic read/write front-ends. */
int32_t usb_read(uint8_t slot, uint8_t endpoint, void *data, uint32_t len);
int32_t usb_write(uint8_t slot, uint8_t endpoint, const void *data, uint32_t len);
/* Device table access. */
uint32_t usb_device_count(void);
const usb_device_t *usb_device_get(uint32_t index);
/* Shutdown / info / status (already present below). */
void usb_shutdown(void);

#endif
