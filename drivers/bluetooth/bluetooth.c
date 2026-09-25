#include "bluetooth.h"
#include "common.h"
#include "usb.h"
#include "xhci.h"
#include "kernel.h"

/*
 * MoniOS Bluetooth (HCI / L2CAP / RFCOMM) driver.
 *
 * 传输层：USB HCI。控制器通过 xhci 驱动枚举出来后，我们读它的设备/配置
 * 描述符，匹配 class 0xE0 / subclass 0x01 / protocol 0x01（Wireless /
 * RF controller / Bluetooth），并记录它的批量端点。HCI 命令与 ACL 数据从
 * bulk-out 发出，事件与 ACL 数据从 bulk-in 取回。
 *
 * xHCI 核心侧的前置条件已补齐：`xhci_address_device()` 会构建规范的
 * Input Context，非 0 号端点经 `xhci_configure_endpoint()` 获得各自独立的
 * 传输环。`bt_hci_transport_ready()` 只有在真正拿到端点后才会为真。
 *
 * 已知限制：xHCI 的软件侧已完整，但在本项目的 QEMU 环境里控制器运算寄存器
 * 读回全 0、事件环无法工作，因此本链路尚未取得实地通过的证据。详见
 * docs/known_limitations.md 的 P0 第 1 条。
 */

static bluetooth_info_t g_bluetooth_info;

/* Per-channel receive reassembly buffers (not part of the public structs). */
#define BT_L2CAP_RX_BUF 256
#define BT_RFCOMM_RX_BUF 256
static uint8_t  l2cap_rx[BT_L2CAP_MAX_CHANNELS][BT_L2CAP_RX_BUF];
static uint16_t l2cap_rx_len[BT_L2CAP_MAX_CHANNELS];
static uint8_t  rfcomm_rx[BT_RFCOMM_MAX_CHANNELS][BT_RFCOMM_RX_BUF];
static uint16_t rfcomm_rx_len[BT_RFCOMM_MAX_CHANNELS];

/* Pending PIN for the pairing flow. */
static char     pending_pin[BT_MAX_PIN_LEN];
static uint8_t  pending_pin_addr[BT_ADDR_LEN];

/* HCI Inquiry Result with RSSI (not enumerated in the header). */
#ifndef HCI_EVT_INQUIRY_RESULT_WITH_RSSI
#define HCI_EVT_INQUIRY_RESULT_WITH_RSSI 0x2F
#endif

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

static bool addr_eq(const uint8_t *a, const uint8_t *b)
{
    for (int i = 0; i < BT_ADDR_LEN; i++)
        if (a[i] != b[i]) return false;
    return true;
}

static bt_device_t *find_dev_raw(const uint8_t *addr)
{
    for (uint32_t i = 0; i < g_bluetooth_info.device_count; i++) {
        if (addr_eq(g_bluetooth_info.devices[i].addr, addr))
            return &g_bluetooth_info.devices[i];
    }
    return NULL;
}

static bt_l2cap_channel_t *find_l2cap_cid(uint16_t cid)
{
    for (int i = 0; i < BT_L2CAP_MAX_CHANNELS; i++)
        if (g_bluetooth_info.l2cap_channels[i].open &&
            g_bluetooth_info.l2cap_channels[i].cid == cid)
            return &g_bluetooth_info.l2cap_channels[i];
    return NULL;
}

static bt_l2cap_channel_t *alloc_l2cap_channel(void)
{
    for (int i = 0; i < BT_L2CAP_MAX_CHANNELS; i++)
        if (!g_bluetooth_info.l2cap_channels[i].open)
            return &g_bluetooth_info.l2cap_channels[i];
    return NULL;
}

static bt_rfcomm_channel_t *find_rfcomm_dlci(uint8_t dlci)
{
    for (int i = 0; i < BT_RFCOMM_MAX_CHANNELS; i++)
        if (g_bluetooth_info.rfcomm_channels[i].open &&
            g_bluetooth_info.rfcomm_channels[i].dlci == dlci)
            return &g_bluetooth_info.rfcomm_channels[i];
    return NULL;
}

static bt_rfcomm_channel_t *alloc_rfcomm_channel(void)
{
    for (int i = 0; i < BT_RFCOMM_MAX_CHANNELS; i++)
        if (!g_bluetooth_info.rfcomm_channels[i].open)
            return &g_bluetooth_info.rfcomm_channels[i];
    return NULL;
}

/* ACL 链路建立前缓存的 L2CAP 信令包，等 CONN_COMPLETE 再发出去。 */
static void bt_l2cap_flush_pending(const uint8_t *addr, uint16_t handle);

/* RFCOMM FCS: CRC-8, polynomial 0x07, init 0xFF, complemented. */
static uint8_t rfcomm_fcs(const uint8_t *data, uint32_t len)
{
    uint8_t fcs = 0xFF;
    for (uint32_t i = 0; i < len; i++) {
        fcs ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (fcs & 0x80)
                fcs = (uint8_t)((fcs << 1) ^ 0x07);
            else
                fcs = (uint8_t)(fcs << 1);
        }
    }
    return (uint8_t)(~fcs);
}

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* ------------------------------------------------------------------ */
/* USB HCI transport                                                  */
/* ------------------------------------------------------------------ */

/* USB 描述符类型与无线类编码（USB 2.0 §9.6、Bluetooth spec Vol 4 Part B） */
#define USB_DT_DEVICE       0x01u
#define USB_DT_INTERFACE    0x04u
#define USB_DT_ENDPOINT     0x05u
#define USB_CLASS_WIRELESS  0xE0u
#define USB_SUBCLASS_RF     0x01u
#define USB_PROTOCOL_BT     0x01u

#define USB_EP_TYPE_BULK    0x02u
#define USB_EP_TYPE_INTR    0x03u
#define USB_EP_TYPE_MASK    0x03u

/* 控制器发现结果。端点字段保存的是 USB 端点地址（含方向位）。 */
typedef struct {
    bool     ready;
    uint8_t  slot;
    uint8_t  iface_number;
    uint8_t  bulk_in_ep;
    uint8_t  bulk_out_ep;
    uint8_t  intr_in_ep;    /* 0 = 控制器没有中断端点 */
} bt_usb_transport_t;

static bt_usb_transport_t g_bt_usb;

/* 18 字节设备描述符中我们关心的字段（USB 2.0 §9.6.1） */
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
} __attribute__((packed)) bt_usb_device_desc_t;

/* 配置描述符解析暂存区（含接口与端点描述符链） */
static uint8_t g_bt_cfg[256];

/* 在配置描述符链里定位蓝牙接口，并取出它的批量/中断端点。 */
static bool bt_usb_parse_config(uint16_t total_len, uint8_t *iface_number,
                               uint8_t *bulk_in, uint8_t *bulk_out,
                               uint8_t *intr_in)
{
    uint32_t off = 0u;
    bool in_bt_iface = false;

    *iface_number = 0u;
    *bulk_in = 0u;
    *bulk_out = 0u;
    *intr_in = 0u;

    while (off + 2u <= (uint32_t) total_len) {
        uint8_t blen  = g_bt_cfg[off];
        uint8_t btype = g_bt_cfg[off + 1u];

        if (blen < 2u || off + (uint32_t) blen > (uint32_t) total_len) {
            break;
        }
        if (btype == USB_DT_INTERFACE && blen >= 9u) {
            in_bt_iface = (g_bt_cfg[off + 5u] == USB_CLASS_WIRELESS &&
                           g_bt_cfg[off + 6u] == USB_SUBCLASS_RF &&
                           g_bt_cfg[off + 7u] == USB_PROTOCOL_BT);
            if (in_bt_iface) {
                *iface_number = g_bt_cfg[off + 2u];
            }
        } else if (btype == USB_DT_ENDPOINT && blen >= 7u && in_bt_iface) {
            uint8_t addr = g_bt_cfg[off + 2u];
            uint8_t type = (uint8_t) (g_bt_cfg[off + 3u] & USB_EP_TYPE_MASK);

            if (type == USB_EP_TYPE_BULK) {
                if ((addr & 0x80u) != 0u) {
                    if (*bulk_in == 0u)  *bulk_in = addr;
                } else {
                    if (*bulk_out == 0u) *bulk_out = addr;
                }
            } else if (type == USB_EP_TYPE_INTR && (addr & 0x80u) != 0u) {
                if (*intr_in == 0u) *intr_in = addr;
            }
        }
        off += blen;
    }

    /* 一个可用的 USB HCI 控制器至少有 bulk-in 与 bulk-out 各一个。 */
    return (*bulk_in != 0u && *bulk_out != 0u);
}

/* 遍历 xHCI 已枚举设备，找出蓝牙控制器并记录其端点。 */
static bool bt_usb_find_controller(void)
{
    uint32_t n;

    memset(&g_bt_usb, 0, sizeof(g_bt_usb));
    n = xhci_device_count();

    for (uint32_t i = 0u; i < n; i++) {
        const xhci_device_t *dev = xhci_get_device(i);
        bt_usb_device_desc_t dd;
        uint8_t iface_number, bulk_in, bulk_out, intr_in;
        int32_t got;
        uint32_t total;

        if (dev == NULL || dev->slot_id == 0u) {
            continue;
        }

        memset(&dd, 0, sizeof(dd));
        got = xhci_get_descriptor(dev->slot_id, XHCI_DESC_DEVICE, 0u,
                                  &dd, (uint16_t) sizeof(dd));
        if (got < (int32_t) sizeof(dd)) {
            continue;
        }
        /* 蓝牙控制器在设备级声明 Wireless / RF / Bluetooth。 */
        if (dd.bDeviceClass != USB_CLASS_WIRELESS ||
            dd.bDeviceSubClass != USB_SUBCLASS_RF ||
            dd.bDeviceProtocol != USB_PROTOCOL_BT) {
            continue;
        }

        /* 先取 9 字节配置描述符拿到 wTotalLength，再取完整描述符链。 */
        got = xhci_get_descriptor(dev->slot_id, XHCI_DESC_CONFIG, 0u,
                                  g_bt_cfg, 9u);
        if (got < 9) {
            continue;
        }
        total = (uint32_t) g_bt_cfg[2] | ((uint32_t) g_bt_cfg[3] << 8);
        if (total < 9u || total > (uint32_t) sizeof(g_bt_cfg)) {
            total = (uint32_t) sizeof(g_bt_cfg);
        }
        got = xhci_get_descriptor(dev->slot_id, XHCI_DESC_CONFIG, 0u,
                                  g_bt_cfg, (uint16_t) total);
        if (got < 9) {
            continue;
        }
        if (!bt_usb_parse_config((uint16_t) got, &iface_number,
                                 &bulk_in, &bulk_out, &intr_in)) {
            continue;   /* 声明是蓝牙，但没有可用的 HCI 端点 */
        }

        g_bt_usb.slot = dev->slot_id;
        g_bt_usb.iface_number = iface_number;
        g_bt_usb.bulk_in_ep = bulk_in;
        g_bt_usb.bulk_out_ep = bulk_out;
        g_bt_usb.intr_in_ep = intr_in;
        g_bt_usb.ready = true;
        return true;
    }
    return false;
}

/* 通过 bulk-out 发一个 HCI 包。 */
static int bt_usb_send(const uint8_t *pkt, uint32_t len)
{
    int32_t n;

    if (!g_bt_usb.ready) {
        return BT_ERR_NOT_READY;
    }
    n = xhci_bulk_transfer(g_bt_usb.slot,
                           (uint8_t) (g_bt_usb.bulk_out_ep & 0x0Fu),
                           (void *) (uintptr_t) pkt, len, false);
    if (n < 0) {
        return BT_ERR_IO;
    }
    return BT_OK;
}

/* 从 bulk-in（或中断端点）取一个 HCI 包。
 * 返回字节数；<0 表示错误，0 表示当前没有数据。 */
static int bt_usb_recv(uint8_t *buf, uint32_t cap, bool prefer_intr)
{
    int32_t n;

    if (!g_bt_usb.ready) {
        return BT_ERR_NOT_READY;
    }
    if (prefer_intr && g_bt_usb.intr_in_ep != 0u) {
        n = xhci_interrupt_transfer(g_bt_usb.slot,
                                    (uint8_t) (g_bt_usb.intr_in_ep & 0x0Fu),
                                    buf, cap);
    } else {
        n = xhci_bulk_transfer(g_bt_usb.slot,
                               (uint8_t) (g_bt_usb.bulk_in_ep & 0x0Fu),
                               buf, cap, true);
    }
    if (n < 0) {
        return BT_ERR_IO;
    }
    return (int) n;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

void bluetooth_init(void)
{
    memset(&g_bluetooth_info, 0, sizeof(g_bluetooth_info));
    memset(l2cap_rx_len, 0, sizeof(l2cap_rx_len));
    memset(rfcomm_rx_len, 0, sizeof(rfcomm_rx_len));

    g_bluetooth_info.initialized = true;
    g_bluetooth_info.usb_transport_ready =
        usb_native_host_present() || xhci_info()->present;
    g_bluetooth_info.controller_present = false;
    g_bluetooth_info.hci_ready = false;
    g_bluetooth_info.controllers = 0;

    /*
     * 枚举 xHCI 设备，匹配 class 0xE0 / subclass 0x01 / protocol 0x01
     * （Wireless / RF controller / Bluetooth），并读取其配置描述符拿到
     * HCI 端点。只有真的拿到批量端点时传输层才算就绪。
     */
    bool found = false;
    if (g_bluetooth_info.usb_transport_ready) {
        found = bt_usb_find_controller();
        g_bluetooth_info.usb_transport_ready = g_bt_usb.ready;
    }

    if (found) {
        g_bluetooth_info.controller_present = true;
        g_bluetooth_info.controllers = 1;

        if (!g_bt_usb.ready) {
            strcpy(g_bluetooth_info.status,
                   "bluetooth: controller found, no HCI endpoints");
            log_write("bluetooth: controller lacks usable bulk endpoints");
            return;
        }

        /* Bring the controller up: Reset -> Event Mask -> Read BD_ADDR. */
        const uint8_t evt_mask[8] = { 0xFF, 0xFB, 0xFF, 0x07,
                                      0xF8, 0x3F, 0x00, 0x00 };
        bt_hci_send_command(HCI_OP_RESET, NULL, 0);
        bt_hci_send_command(HCI_OP_SET_EVENT_MASK, evt_mask, 8);
        bt_hci_send_command(HCI_OP_READ_BD_ADDR, NULL, 0);
        g_bluetooth_info.hci_ready = true;
        strcpy(g_bluetooth_info.status, "bluetooth: usb controller ready");
    } else {
        g_bluetooth_info.controller_present = false;
        g_bluetooth_info.hci_ready = false;
        g_bt_usb.ready = false;
        strcpy(g_bluetooth_info.status, "bluetooth: not found");
    }
}

const bluetooth_info_t *bluetooth_info(void)
{
    return &g_bluetooth_info;
}

const char *bluetooth_status(void)
{
    return g_bluetooth_info.status;
}

/* ------------------------------------------------------------------ */
/* HCI layer                                                          */
/* ------------------------------------------------------------------ */

int bt_hci_send_command(uint16_t opcode, const uint8_t *params, uint8_t param_len)
{
    if (!g_bluetooth_info.controller_present)
        return BT_ERR_NO_DEVICE;
    if (param_len > (BT_HCI_CMD_BUF_SIZE - 4))
        return BT_ERR_INVALID;

    /* HCI command packet: [0x01] [opcode LE16] [param_len] [params...] */
    uint8_t pkt[BT_HCI_CMD_BUF_SIZE];
    pkt[0] = HCI_COMMAND_PKT;
    put_le16(pkt + 1, opcode);
    pkt[3] = param_len;
    if (params && param_len)
        memcpy(pkt + 4, params, param_len);

    return bt_usb_send(pkt, (uint32_t) param_len + 4u);
}

bool bt_hci_transport_ready(void)
{
    return g_bt_usb.ready;
}

/* 发送一个 ACL 数据包（HCI ACL 头 + L2CAP 头 + 载荷）。
 * `pb` 是 ACL 包的 Packet_Boundary 标志：0x2 = 首片(自动刷新)，
 * 0x1 = 后续分片。 */
static int bt_acl_send(uint16_t handle, uint8_t pb, uint16_t cid,
                       const uint8_t *data, uint16_t len)
{
    uint8_t pkt[BT_HCI_ACL_BUF_SIZE];

    if (!g_bt_usb.ready) {
        return BT_ERR_NOT_READY;
    }
    if (handle == 0u || len == 0u) {
        return BT_ERR_INVALID;
    }
    if ((uint32_t) len + 8u > sizeof(pkt)) {
        return BT_ERR_INVALID;
    }

    /* ACL 头：Handle(12b) | PB(2b) | BC(2b)，然后是数据总长。 */
    put_le16(pkt + 0, (uint16_t) ((handle & 0x0FFFu) | ((uint16_t) (pb & 0x3u) << 12)));
    put_le16(pkt + 2, (uint16_t) (len + 4u));
    /* L2CAP 头：长度 + 通道 CID。 */
    put_le16(pkt + 4, len);
    put_le16(pkt + 6, cid);
    memcpy(pkt + 8, data, len);

    return bt_usb_send(pkt, (uint32_t) len + 8u);
}

bool bt_hci_is_ready(void)
{
    return g_bluetooth_info.hci_ready;
}

/* Add or refresh a discovered device; returns the slot. */
static bt_device_t *add_discovered_device(const uint8_t *addr,
                                          uint32_t class_of_device,
                                          int8_t rssi)
{
    bt_device_t *d = find_dev_raw(addr);
    if (!d) {
        if (g_bluetooth_info.device_count >= BT_MAX_DEVICES)
            return NULL;
        d = &g_bluetooth_info.devices[g_bluetooth_info.device_count++];
        memset(d, 0, sizeof(*d));
        memcpy(d->addr, addr, BT_ADDR_LEN);
    }
    d->class_of_device = class_of_device;
    d->rssi = rssi;
    return d;
}

int bt_hci_process_event(const uint8_t *data, uint16_t len)
{
    if (!g_bluetooth_info.controller_present)
        return BT_ERR_NO_DEVICE;
    if (!data || len < 2)
        return BT_ERR_INVALID;

    uint8_t evt = data[0];
    uint8_t plen = data[1];
    const uint8_t *p = data + 2;
    if ((uint16_t)plen > (uint16_t)(len - 2))
        return BT_ERR_INVALID;

    switch (evt) {
    case HCI_EVT_INQUIRY_COMPLETE:
        g_bluetooth_info.scanning = false;
        break;

    case HCI_EVT_INQUIRY_RESULT: {
        /* p[0] = num responses; each entry = 14 bytes */
        if (plen < 1) break;
        uint8_t n = p[0];
        for (uint8_t i = 0; i < n; i++) {
            const uint8_t *e = p + 1 + (uint32_t)i * 14;
            if ((e + 12) > p + plen) break;
            uint32_t cod = (uint32_t)e[9] |
                           ((uint32_t)e[10] << 8) |
                           ((uint32_t)e[11] << 16);
            add_discovered_device(e, cod, 127);
        }
        break;
    }

    case HCI_EVT_INQUIRY_RESULT_WITH_RSSI: {
        if (plen < 1) break;
        uint8_t n = p[0];
        for (uint8_t i = 0; i < n; i++) {
            const uint8_t *e = p + 1 + (uint32_t)i * 14;
            if ((e + 13) > p + plen) break;
            uint32_t cod = (uint32_t)e[8] |
                           ((uint32_t)e[9] << 8) |
                           ((uint32_t)e[10] << 16);
            add_discovered_device(e, cod, (int8_t)e[13]);
        }
        break;
    }

    case HCI_EVT_CONN_COMPLETE: {
        /* p[0]=status, p[1..2]=handle, p[3..8]=bdaddr, p[9..10]=linktype */
        if (plen < 11) break;
        uint16_t handle = get_le16(p + 1);
        bt_device_t *d = find_dev_raw(p + 3);
        if (d) {
            d->handle = handle;
            d->connected = (p[0] == 0);
        }
        if (p[0] == 0) {
            /* ACL 链路通了，把之前缓存的 L2CAP 信令包发出去。 */
            bt_l2cap_flush_pending(p + 3, handle);
        }
        break;
    }

    case HCI_EVT_DISCONN_COMPLETE: {
        /* p[0]=status, p[1..2]=handle, p[3]=reason */
        if (plen < 4) break;
        uint16_t handle = get_le16(p + 1);
        for (uint32_t i = 0; i < g_bluetooth_info.device_count; i++) {
            bt_device_t *d = &g_bluetooth_info.devices[i];
            if (d->handle == handle) {
                d->connected = false;
                d->handle = 0;
            }
        }
        break;
    }

    case HCI_EVT_CMD_COMPLETE: {
        /* p[0]=ncmd, p[1..2]=opcode, then return params */
        if (plen < 3) break;
        uint16_t opcode = get_le16(p + 1);
        if (opcode == HCI_OP_READ_BD_ADDR && plen >= 9) {
            memcpy(g_bluetooth_info.local_addr, p + 3, BT_ADDR_LEN);
        }
        break;
    }

    case HCI_EVT_CMD_STATUS:
        /* p[0]=status, p[1]=ncmd, p[2..3]=opcode */
        break;

    case HCI_EVT_PIN_CODE_REQ: {
        /* p[0..5] = bdaddr; reply with stored PIN */
        if (plen < 6) break;
        uint8_t params[22];
        memset(params, 0, sizeof(params));
        memcpy(params, p, BT_ADDR_LEN);
        params[6] = (uint8_t)(pending_pin[0] ?
                              (uint8_t)strlen(pending_pin) : 4);
        uint32_t plen_pin = strlen(pending_pin);
        if (plen_pin > 16) plen_pin = 16;
        memcpy(params + 7, pending_pin, plen_pin);
        bt_hci_send_command(HCI_OP_PIN_CODE_REPLY, params, sizeof(params));
        (void)pending_pin_addr;
        break;
    }

    case HCI_EVT_LINK_KEY_REQ:
    case HCI_EVT_AUTH_COMPLETE:
    case HCI_EVT_ENCRYPT_CHANGE:
    case HCI_EVT_CONN_REQUEST:
    case HCI_EVT_REMOTE_NAME_REQ_COMPLETE:
    case HCI_EVT_NUM_COMPLETED_PKTS:
    case HCI_EVT_LINK_KEY_NOTIFY:
    default:
        /* Unhandled but harmless event. */
        break;
    }
    return BT_OK;
}

/* ------------------------------------------------------------------ */
/* Device scanning                                                    */
/* ------------------------------------------------------------------ */

int bt_scan_start(void)
{
    if (!g_bluetooth_info.controller_present)
        return BT_ERR_NO_DEVICE;

    /* HCI Inquiry: LAP=0x9E8B32 (GIAC), encoded little-endian,
     * length = 0x08 (~10.24 s), max responses = 0 (unlimited). */
    uint8_t params[5];
    params[0] = 0x33; /* LAP 0x9E8B33 LE */
    params[1] = 0x8B;
    params[2] = 0x9E;
    params[3] = 0x08;
    params[4] = 0x00;
    int rc = bt_hci_send_command(HCI_OP_INQUIRY, params, sizeof(params));
    if (rc == BT_OK)
        g_bluetooth_info.scanning = true;
    return rc;
}

int bt_scan_stop(void)
{
    if (!g_bluetooth_info.controller_present)
        return BT_ERR_NO_DEVICE;
    uint8_t lap[3] = { 0x33, 0x8B, 0x9E };
    int rc = bt_hci_send_command(HCI_OP_INQUIRY_CANCEL, lap, sizeof(lap));
    g_bluetooth_info.scanning = false;
    return rc;
}

bool bt_is_scanning(void)
{
    return g_bluetooth_info.scanning;
}

int bt_get_devices(bt_device_t *out, uint32_t max_count)
{
    if (!out || !max_count) return 0;
    uint32_t n = g_bluetooth_info.device_count;
    if (n > max_count) n = max_count;
    memcpy(out, g_bluetooth_info.devices, n * sizeof(bt_device_t));
    return (int)n;
}

uint32_t bt_device_count(void)
{
    return g_bluetooth_info.device_count;
}

const bt_device_t *bt_find_device(const uint8_t *addr)
{
    if (!addr) return NULL;
    return find_dev_raw(addr);
}

/* ------------------------------------------------------------------ */
/* L2CAP layer                                                        */
/* ------------------------------------------------------------------ */

/* 等待 ACL 链路建立后再发出的 L2CAP 信令包（每通道一个槽）。 */
static uint8_t  sig_pending[BT_L2CAP_MAX_CHANNELS][16];
static uint16_t sig_pending_len[BT_L2CAP_MAX_CHANNELS];
static uint8_t  sig_pending_addr[BT_L2CAP_MAX_CHANNELS][BT_ADDR_LEN];

static void bt_l2cap_flush_pending(const uint8_t *addr, uint16_t handle)
{
    for (int i = 0; i < BT_L2CAP_MAX_CHANNELS; i++) {
        if (sig_pending_len[i] == 0u) continue;
        if (!addr_eq(sig_pending_addr[i], addr)) continue;
        /* L2CAP 信令通道 CID 固定为 0x0001。 */
        (void) bt_acl_send(handle, 0x2u, 0x0001u,
                           sig_pending[i], sig_pending_len[i]);
        sig_pending_len[i] = 0u;
    }
}

int bt_l2cap_connect_ex(const uint8_t *addr, uint16_t psm, uint16_t *out_cid)
{
    if (!g_bluetooth_info.controller_present)
        return BT_ERR_NO_DEVICE;
    if (!addr) return BT_ERR_INVALID;

    /* 1. Bring up the ACL link to the remote. */
    {
        uint8_t p[13];
        memset(p, 0, sizeof(p));
        memcpy(p, addr, BT_ADDR_LEN);
        put_le16(p + 6, 0xDBE8); /* packet type: no 3-slot */
        p[8] = 0x01;             /* page scan rep mode */
        p[9] = 0x00;             /* reserved */
        put_le16(p + 10, 0x0000);/* clock offset */
        p[12] = 0x00;             /* allow role switch */
        int rc = bt_hci_send_command(HCI_OP_CREATE_CONN, p, sizeof(p));
        if (rc != BT_OK) return rc;
    }

    /* 2. Allocate a dynamic CID (start at 0x0040). */
    bt_l2cap_channel_t *ch = alloc_l2cap_channel();
    if (!ch) return BT_ERR_FULL;

    static uint16_t next_cid = 0x0040;
    int slot = (int) (ch - &g_bluetooth_info.l2cap_channels[0]);
    memset(ch, 0, sizeof(*ch));
    ch->cid = next_cid++;
    ch->psm = psm;
    ch->mtu = 672;
    memcpy(ch->remote_addr, addr, BT_ADDR_LEN);

    /* 3. Send L2CAP Connect Request on the signaling CID 0x0001.
     *    signaling: code=0x02, ident=1, len=4, PSM(2), SCID(2). */
    uint8_t sig[8];
    sig[0] = L2CAP_CONN_REQ;
    sig[1] = 0x01;                 /* identifier */
    put_le16(sig + 2, 4);          /* length */
    put_le16(sig + 4, psm);
    put_le16(sig + 6, ch->cid);

    /* 如果 ACL 链路已经建立，直接发；否则缓存起来等 CONN_COMPLETE。 */
    {
        bt_device_t *d = find_dev_raw(addr);

        if (d && d->connected && d->handle != 0u) {
            int rc = bt_acl_send(d->handle, 0x2u, 0x0001u, sig, sizeof(sig));
            if (rc != BT_OK) return rc;
        } else {
            memcpy(sig_pending[slot], sig, sizeof(sig));
            sig_pending_len[slot] = (uint16_t) sizeof(sig);
            memcpy(sig_pending_addr[slot], addr, BT_ADDR_LEN);
        }
    }

    /* Connect Response 到达前先标记为打开，上层可以开始排 UIH 帧；
     * 远端应答后 bt_l2cap_process_acl() 会用真实 DCID 收敛。 */
    ch->open = true;
    if (out_cid) {
        *out_cid = ch->cid;
    }
    return BT_OK;
}

int bt_l2cap_connect(const uint8_t *addr, uint16_t psm)
{
    return bt_l2cap_connect_ex(addr, psm, NULL);
}

int bt_l2cap_send(uint16_t cid, const uint8_t *data, uint16_t len)
{
    bt_l2cap_channel_t *ch;
    bt_device_t *dev;
    uint16_t handle = 0u;

    if (!g_bluetooth_info.controller_present)
        return BT_ERR_NO_DEVICE;
    ch = find_l2cap_cid(cid);
    if (!ch) return BT_ERR_INVALID;
    if (!data || len == 0) return 0;

    /* ACL 头里的 handle 来自设备表（L2CAP 通道只记远端地址）。 */
    dev = find_dev_raw(ch->remote_addr);
    if (dev != NULL) {
        handle = dev->handle;
    }

    /* B-frame: L2CAP length(2 LE) + CID(2 LE) + payload，
     * 外面再包一层 HCI ACL 头（handle+PB+BC, 总长）。
     * 出向帧的目标 CID 是远端的 DCID；还没协商到就先用本地 CID。 */
    return bt_acl_send(handle, 0x2u,
                       ch->remote_cid != 0u ? ch->remote_cid : cid,
                       data, len);
}

int bt_l2cap_receive(uint16_t cid, uint8_t *data, uint16_t max_len)
{
    if (!data || !max_len) return 0;
    for (int i = 0; i < BT_L2CAP_MAX_CHANNELS; i++) {
        if (g_bluetooth_info.l2cap_channels[i].open &&
            g_bluetooth_info.l2cap_channels[i].cid == cid) {
            uint16_t n = l2cap_rx_len[i];
            if (n > max_len) n = max_len;
            memcpy(data, l2cap_rx[i], n);
            l2cap_rx_len[i] = 0;
            return (int)n;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* RFCOMM layer (TS 07.10 / Bluetooth serial port emulation)          */
/* ------------------------------------------------------------------ */

int bt_rfcomm_connect(const uint8_t *addr, uint8_t channel)
{
    if (!g_bluetooth_info.controller_present)
        return BT_ERR_NO_DEVICE;
    if (!addr || channel == 0 || channel > 30) return BT_ERR_INVALID;

    /* RFCOMM runs over L2CAP PSM 0x0003；拿回本地 CID 才能发帧。 */
    uint16_t cid = 0u;
    int rc = bt_l2cap_connect_ex(addr, L2CAP_PSM_RFCOMM, &cid);
    if (rc != BT_OK) return rc;

    bt_rfcomm_channel_t *ch = alloc_rfcomm_channel();
    if (!ch) return BT_ERR_FULL;

    uint8_t dlci = (uint8_t)(channel << 1);
    memset(ch, 0, sizeof(*ch));
    ch->dlci = dlci;
    memcpy(ch->remote_addr, addr, BT_ADDR_LEN);
    ch->l2cap_cid = cid;

    /* Build SABME frame: addr=(dlci<<2)|0x03, ctrl=0x2F, len=0x01, FCS. */
    uint8_t f[4];
    f[0] = (uint8_t)((dlci << 2) | 0x03);
    f[1] = RFCOMM_SABME;
    f[2] = 0x01;                    /* length=0, EA=1 */
    f[3] = rfcomm_fcs(f, 3);
    (void) bt_l2cap_send(ch->l2cap_cid, f, sizeof(f));

    ch->open = true;
    ch->connected = true;
    return BT_OK;
}

int bt_rfcomm_send(uint8_t dlci, const uint8_t *data, uint16_t len)
{
    if (!g_bluetooth_info.controller_present)
        return BT_ERR_NO_DEVICE;
    bt_rfcomm_channel_t *ch = find_rfcomm_dlci(dlci);
    if (!ch || !ch->connected) return BT_ERR_INVALID;
    if (!data || len == 0) return 0;

    /* UIH frame: addr, control=0xEF, length, info..., FCS. */
    uint8_t frame[BT_RFCOMM_RX_BUF + 8];
    uint16_t pos = 0;
    frame[pos++] = (uint8_t)((dlci << 2) | 0x03);
    frame[pos++] = RFCOMM_UIH;
    if (len < 128) {
        frame[pos++] = (uint8_t)((len << 1) | 0x01);
    } else {
        frame[pos++] = (uint8_t)((len & 0x7F) << 1);
        frame[pos++] = (uint8_t)((len >> 7) & 0x03);
    }
    memcpy(frame + pos, data, len);
    pos += len;
    {
        uint8_t fcs = rfcomm_fcs(frame, pos);
        frame[pos++] = fcs;
    }

    /* Hand to L2CAP on the RFCOMM PSM channel. */
    if (ch->l2cap_cid == 0u) {
        return BT_ERR_NOT_READY;
    }
    return bt_l2cap_send(ch->l2cap_cid, frame, pos) == BT_OK ? (int)len
                                                             : BT_ERR_IO;
}

int bt_rfcomm_receive(uint8_t dlci, uint8_t *data, uint16_t max_len)
{
    if (!data || !max_len) return 0;
    for (int i = 0; i < BT_RFCOMM_MAX_CHANNELS; i++) {
        if (g_bluetooth_info.rfcomm_channels[i].open &&
            g_bluetooth_info.rfcomm_channels[i].dlci == dlci) {
            uint16_t n = rfcomm_rx_len[i];
            if (n > max_len) n = max_len;
            memcpy(data, rfcomm_rx[i], n);
            rfcomm_rx_len[i] = 0;
            return (int)n;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Pairing / connection                                               */
/* ------------------------------------------------------------------ */

int bt_pair(const uint8_t *addr, const char *pin)
{
    if (!g_bluetooth_info.controller_present)
        return BT_ERR_NO_DEVICE;
    if (!addr) return BT_ERR_INVALID;

    /* Remember the PIN for the upcoming PIN Code Request event. */
    memset(pending_pin, 0, sizeof(pending_pin));
    if (pin) {
        uint32_t l = strlen(pin);
        if (l > BT_MAX_PIN_LEN - 1) l = BT_MAX_PIN_LEN - 1;
        memcpy(pending_pin, pin, l);
    }
    memcpy(pending_pin_addr, addr, BT_ADDR_LEN);

    /* Kick off the ACL connection; auth/pin flow completes via events. */
    uint8_t p[13];
    memset(p, 0, sizeof(p));
    memcpy(p, addr, BT_ADDR_LEN);
    put_le16(p + 6, 0xDBE8);
    p[8] = 0x01;
    p[12] = 0x00;
    int rc = bt_hci_send_command(HCI_OP_CREATE_CONN, p, sizeof(p));
    if (rc == BT_OK) {
        bt_device_t *d = find_dev_raw(addr);
        if (d) d->paired = true;
    }
    return rc;
}

int bt_unpair(const uint8_t *addr)
{
    if (!addr) return BT_ERR_INVALID;
    bt_device_t *d = find_dev_raw(addr);
    if (d) {
        d->paired = false;
        d->connected = false;
        d->handle = 0;
    }
    return BT_OK;
}

int bt_connect(const uint8_t *addr)
{
    if (!g_bluetooth_info.controller_present)
        return BT_ERR_NO_DEVICE;
    if (!addr) return BT_ERR_INVALID;

    /* ACL link + L2CAP (RFCOMM profile on PSM 3). */
    uint8_t p[13];
    memset(p, 0, sizeof(p));
    memcpy(p, addr, BT_ADDR_LEN);
    put_le16(p + 6, 0xDBE8);
    p[8] = 0x01;
    int rc = bt_hci_send_command(HCI_OP_CREATE_CONN, p, sizeof(p));
    if (rc != BT_OK) return rc;

    rc = bt_l2cap_connect(addr, L2CAP_PSM_RFCOMM);
    return rc;
}

int bt_disconnect(const uint8_t *addr)
{
    if (!g_bluetooth_info.controller_present)
        return BT_ERR_NO_DEVICE;
    if (!addr) return BT_ERR_INVALID;
    bt_device_t *d = find_dev_raw(addr);
    if (!d || !d->connected) return BT_ERR_INVALID;

    uint8_t p[3];
    put_le16(p, d->handle);
    p[2] = 0x13; /* Remote User Terminated Connection */
    int rc = bt_hci_send_command(HCI_OP_DISCONNECT, p, sizeof(p));
    d->connected = false;
    d->handle = 0;
    return rc;
}

bool bt_is_connected(const uint8_t *addr)
{
    if (!addr) return false;
    const bt_device_t *d = find_dev_raw(addr);
    return d && d->connected;
}

/* ------------------------------------------------------------------ */
/* Address utilities                                                  */
/* ------------------------------------------------------------------ */

static const char hex_digits[] = "0123456789ABCDEF";

void bt_addr_to_string(const uint8_t *addr, char *out, uint32_t out_len)
{
    if (!out || out_len < BT_ADDR_STR_LEN) return;
    for (int i = 0; i < BT_ADDR_LEN; i++) {
        out[i * 3 + 0] = hex_digits[(addr[i] >> 4) & 0x0F];
        out[i * 3 + 1] = hex_digits[addr[i] & 0x0F];
        out[i * 3 + 2] = (i == BT_ADDR_LEN - 1) ? '\0' : ':';
    }
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int bt_string_to_addr(const char *str, uint8_t *addr)
{
    if (!str || !addr) return BT_ERR_INVALID;
    for (int i = 0; i < BT_ADDR_LEN; i++) {
        int hi = hex_nibble(str[i * 3 + 0]);
        int lo = hex_nibble(str[i * 3 + 1]);
        if (hi < 0 || lo < 0) return BT_ERR_INVALID;
        addr[i] = (uint8_t)((hi << 4) | lo);
        if (i < BT_ADDR_LEN - 1 && str[i * 3 + 2] != ':')
            return BT_ERR_INVALID;
    }
    return BT_OK;
}

/* ------------------------------------------------------------------ */
/* RX path: ACL demultiplexing + transport polling                    */
/* ------------------------------------------------------------------ */

/* 处理一个 HCI ACL 数据包（入参不含 HCI 包类型字节）。
 * 布局：handle+PB+BC(2) | 数据总长(2) | L2CAP 长度(2) | CID(2) | 载荷。
 * 只处理首片（PB=0b10）；分片重组不在本驱动范围内，会明确丢弃。 */
static void bt_l2cap_process_acl(const uint8_t *pkt, uint16_t len)
{
    uint16_t hdr, dlen, l2len, cid;
    const uint8_t *payload;
    uint16_t payload_len;
    uint8_t pb;

    if (len < 8u) return;

    hdr = get_le16(pkt);
    pb = (uint8_t) ((hdr >> 12) & 0x3u);
    if (pb != 0x2u) return;                 /* 只解析首片 */

    dlen = get_le16(pkt + 2);
    if ((uint32_t) dlen + 4u > (uint32_t) len) return;

    l2len = get_le16(pkt + 4);
    cid = get_le16(pkt + 6);
    payload = pkt + 8;
    payload_len = (dlen >= 4u) ? (uint16_t) (dlen - 4u) : 0u;
    if (l2len < payload_len) payload_len = l2len;

    if (cid == 0x0001u) {
        /* L2CAP 信令通道：处理 Connect Response。 */
        uint8_t code;
        uint16_t plen;

        if (payload_len < 4u) return;
        code = payload[0];
        plen = get_le16(payload + 2);
        if (plen > (uint16_t) (payload_len - 4u)) {
            plen = (uint16_t) (payload_len - 4u);
        }

        if (code == L2CAP_CONN_RSP && plen >= 8u) {
            uint16_t dcid = get_le16(payload + 4);
            uint16_t scid = get_le16(payload + 6);
            uint16_t result = get_le16(payload + 8);
            bt_l2cap_channel_t *ch = find_l2cap_cid(scid);

            if (ch != NULL) {
                if (result == 0u) {
                    ch->remote_cid = dcid;   /* 出向帧按远端 CID 寻址 */
                    ch->open = true;
                } else {
                    ch->open = false;
                }
            }
        }
        return;
    }

    /* 数据通道：先按本地 CID 找到 L2CAP 通道，再按 PSM 分发。 */
    for (int i = 0; i < BT_L2CAP_MAX_CHANNELS; i++) {
        bt_l2cap_channel_t *ch = &g_bluetooth_info.l2cap_channels[i];
        uint16_t n;

        if (!ch->open || ch->cid != cid) continue;

        if (ch->psm != L2CAP_PSM_RFCOMM) {
            n = payload_len;
            if (n > BT_L2CAP_RX_BUF) n = BT_L2CAP_RX_BUF;
            memcpy(l2cap_rx[i], payload, n);
            l2cap_rx_len[i] = n;
            return;
        }

        /* RFCOMM：addr 字节的 bit2..7 是 DLCI。 */
        if (payload_len < 4u) return;
        {
            uint8_t dlci = (uint8_t) (payload[0] >> 2);
            bt_rfcomm_channel_t *rc = find_rfcomm_dlci(dlci);
            uint32_t info_len;

            if (rc == NULL) return;
            /* 帧尾 1 字节是 FCS，校验不过就丢弃。 */
            if (rfcomm_fcs(payload, (uint32_t) payload_len - 1u) !=
                payload[payload_len - 1u]) {
                return;
            }
            info_len = (uint32_t) payload_len - 4u;   /* addr+ctrl+len+fcs */
            if (info_len > BT_RFCOMM_RX_BUF) info_len = BT_RFCOMM_RX_BUF;
            {
                int slot = (int) (rc - &g_bluetooth_info.rfcomm_channels[0]);

                memcpy(rfcomm_rx[slot], payload + 3, info_len);
                rfcomm_rx_len[slot] = (uint16_t) info_len;
            }
        }
        return;
    }
}

/* 轮询传输层：事件走中断端点，ACL 数据走批量端点。
 * 每次最多处理 4 个包，避免在单次调用里长时间占住 CPU。 */
int bt_hci_poll(void)
{
    uint8_t buf[BT_HCI_ACL_BUF_SIZE + 8];
    int processed = 0;

    if (!g_bt_usb.ready) {
        return 0;
    }

    /* HCI 事件（中断端点） */
    if (g_bt_usb.intr_in_ep != 0u) {
        int n = bt_usb_recv(buf, sizeof(buf), true);

        if (n > 0 && buf[0] == HCI_EVENT_PKT && n >= 2) {
            (void) bt_hci_process_event(buf + 1, (uint16_t) (n - 1));
            processed++;
        }
    }

    /* ACL 数据（批量端点） */
    for (int i = 0; i < 4; i++) {
        int n = bt_usb_recv(buf, sizeof(buf), false);

        if (n <= 0) break;
        if (buf[0] == HCI_ACLDATA_PKT && n >= 2) {
            bt_l2cap_process_acl(buf + 1, (uint16_t) (n - 1));
            processed++;
        } else if (buf[0] == HCI_EVENT_PKT && n >= 2) {
            /* 有些控制器把事件也放在批量端点上。 */
            (void) bt_hci_process_event(buf + 1, (uint16_t) (n - 1));
            processed++;
        }
    }
    return processed;
}
