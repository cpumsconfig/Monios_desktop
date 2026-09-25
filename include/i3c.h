#ifndef _I3C_H_
#define _I3C_H_

#include "stdbool.h"
#include "stdint.h"

#define I3C_BROADCAST_ADDR    0x7E
#define I3C_MAX_DEVICES       128

#define I3C_CCC_ENEC          0x00
#define I3C_CCC_DISEC         0x01
#define I3C_CCC_ENTAS0        0x02
#define I3C_CCC_ENTAS1        0x03
#define I3C_CCC_ENTAS2        0x04
#define I3C_CCC_ENTAS3        0x05
#define I3C_CCC_RSTDAA        0x06
#define I3C_CCC_ENTDAA        0x07
#define I3C_CCC_DEFSVLS       0x08
#define I3C_CCC_SETMWL        0x09
#define I3C_CCC_SETMRL        0x0A
#define I3C_CCC_ENTTM         0x0B
#define I3C_CCC_SETBUSCON     0x0C
#define I3C_CCC_ENDXFER       0x0D
#define I3C_CCC_ENTHDR0       0x20
#define I3C_CCC_ENTHDR1       0x21
#define I3C_CCC_ENTHDR2       0x22
#define I3C_CCC_ENTHDR3       0x23
#define I3C_CCC_ENTHDR4       0x24
#define I3C_CCC_ENTHDR5       0x25
#define I3C_CCC_ENTHDR6       0x26
#define I3C_CCC_ENTHDR7       0x27
#define I3C_CCC_SETXTIME      0x28
#define I3C_CCC_SETAASA       0x29

#define I3C_BCR_IBI           0x01
#define I3C_BCR_IBI_PAYLOAD   0x02
#define I3C_BCR_OFFLINE       0x04
#define I3C_BCR_SIR           0x08
#define I3C_BCR_HDR_CAP       0x10

/* ── 动态地址 (DAA) 规则（MIPI I3C spec §5.1.3 / 表 5） ────────────
 * 合法动态地址范围是 0x08..0x3D；0x00..0x07 为保留/静态专用，
 * 0x3E 与 0x3F 是组播与广播地址，都不能分配给设备。
 * ENTDAA 写回的 8 位地址字节 = 7 位地址 + 1 位奇偶（奇校验）。 */
#define I3C_DYN_ADDR_MIN      0x08u
#define I3C_DYN_ADDR_MAX      0x3Du
#define I3C_DAA_MAX_TARGETS   16u
/* I2C 兼容设备的静态地址上限（7 位地址） */
#define I3C_STATIC_ADDR_MAX   0x7Du

/* ENTDAA 应答的 8 字节布局：PID[47:0] 小端 6 字节 + BCR + DCR */
#define I3C_DAA_INFO_LEN      8u

/* ── 低层总线后端 ──────────────────────────────────────────────────
 * i3c.c 是协议层，具体时序由控制器驱动提供。注册后 i3c_do_daa() 会走
 * 真正的 ENTDAA 事务；未注册时退回 I2C 兼容枚举（不会伪造动态地址）。 */
typedef struct {
    /* 一次 ENTDAA 读事务：有待分配设备时返回 true 并填充 info[8]。 */
    bool (*daa_read)(uint8_t *info);
    /* 把地址字节（7 位地址 | 奇偶位）写回同一 ENTDAA 帧。 */
    bool (*daa_write_addr)(uint8_t addr_byte);
    /* 广播一个直写 CCC。 */
    bool (*ccc_broadcast)(uint8_t ccc, const uint8_t *data, uint32_t len);
} i3c_bus_ops_t;

/* 注册控制器后端（传 NULL 注销）。 */
void i3c_register_bus_ops(const i3c_bus_ops_t *ops);
/* 7 位动态地址是否落在合法范围。 */
bool i3c_daa_addr_valid(uint8_t addr);
/* 计算 ENTDAA 地址字节的奇偶位并返回完整 8 位字节。 */
uint8_t i3c_daa_addr_parity(uint8_t addr7);

typedef struct {
    bool available;
    uint8_t bus_count;
    uint8_t device_count;
    bool i2c_devices_present;
    uint32_t scl_freq;
    uint32_t transfer_count;
    char status[64];
} i3c_info_t;

typedef struct {
    uint8_t dynamic_addr;
    uint8_t static_addr;
    uint16_t pid_hi;
    uint32_t pid_lo;
    uint8_t bcr;
    uint8_t dcr;
    uint16_t max_read_len;
    uint16_t max_write_len;
    bool is_i2c;
    bool has_ibi;
} i3c_device_t;

void i3c_init(void);
bool i3c_probe(uint8_t bus, uint8_t addr);
int32_t i3c_read(uint8_t bus, uint8_t addr, uint8_t *buf, uint32_t len);
int32_t i3c_write(uint8_t bus, uint8_t addr, const uint8_t *buf, uint32_t len);
int32_t i3c_send_ccc(uint8_t bus, uint8_t ccc, const uint8_t *data, uint32_t len);
int32_t i3c_do_daa(uint8_t bus, i3c_device_t *devices, uint32_t max_devices);
uint32_t i3c_scan(uint8_t bus, uint8_t *buffer, uint32_t capacity);
int32_t i3c_get_device_info(uint8_t index, i3c_device_t *info);
int32_t i3c_set_frequency(uint8_t bus, uint32_t freq_hz);
const i3c_info_t *i3c_info(void);
int32_t i3c_find_device(uint8_t addr, i3c_device_t *info);
const char *i3c_status(void);

#endif
