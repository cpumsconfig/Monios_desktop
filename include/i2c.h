#ifndef _I2C_H_
#define _I2C_H_

#include "stdbool.h"
#include "stdint.h"

#define I2C_SPEED_STANDARD    100000
#define I2C_SPEED_FAST        400000
#define I2C_SPEED_FAST_PLUS   1000000
#define I2C_SPEED_HIGH        3400000

#define I2C_FLAG_READ         0x01
#define I2C_FLAG_WRITE        0x00
#define I2C_FLAG_10BIT_ADDR   0x02

typedef struct {
    bool available;
    uint8_t bus_count;
    uint32_t bus_speed;
    uint8_t last_address;
    uint32_t transfer_count;
    uint32_t error_count;
    char status[64];
} i2c_info_t;

typedef struct {
    uint16_t addr;
    uint8_t *buf;
    uint32_t len;
    uint8_t flags;
} i2c_msg_t;

void i2c_init(void);
bool i2c_probe(uint8_t bus, uint8_t address);
int32_t i2c_transfer(uint8_t bus, i2c_msg_t *msgs, uint32_t num);
int32_t i2c_read(uint8_t bus, uint8_t addr, uint8_t reg, uint8_t *buf, uint32_t len);
int32_t i2c_write(uint8_t bus, uint8_t addr, uint8_t reg, const uint8_t *buf, uint32_t len);
uint32_t i2c_scan(uint8_t bus, uint8_t *buffer, uint32_t capacity);
const i2c_info_t *i2c_info(void);
const char *i2c_status(void);

#endif
