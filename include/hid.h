#ifndef _HID_H_
#define _HID_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool legacy_keyboard;
    bool legacy_mouse;
    bool usb_legacy;
    bool xhci_present;
    uint32_t key_events_seen;
    int32_t mouse_x;
    int32_t mouse_y;
    uint8_t mouse_buttons;
    char status[64];
} hid_info_t;

void hid_init(void);
void hid_update(void);
const hid_info_t *hid_info(void);
const char *hid_status(void);

/* --- HID report-descriptor parsing --- */
bool hid_parse_report_descriptor(const uint8_t *desc, uint32_t len);

/* --- Boot-protocol report processing (injected into PS/2 queues) --- */
void hid_process_mouse_report(const uint8_t *report, uint32_t len);
void hid_process_keyboard_report(const uint8_t *report, uint32_t len);

/* --- Hotplug registration (called by usb_ext.c) --- */
void hid_device_connected(uint8_t slot_id, uint8_t iface_protocol,
                         uint16_t vendor_id, uint16_t product_id);
void hid_device_disconnected(uint8_t slot_id);

/* ── New unified HID driver interface ────────────────────────────── */
/* Detect HID devices (interface class 0x03). Prints "hid: not found"
 * and returns false if none. */
bool hid_probe(void);

/* HID class-specific setup (Set Protocol / Set Idle / Get Report Desc). */
int32_t hid_set_boot_protocol(uint8_t slot);
int32_t hid_set_idle(uint8_t slot, uint8_t duration);
int32_t hid_get_report_descriptor(uint8_t slot, void *buf, uint32_t len);
int32_t hid_set_output_report(uint8_t slot, uint8_t report_id,
                              const void *data, uint32_t len);
int32_t hid_set_keyboard_led(uint8_t slot, uint8_t leds);

/* Report descriptor field summary (filled by hid_parse_report_info). */
typedef struct {
    uint16_t usage_page;
    uint8_t  report_size_bits;
    uint8_t  report_count;
    uint16_t input_report_bytes;
    uint16_t output_report_bytes;
    uint16_t feature_report_bytes;
    bool     has_data;
} hid_report_info_t;

bool hid_parse_report_info(const uint8_t *desc, uint32_t len,
                           hid_report_info_t *out);

/* Generic read/write: submit an interrupt IN / OUT transfer. */
int32_t hid_read(uint8_t slot, void *report, uint32_t len);
int32_t hid_write(uint8_t slot, const void *report, uint32_t len);

/* Keyboard LED bits (Set Output report byte 0) */
#define HID_LED_NUM_LOCK    (1u << 0)
#define HID_LED_CAPS_LOCK   (1u << 1)
#define HID_LED_SCROLL_LOCK (1u << 2)

#endif
