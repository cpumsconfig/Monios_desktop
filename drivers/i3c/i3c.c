#include "i3c.h"
#include "common.h"
#include "i2c.h"
#include "kernel.h"
#include "pci.h"

static i3c_info_t g_i3c_info;
static i3c_device_t g_i3c_devices[I3C_MAX_DEVICES];
static uint8_t g_i3c_device_count;

/* I3C CCC command names for logging */
static const char *i3c_ccc_names[] = {
    "ENEC", "DISEC", "ENTAS0", "ENTAS1", "ENTAS2", "ENTAS3",
    "RSTDAA", "ENTDAA", "DEFSLVS", "SETMWL", "SETMRL", "ENTTM",
    "SETBUSCON", "ENDXFER", NULL, NULL,
    "ENTHDR0", "ENTHDR1", "ENTHDR2", "ENTHDR3", "ENTHDR4",
    "ENTHDR5", "ENTHDR6", "ENTHDR7", "SETXTIME", "SETAASA"
};

/* Get CCC command name */
static const char *i3c_ccc_name(uint8_t ccc)
{
    if (ccc < sizeof(i3c_ccc_names) / sizeof(i3c_ccc_names[0])) {
        return i3c_ccc_names[ccc] ? i3c_ccc_names[ccc] : "RESERVED";
    }
    return "UNKNOWN";
}

/* Initialize I3C controller */
void i3c_init(void)
{
    memset(&g_i3c_info, 0, sizeof(g_i3c_info));
    memset(g_i3c_devices, 0, sizeof(g_i3c_devices));
    g_i3c_device_count = 0;

    g_i3c_info.available = false;
    g_i3c_info.bus_count = 0;
    g_i3c_info.device_count = 0;
    g_i3c_info.i2c_devices_present = false;
    g_i3c_info.scl_freq = 12500000; /* 12.5 MHz typical I3C SDR */
    g_i3c_info.transfer_count = 0;

    /*
     * Try to detect I3C controller via PCI or ACPI.
     * For now, check if I2C/SMBus is available as a fallback
     * since I3C is backward compatible with I2C.
     */
    if (i2c_info()->available) {
        g_i3c_info.available = true;
        g_i3c_info.bus_count = 1;
        g_i3c_info.i2c_devices_present = true;
        strcpy(g_i3c_info.status, "i3c: i2c-compatible mode initialized");
        log_write("i3c: initialized in I2C backward compatibility mode");
    } else {
        strcpy(g_i3c_info.status, "i3c: controller not detected");
        log_write("i3c: no I3C controller found");
    }
}

/* Probe for an I3C device at given address */
bool i3c_probe(uint8_t bus, uint8_t addr)
{
    (void) bus;

    if (!g_i3c_info.available) {
        strcpy(g_i3c_info.status, "i3c: controller unavailable");
        return false;
    }

    /* Try I2C probe first (backward compatibility) */
    if (i2c_probe(0, addr)) {
        strcpy(g_i3c_info.status, "i3c: device found (i2c mode)");
        return true;
    }

    /* TODO: Try I3C-specific probe with ENTDAA */

    strcpy(g_i3c_info.status, "i3c: device not found");
    return false;
}

/* Read data from I3C device */
int32_t i3c_read(uint8_t bus, uint8_t addr, uint8_t *buf, uint32_t len)
{
    (void) bus;

    if (!g_i3c_info.available || buf == NULL || len == 0) {
        return -1;
    }

    g_i3c_info.transfer_count++;

    /* Use I2C read for backward compatibility */
    int32_t ret = i2c_read(0, addr, 0, buf, len);
    if (ret >= 0) {
        strcpy(g_i3c_info.status, "i3c: read complete");
        return ret;
    }

    strcpy(g_i3c_info.status, "i3c: read error");
    return -1;
}

/* Write data to I3C device */
int32_t i3c_write(uint8_t bus, uint8_t addr, const uint8_t *buf, uint32_t len)
{
    (void) bus;

    if (!g_i3c_info.available || buf == NULL || len == 0) {
        return -1;
    }

    g_i3c_info.transfer_count++;

    /* Use I2C write for backward compatibility */
    int32_t ret = i2c_write(0, addr, buf[0], buf + 1, len - 1);
    if (ret >= 0) {
        strcpy(g_i3c_info.status, "i3c: write complete");
        return ret;
    }

    strcpy(g_i3c_info.status, "i3c: write error");
    return -1;
}

/* Send CCC (Common Command Code) broadcast */
int32_t i3c_send_ccc(uint8_t bus, uint8_t ccc, const uint8_t *data, uint32_t len)
{
    (void) bus;
    (void) data;
    (void) len;

    if (!g_i3c_info.available) {
        return -1;
    }

    log_write("i3c: sending CCC command");

    /*
     * CCC commands are sent to broadcast address 0x7E.
     * For now, we simulate CCC support since we're in I2C compatibility mode.
     */
    switch (ccc) {
    case I3C_CCC_ENEC:
        /* Enable Events Command - simulate success */
        strcpy(g_i3c_info.status, "i3c: ENEC sent");
        return 0;

    case I3C_CCC_DISEC:
        /* Disable Events Command - simulate success */
        strcpy(g_i3c_info.status, "i3c: DISEC sent");
        return 0;

    case I3C_CCC_RSTDAA:
        /* Reset Dynamic Address Assignment - reset all dynamic addresses */
        for (uint8_t i = 0; i < g_i3c_device_count; i++) {
            g_i3c_devices[i].dynamic_addr = 0;
        }
        strcpy(g_i3c_info.status, "i3c: RSTDAA complete");
        log_write("i3c: dynamic addresses reset");
        return 0;

    case I3C_CCC_ENTDAA:
        /* Enter Dynamic Address Assignment - handled separately */
        strcpy(g_i3c_info.status, "i3c: ENTDAA requires DAA procedure");
        return -1;

    case I3C_CCC_SETMWL:
        /* Set Max Write Length - simulate */
        strcpy(g_i3c_info.status, "i3c: SETMWL sent");
        return 0;

    case I3C_CCC_SETMRL:
        /* Set Max Read Length - simulate */
        strcpy(g_i3c_info.status, "i3c: SETMRL sent");
        return 0;

    default:
        strcpy(g_i3c_info.status, "i3c: CCC not supported in compat mode");
        log_write("i3c: CCC not supported in compatibility mode");
        return -1;
    }
}

/* Perform DAA (Dynamic Address Assignment) */
int32_t i3c_do_daa(uint8_t bus, i3c_device_t *devices, uint32_t max_devices)
{
    (void) bus;
    uint32_t found = 0;

    if (!g_i3c_info.available || devices == NULL || max_devices == 0) {
        return -1;
    }

    log_write("i3c: starting DAA (Dynamic Address Assignment)...");

    /*
     * In real hardware, DAA is a complex procedure where each device
     * identifies itself by its 48-bit PID.
     *
     * For now, we scan the I2C bus and assign dynamic addresses
     * to any devices found (simulating I3C DAA in compat mode).
     */

    /* First send RSTDAA to reset */
    i3c_send_ccc(0, I3C_CCC_RSTDAA, NULL, 0);

    /* Scan for I2C devices and assign them as I3C devices */
    uint8_t i2c_addrs[128];
    uint32_t i2c_count = i2c_scan(0, i2c_addrs, 128);

    for (uint32_t i = 0; i < i2c_count && found < max_devices && found < I3C_MAX_DEVICES; i++) {
        i3c_device_t *dev = &g_i3c_devices[found];
        memset(dev, 0, sizeof(i3c_device_t));

        dev->static_addr = i2c_addrs[i];
        dev->dynamic_addr = (uint8_t)(0x08 + i * 2); /* Assign dynamic address */
        dev->is_i2c = true;
        dev->bcr = 0;
        dev->dcr = 0;
        dev->max_read_len = 256;
        dev->max_write_len = 256;
        dev->has_ibi = false;

        /* Generate a dummy PID */
        dev->pid_hi = 0x0000;
        dev->pid_lo = (uint32_t)(0x12340000 | i2c_addrs[i]);

        /* Copy to output buffer */
        memcpy(&devices[found], dev, sizeof(i3c_device_t));
        found++;
    }

    g_i3c_device_count = (uint8_t)found;
    g_i3c_info.device_count = (uint8_t)found;

    if (found > 0) {
        strcpy(g_i3c_info.status, "i3c: DAA complete, devices assigned");
        log_write("i3c: DAA complete - devices assigned dynamic addresses");
    } else {
        strcpy(g_i3c_info.status, "i3c: DAA complete, no devices");
        log_write("i3c: DAA complete - no devices found");
    }

    return (int32_t)found;
}

/* Scan I3C bus for devices */
uint32_t i3c_scan(uint8_t bus, uint8_t *buffer, uint32_t capacity)
{
    (void) bus;
    uint32_t count = 0;

    if (!g_i3c_info.available || buffer == NULL || capacity == 0) {
        return 0;
    }

    log_write("i3c: starting bus scan...");

    /* Scan using dynamic addresses if DAA was performed */
    for (uint8_t i = 0; i < g_i3c_device_count && count < capacity; i++) {
        if (g_i3c_devices[i].dynamic_addr != 0) {
            buffer[count++] = g_i3c_devices[i].dynamic_addr;
        }
    }

    /* If no dynamic devices, fall back to I2C scan */
    if (count == 0) {
        count = i2c_scan(0, buffer, capacity);
    }

    if (count > 0) {
        strcpy(g_i3c_info.status, "i3c: scan complete");
        log_write("i3c: scan complete - devices found");
    } else {
        strcpy(g_i3c_info.status, "i3c: scan empty");
    }

    return count;
}

/* Get device info by index */
int32_t i3c_get_device_info(uint8_t index, i3c_device_t *info)
{
    if (info == NULL || index >= g_i3c_device_count) {
        return -1;
    }

    memcpy(info, &g_i3c_devices[index], sizeof(i3c_device_t));
    return 0;
}

/* Set I3C bus frequency */
int32_t i3c_set_frequency(uint8_t bus, uint32_t freq_hz)
{
    (void) bus;

    if (!g_i3c_info.available) {
        return -1;
    }

    if (freq_hz == 0 || freq_hz > 12500000) { /* Max 12.5 MHz SDR */
        strcpy(g_i3c_info.status, "i3c: invalid frequency");
        return -1;
    }

    g_i3c_info.scl_freq = freq_hz;
    strcpy(g_i3c_info.status, "i3c: frequency updated");
    log_write("i3c: SCL frequency updated");
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
