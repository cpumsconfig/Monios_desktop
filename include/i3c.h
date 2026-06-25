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
const i3c_info_t *i3c_info(void);
const char *i3c_status(void);

#endif
