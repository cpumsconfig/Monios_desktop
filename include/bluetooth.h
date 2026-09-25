#ifndef _BLUETOOTH_H_
#define _BLUETOOTH_H_

#include "stdbool.h"
#include "stdint.h"

#define BT_ADDR_LEN            6
#define BT_MAX_DEVICES         16
#define BT_MAX_NAME_LEN        32
#define BT_MAX_PIN_LEN         16
#define BT_L2CAP_MAX_CHANNELS  8
#define BT_RFCOMM_MAX_CHANNELS 8
#define BT_HCI_CMD_BUF_SIZE    256
#define BT_HCI_EVT_BUF_SIZE    256
#define BT_HCI_ACL_BUF_SIZE    512
#define BT_ADDR_STR_LEN        18   /* "XX:XX:XX:XX:XX:XX" + NUL */

/* ---- HCI packet types ---- */
#define HCI_COMMAND_PKT  0x01
#define HCI_ACLDATA_PKT  0x02
#define HCI_EVENT_PKT    0x04

/* ---- HCI opcodes (OGF << 10 | OCF) ---- */
#define HCI_OP_RESET                  0x0C03
#define HCI_OP_SET_EVENT_MASK         0x0C01
#define HCI_OP_WRITE_CLASS_OF_DEVICE  0x0C24
#define HCI_OP_WRITE_SCAN_ENABLE      0x0C1A
#define HCI_OP_CHANGE_LOCAL_NAME      0x0C13
#define HCI_OP_HOST_BUFFER_SIZE       0x0C33
#define HCI_OP_READ_LOCAL_VERSION     0x0401
#define HCI_OP_READ_BD_ADDR           0x1009
#define HCI_OP_INQUIRY                0x0401
#define HCI_OP_INQUIRY_CANCEL         0x0402
#define HCI_OP_CREATE_CONN            0x0405
#define HCI_OP_DISCONNECT             0x0406
#define HCI_OP_ACCEPT_CONN_REQ        0x0409
#define HCI_OP_AUTH_REQUESTED         0x0411
#define HCI_OP_SET_CONN_ENCRYPTION    0x0413
#define HCI_OP_LINK_KEY_REPLY         0x040B
#define HCI_OP_LINK_KEY_NEG_REPLY     0x040C
#define HCI_OP_PIN_CODE_REPLY         0x040D
#define HCI_OP_PIN_CODE_NEG_REPLY     0x040E
#define HCI_OP_READ_REMOTE_NAME       0x0419

/* ---- HCI events ---- */
#define HCI_EVT_INQUIRY_COMPLETE         0x01
#define HCI_EVT_INQUIRY_RESULT           0x02
#define HCI_EVT_CONN_COMPLETE            0x03
#define HCI_EVT_CONN_REQUEST             0x04
#define HCI_EVT_DISCONN_COMPLETE         0x05
#define HCI_EVT_AUTH_COMPLETE            0x06
#define HCI_EVT_REMOTE_NAME_REQ_COMPLETE 0x07
#define HCI_EVT_ENCRYPT_CHANGE           0x08
#define HCI_EVT_CMD_COMPLETE             0x0E
#define HCI_EVT_CMD_STATUS               0x0F
#define HCI_EVT_NUM_COMPLETED_PKTS       0x13
#define HCI_EVT_PIN_CODE_REQ             0x16
#define HCI_EVT_LINK_KEY_REQ             0x17
#define HCI_EVT_LINK_KEY_NOTIFY          0x18

/* ---- L2CAP PSM values ---- */
#define L2CAP_PSM_SDP    0x0001
#define L2CAP_PSM_RFCOMM 0x0003

/* ---- L2CAP signaling codes ---- */
#define L2CAP_CMD_REJ       0x01
#define L2CAP_CONN_REQ      0x02
#define L2CAP_CONN_RSP      0x03
#define L2CAP_CFG_REQ       0x04
#define L2CAP_CFG_RSP       0x05
#define L2CAP_DISCONN_REQ   0x06
#define L2CAP_DISCONN_RSP   0x07
#define L2CAP_INFO_REQ      0x0A
#define L2CAP_INFO_RSP      0x0B

/* ---- RFCOMM frame types ---- */
#define RFCOMM_SABME 0x2F
#define RFCOMM_UA    0x63
#define RFCOMM_DM    0x0F
#define RFCOMM_DISC  0x43
#define RFCOMM_UIH   0xEF

/* ---- Error codes ---- */
#define BT_OK              0
#define BT_ERR_NOT_READY  -1
#define BT_ERR_NO_DEVICE  -2
#define BT_ERR_TIMEOUT    -3
#define BT_ERR_INVALID    -4
#define BT_ERR_FULL       -5
#define BT_ERR_IO         -6

/* ---- A discovered / paired device ---- */
typedef struct {
    uint8_t  addr[BT_ADDR_LEN];
    char     name[BT_MAX_NAME_LEN];
    uint32_t class_of_device;
    int8_t   rssi;
    bool     paired;
    bool     connected;
    uint16_t handle;
} bt_device_t;

/* ---- L2CAP channel ---- */
typedef struct {
    uint16_t cid;          /* 本地 source CID (SCID)，入向数据按它匹配 */
    uint16_t remote_cid;   /* 远端 CID (DCID)，出向数据按它寻址；0 = 未协商 */
    uint16_t psm;
    uint16_t mtu;
    bool     open;
    uint8_t  remote_addr[BT_ADDR_LEN];
} bt_l2cap_channel_t;

/* ---- RFCOMM channel (serial emulation) ---- */
typedef struct {
    uint8_t  dlci;
    bool     open;
    bool     connected;
    uint16_t l2cap_cid;
    uint8_t  remote_addr[BT_ADDR_LEN];
} bt_rfcomm_channel_t;

/* ---- Global Bluetooth state ---- */
typedef struct {
    bool     initialized;
    bool     controller_present;
    bool     usb_transport_ready;
    bool     hci_ready;
    bool     scanning;
    uint32_t controllers;
    uint8_t  local_addr[BT_ADDR_LEN];
    char     local_name[BT_MAX_NAME_LEN];
    char     status[64];
    bt_device_t devices[BT_MAX_DEVICES];
    uint32_t device_count;
    bt_l2cap_channel_t  l2cap_channels[BT_L2CAP_MAX_CHANNELS];
    bt_rfcomm_channel_t rfcomm_channels[BT_RFCOMM_MAX_CHANNELS];
} bluetooth_info_t;

/* ---- Lifecycle ---- */
void bluetooth_init(void);
const bluetooth_info_t *bluetooth_info(void);
const char *bluetooth_status(void);

/* ---- HCI layer ---- */
int  bt_hci_send_command(uint16_t opcode, const uint8_t *params, uint8_t param_len);
int  bt_hci_process_event(const uint8_t *data, uint16_t len);
bool bt_hci_is_ready(void);
/* 轮询 HCI 传输层：取出并分发所有待处理的 HCI 事件/ACL 包。
 * 返回本次处理的包数；传输层未就绪返回 0。 */
int  bt_hci_poll(void);
/* USB HCI 传输层是否已就绪（发现控制器并拿到批量端点）。 */
bool bt_hci_transport_ready(void);

/* ---- Device scanning ---- */
int  bt_scan_start(void);
int  bt_scan_stop(void);
bool bt_is_scanning(void);
int  bt_get_devices(bt_device_t *out, uint32_t max_count);
uint32_t bt_device_count(void);
const bt_device_t *bt_find_device(const uint8_t *addr);

/* ---- Pairing ---- */
int bt_pair(const uint8_t *addr, const char *pin);
int bt_unpair(const uint8_t *addr);

/* ---- Connection ---- */
int  bt_connect(const uint8_t *addr);
int  bt_disconnect(const uint8_t *addr);
bool bt_is_connected(const uint8_t *addr);

/* ---- L2CAP layer ---- */
int bt_l2cap_connect(const uint8_t *addr, uint16_t psm);
/* 同上，但把分配的本地 CID 回传给调用者（RFCOMM 需要用它发帧）。 */
int bt_l2cap_connect_ex(const uint8_t *addr, uint16_t psm, uint16_t *out_cid);
int bt_l2cap_send(uint16_t cid, const uint8_t *data, uint16_t len);
int bt_l2cap_receive(uint16_t cid, uint8_t *data, uint16_t max_len);

/* ---- RFCOMM layer (serial port emulation) ---- */
int bt_rfcomm_connect(const uint8_t *addr, uint8_t channel);
int bt_rfcomm_send(uint8_t dlci, const uint8_t *data, uint16_t len);
int bt_rfcomm_receive(uint8_t dlci, uint8_t *data, uint16_t max_len);

/* ---- Address utilities ---- */
void bt_addr_to_string(const uint8_t *addr, char *out, uint32_t out_len);
int  bt_string_to_addr(const char *str, uint8_t *addr);

#endif /* _BLUETOOTH_H_ */
