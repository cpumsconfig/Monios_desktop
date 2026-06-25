#include "i3c.h"
#include "common.h"
#include "i2c.h"

static i3c_info_t g_i3c_info;
static i3c_device_t g_i3c_devices[I3C_MAX_DEVICES];

void i3c_init(void)
{
    memset(&g_i3c_info, 0, sizeof(g_i3c_info));
    memset(g_i3c_devices, 0, sizeof(g_i3c_devices));
    g_i3c_info.available = false;
    g_i3c_info.bus_count = 0;
    g_i3c_info.device_count = 0;
    g_i3c_info.i2c_devices_present = false;
    g_i3c_info.scl_freq = 12500000; /* 12.5 MHz typical */
    strcpy(g_i3c_info.status, "i3c: controller not detected");
}

bool i3c_probe(uint8_t bus, uint8_t addr)
{
    (void) bus;
    if (!g_i3c_info.available) {
        strcpy(g_i3c_info.status, "i3c: controller unavailable");
        return false;
    }
    /* TODO: implement I3C probe */
    strcpy(g_i3c_info.status, "i3c: probe stub");
    return false;
}

int32_t i3c_read(uint8_t bus, uint8_t addr, uint8_t *buf, uint32_t len)
{
    (void) bus;
    (void) addr;
    (void) buf;
    (void) len;
    if (!g_i3c_info.available) {
        return -1;
    }
    g_i3c_info.transfer_count++;
    /* TODO: implement I3C read */
    strcpy(g_i3c_info.status, "i3c: read stub");
    return 0;
}

int32_t i3c_write(uint8_t bus, uint8_t addr, const uint8_t *buf, uint32_t len)
{
    (void) bus;
    (void) addr;
    (void) buf;
    (void) len;
    if (!g_i3c_info.available) {
        return -1;
    }
    g_i3c_info.transfer_count++;
    /* TODO: implement I3C write */
    strcpy(g_i3c_info.status, "i3c: write stub");
    return 0;
}

int32_t i3c_send_ccc(uint8_t bus, uint8_t ccc, const uint8_t *data, uint32_t len)
{
    (void) bus;
    (void) ccc;
    (void) data;
    (void) len;
    if (!g_i3c_info.available) {
        return -1;
    }
    /* TODO: implement CCC (Common Command Code) */
    strcpy(g_i3c_info.status, "i3c: ccc stub");
    return 0;
}

int32_t i3c_do_daa(uint8_t bus, i3c_device_t *devices, uint32_t max_devices)
{
    (void) bus;
    (void) devices;
    (void) max_devices;
    if (!g_i3c_info.available) {
        return -1;
    }
    /* TODO: implement DAA (Dynamic Address Assignment) */
    strcpy(g_i3c_info.status, "i3c: daa stub");
    return 0;
}

uint32_t i3c_scan(uint8_t bus, uint8_t *buffer, uint32_t capacity)
{
    (void) bus;
    (void) buffer;
    (void) capacity;
    if (!g_i3c_info.available) {
        return 0;
    }
    /* TODO: implement I3C bus scan */
    strcpy(g_i3c_info.status, "i3c: scan stub");
    return 0;
}

const i3c_info_t *i3c_info(void)
{
    return &g_i3c_info;
}

const char *i3c_status(void)
{
    return g_i3c_info.status;
}
