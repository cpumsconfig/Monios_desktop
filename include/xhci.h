#ifndef _XHCI_H_
#define _XHCI_H_

#include "stdbool.h"
#include "stdint.h"

/* ── Standard USB Setup Packet (8 bytes) ─────────────────────────── */
typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) xhci_setup_packet_t;

/* ── Enumerated xHCI device record ──────────────────────────────── */
typedef struct {
    bool     present;
    uint8_t  slot_id;
    uint8_t  port;
    uint8_t  speed;            /* PORTSC Port Speed field */
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t  device_class;
    uint8_t  iface_class;
    uint8_t  max_packet_size0;
} xhci_device_t;

typedef struct {
    bool present;
    bool mmio_ready;
    bool hc_running;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t irq;
    /* 64-bit because xHCI exposes a 64-bit BAR and UEFI firmware in particular
     * tends to place it above the 4 GiB line. */
    uint64_t mmio_base;
    uint8_t cap_length;
    uint16_t hci_version;
    uint8_t max_slots;
    uint8_t max_ports;
    uint32_t hcsparams1;
    uint32_t hccparams1;
    uint32_t hcsparams2;
    uint32_t hcsparams3;
    uint32_t dboff;
    uint32_t rts_off;
    uint32_t pagesize;
    uint32_t num_active_slots;
    uint32_t device_count;
    char status[64];
} xhci_info_t;

/* ── Lifecycle (existing) ────────────────────────────────────────── */
bool xhci_driver_init(void);
void xhci_shutdown(void);
const xhci_info_t *xhci_info(void);
const char *xhci_status(void);

/* ── New unified driver interface ─────────────────────────────────── */
bool xhci_probe(void);                 /* alias of xhci_driver_init */
void xhci_init(void);                  /* alias of xhci_driver_init */
void xhci_info_dump(void);             /* log controller info */

/* Port / device management */
int  xhci_port_reset(uint8_t port);
int  xhci_roothub_poll(void);          /* walk PORTSC, enumerate attached devices */
int  xhci_enable_slot(uint8_t *out_slot);
/* Bring a slot to Addressed state. `port` is the 1-based root-hub port and
 * `speed` is the PORTSC speed code; both are recorded in the Slot Context. */
int  xhci_address_device(uint8_t slot, uint8_t port, uint8_t speed);

/* ── Configure Endpoint ─────────────────────────────────────────────
 * Address Device only establishes EP0. Every other endpoint stays disabled
 * until a Configure Endpoint command enables it, so a device that only ever
 * ran Address Device cannot move bulk or interrupt traffic at all. */
/* Transfer type, matching the USB endpoint descriptor bmAttributes Transfer
 * Type field so a parsed descriptor can be passed through unchanged. */
#define XHCI_XFER_CONTROL     0u
#define XHCI_XFER_ISOCH       1u
#define XHCI_XFER_BULK        2u
#define XHCI_XFER_INTERRUPT   3u

typedef struct {
    uint8_t  ep_number;     /* 1..15 (bEndpointAddress bits 3:0) */
    bool     direction_in;  /* true = IN (device -> host) */
    uint8_t  xfer_type;     /* XHCI_XFER_* */
    uint16_t max_packet;    /* decoded wMaxPacketSize */
    uint8_t  interval;      /* bInterval; 0 for bulk/control */
    uint8_t  max_burst;     /* decoded from wMaxPacketSize for HS/SS */
} xhci_ep_config_t;

/* Maximum number of endpoints accepted by one xhci_configure_endpoint() call. */
#define XHCI_MAX_CONFIGURED_EPS 14u

/* Enable the endpoints in `eps`. The slot must already be Addressed.
 * Endpoint numbering follows the USB descriptor: ep_number 0 is rejected
 * (EP0 is already live), 1..15 are accepted. Returns 0, or negative. */
int  xhci_configure_endpoint(uint8_t slot, const xhci_ep_config_t *eps,
                             uint8_t ep_count);

/* Transfers */
int32_t xhci_control_transfer(uint8_t slot, const xhci_setup_packet_t *setup,
                              void *data, uint32_t data_len);
/* Bulk/interrupt on a non-zero endpoint. `endpoint` may be given either as a
 * bare number (1..15) or as a full bEndpointAddress byte (0x01..0x8F); the
 * direction is the OR of `is_in` and bit7 of `endpoint`. The endpoint must have
 * been enabled by xhci_configure_endpoint() or the transfer will not complete. */
int32_t xhci_bulk_transfer(uint8_t slot, uint8_t endpoint,
                            void *data, uint32_t len, bool is_in);
int32_t xhci_interrupt_transfer(uint8_t slot, uint8_t endpoint,
                                void *data, uint32_t len);

/* Generic read/write front-ends: force the direction on top of the endpoint
 * value, so both a bare number and an address byte work. */
int32_t xhci_read(uint8_t slot, uint8_t endpoint, void *data, uint32_t len);
int32_t xhci_write(uint8_t slot, uint8_t endpoint, const void *data, uint32_t len);

/* ── Descriptor access ───────────────────────────────────────────── */
#define XHCI_DESC_DEVICE 1u
#define XHCI_DESC_CONFIG 2u
#define XHCI_DESC_STRING 3u

/* GET_DESCRIPTOR over EP0 (control-IN). Returns bytes transferred or <0. */
int32_t xhci_get_descriptor(uint8_t slot, uint8_t type, uint8_t index,
                            void *buf, uint16_t len);

/* ── Enumerated device table (filled by xhci_roothub_poll) ───────── */
uint32_t xhci_device_count(void);
const xhci_device_t *xhci_get_device(uint32_t index);

/* Raw MMIO register access helpers */
uint32_t xhci_mmio_read32(uint32_t offset);
void     xhci_mmio_write32(uint32_t offset, uint32_t value);

#endif
