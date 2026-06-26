#include "i2c.h"
#include "common.h"
#include "pci.h"
#include "smbus.h"
#include "kernel.h"

static i2c_info_t g_i2c_info;
static uint8_t g_i2c_error_count;

/* I2C bus speed configuration */
static const uint32_t i2c_speed_table[] = {
    I2C_SPEED_STANDARD,
    I2C_SPEED_FAST,
    I2C_SPEED_FAST_PLUS,
    I2C_SPEED_HIGH
};

void i2c_init(void)
{
    memset(&g_i2c_info, 0, sizeof(g_i2c_info));
    g_i2c_info.available = smbus_available();
    g_i2c_info.bus_count = g_i2c_info.available ? 1u : 0u;
    g_i2c_info.bus_speed = I2C_SPEED_STANDARD;
    g_i2c_info.last_address = 0;
    g_i2c_info.transfer_count = 0;
    g_i2c_info.error_count = 0;
    g_i2c_error_count = 0;

    if (g_i2c_info.available) {
        strcpy(g_i2c_info.status, "i2c: smbus bridge online (standard mode)");
        log_write("i2c: initialized via SMBus bridge");
    } else {
        strcpy(g_i2c_info.status, "i2c: no adapter found");
        log_write("i2c: no SMBus adapter available");
    }
}

bool i2c_probe(uint8_t bus, uint8_t address)
{
    (void) bus;

    if (!g_i2c_info.available) {
        strcpy(g_i2c_info.status, "i2c: adapter unavailable");
        return false;
    }

    /* Validate address - 7-bit addressing */
    if (address < 0x03 || address > 0x77) {
        strcpy(g_i2c_info.status, "i2c: invalid address");
        return false;
    }

    g_i2c_info.last_address = address;

    if (smbus_probe_device(address)) {
        strcpy(g_i2c_info.status, "i2c: device acknowledged");
        return true;
    }

    g_i2c_info.error_count++;
    strcpy(g_i2c_info.status, "i2c: no response from device");
    return false;
}

/* Execute a single I2C message via SMBus */
static int32_t i2c_exec_msg(uint8_t addr, i2c_msg_t *msg)
{
    uint8_t flags = msg->flags;
    bool is_read = (flags & I2C_FLAG_READ) != 0;

    if (msg->len == 0 || msg->buf == NULL) {
        return -1;
    }

    if (is_read) {
        /* Read operation */
        if (msg->len == 1) {
            uint8_t data = 0;
            if (smbus_read_byte(addr, msg->buf[0], &data)) {
                msg->buf[0] = data;
                return 1;
            }
        } else if (msg->len == 2) {
            uint16_t data = 0;
            if (smbus_read_word(addr, msg->buf[0], &data)) {
                msg->buf[0] = (uint8_t)(data & 0xFF);
                msg->buf[1] = (uint8_t)((data >> 8) & 0xFF);
                return 2;
            }
        } else {
            /* Block read - simulate with multiple byte reads for compatibility */
            uint8_t reg = msg->buf[0];
            for (uint32_t i = 0; i < msg->len; i++) {
                uint8_t data = 0;
                if (!smbus_read_byte(addr, (uint8_t)(reg + i), &data)) {
                    return (int32_t)i;
                }
                msg->buf[i] = data;
            }
            return (int32_t)msg->len;
        }
    } else {
        /* Write operation */
        if (msg->len == 1) {
            if (smbus_quick_command(addr)) {
                return 1;
            }
        } else if (msg->len == 2) {
            if (smbus_write_byte(addr, msg->buf[0], msg->buf[1])) {
                return 2;
            }
        } else if (msg->len == 3) {
            uint16_t data = (uint16_t)((msg->buf[2] << 8) | msg->buf[1]);
            if (smbus_write_word(addr, msg->buf[0], data)) {
                return 3;
            }
        } else {
            /* Block write - simulate with multiple byte writes */
            uint8_t reg = msg->buf[0];
            for (uint32_t i = 1; i < msg->len; i++) {
                if (!smbus_write_byte(addr, (uint8_t)(reg + i - 1), msg->buf[i])) {
                    return (int32_t)(i - 1);
                }
            }
            return (int32_t)msg->len;
        }
    }

    g_i2c_error_count++;
    return -1;
}

int32_t i2c_transfer(uint8_t bus, i2c_msg_t *msgs, uint32_t num)
{
    (void) bus;
    int32_t total_transferred = 0;

    if (!g_i2c_info.available || msgs == NULL || num == 0) {
        return -1;
    }

    g_i2c_info.transfer_count++;

    for (uint32_t i = 0; i < num; i++) {
        int32_t ret = i2c_exec_msg(msgs[i].addr, &msgs[i]);
        if (ret < 0) {
            g_i2c_info.error_count++;
            strcpy(g_i2c_info.status, "i2c: transfer error");
            return total_transferred > 0 ? total_transferred : -1;
        }
        total_transferred += ret;
    }

    strcpy(g_i2c_info.status, "i2c: transfer complete");
    return total_transferred;
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
    uint8_t write_buf[64];
    uint32_t total_len;

    if (buf == NULL || len == 0 || len > 63) {
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

    log_write("i2c: starting bus scan...");

    for (uint8_t address = 0x03; address <= 0x77 && count < capacity; address++) {
        if (i2c_probe(bus, address)) {
            buffer[count++] = address;
        }
    }

    if (count > 0) {
        strcpy(g_i2c_info.status, "i2c: scan complete, devices found");
        log_write("i2c: scan complete - devices found");
    } else {
        strcpy(g_i2c_info.status, "i2c: scan complete, no devices");
        log_write("i2c: scan complete - no devices found");
    }

    return count;
}

/* Set I2C bus speed */
int32_t i2c_set_speed(uint8_t bus, uint32_t speed_hz)
{
    (void) bus;

    if (!g_i2c_info.available) {
        return -1;
    }

    /* Validate speed against supported modes */
    for (uint32_t i = 0; i < sizeof(i2c_speed_table) / sizeof(i2c_speed_table[0]); i++) {
        if (speed_hz == i2c_speed_table[i]) {
            g_i2c_info.bus_speed = speed_hz;
            strcpy(g_i2c_info.status, "i2c: speed updated");
            return 0;
        }
    }

    strcpy(g_i2c_info.status, "i2c: unsupported speed");
    return -1;
}

const i2c_info_t *i2c_info(void)
{
    return &g_i2c_info;
}

const char *i2c_status(void)
{
    return g_i2c_info.status;
}
