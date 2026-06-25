#include "i2c.h"
#include "common.h"
#include "pci.h"
#include "smbus.h"

static i2c_info_t g_i2c_info;

void i2c_init(void)
{
    memset(&g_i2c_info, 0, sizeof(g_i2c_info));
    g_i2c_info.available = smbus_available();
    g_i2c_info.bus_count = g_i2c_info.available ? 1u : 0u;
    g_i2c_info.bus_speed = I2C_SPEED_STANDARD;
    strcpy(g_i2c_info.status, g_i2c_info.available ? "i2c: smbus bridge online" : "i2c: no adapter");
}

bool i2c_probe(uint8_t bus, uint8_t address)
{
    (void) bus;
    g_i2c_info.last_address = address;
    if (!g_i2c_info.available) {
        strcpy(g_i2c_info.status, "i2c: adapter unavailable");
        return false;
    }
    if (smbus_probe_device(address)) {
        strcpy(g_i2c_info.status, "i2c: device acknowledged");
        return true;
    }
    strcpy(g_i2c_info.status, "i2c: no response");
    return false;
}

int32_t i2c_transfer(uint8_t bus, i2c_msg_t *msgs, uint32_t num)
{
    (void) bus;
    if (!g_i2c_info.available || msgs == NULL || num == 0) {
        return -1;
    }
    g_i2c_info.transfer_count++;
    /* TODO: implement full I2C transfer using SMBus or bit-banging */
    strcpy(g_i2c_info.status, "i2c: transfer stub");
    return 0;
}

int32_t i2c_read(uint8_t bus, uint8_t addr, uint8_t reg, uint8_t *buf, uint32_t len)
{
    i2c_msg_t msgs[2];
    uint8_t reg_buf = reg;

    if (buf == NULL || len == 0) {
        return -1;
    }

    msgs[0].addr = addr;
    msgs[0].buf = &reg_buf;
    msgs[0].len = 1;
    msgs[0].flags = I2C_FLAG_WRITE;

    msgs[1].addr = addr;
    msgs[1].buf = buf;
    msgs[1].len = len;
    msgs[1].flags = I2C_FLAG_READ;

    return i2c_transfer(bus, msgs, 2);
}

int32_t i2c_write(uint8_t bus, uint8_t addr, uint8_t reg, const uint8_t *buf, uint32_t len)
{
    i2c_msg_t msg;
    uint8_t write_buf[32];
    uint32_t total_len;

    if (buf == NULL || len == 0 || len > 31) {
        return -1;
    }

    total_len = len + 1;
    write_buf[0] = reg;
    memcpy(write_buf + 1, buf, len);

    msg.addr = addr;
    msg.buf = write_buf;
    msg.len = total_len;
    msg.flags = I2C_FLAG_WRITE;

    return i2c_transfer(bus, &msg, 1);
}

uint32_t i2c_scan(uint8_t bus, uint8_t *buffer, uint32_t capacity)
{
    uint32_t count = 0;

    if (!g_i2c_info.available || buffer == NULL || capacity == 0) {
        return 0;
    }
    for (uint8_t address = 0x03; address <= 0x77 && count < capacity; address++) {
        if (i2c_probe(bus, address)) {
            buffer[count++] = address;
        }
    }
    strcpy(g_i2c_info.status, count > 0 ? "i2c: scan complete" : "i2c: scan empty");
    return count;
}

const i2c_info_t *i2c_info(void)
{
    return &g_i2c_info;
}

const char *i2c_status(void)
{
    return g_i2c_info.status;
}
