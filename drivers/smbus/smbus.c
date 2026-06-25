#include "common.h"
#include "kernel.h"
#include "pci.h"
#include "smbus.h"
#include "interrupt.h"

/* SMBus Controller Registers */
#define SMBUS_HOST_STATUS_REG    0x00
#define SMBUS_HOST_CONTROL_REG   0x02
#define SMBUS_HOST_CMD_REG       0x03
#define SMBUS_HOST_ADDR_REG      0x04
#define SMBUS_HOST_DATA_REG      0x05
#define SMBUS_HOST_BLOCK_DATA    0x06
#define SMBUS_HOST_BLOCK_COUNT   0x07

/* Host Status Register Bits */
#define SMBUS_STATUS_ERROR       0x01
#define SMBUS_STATUS_INUSE       0x02
#define SMBUS_STATUS_SMBALERT    0x04
#define SMBUS_STATUS_TIMEOUT     0x08
#define SMBUS_STATUS_DONE        0x10

/* Host Control Register Bits */
#define SMBUS_CTRL_INT_EN       0x01
#define SMBUS_CTRL_PEC_EN       0x02
#define SMBUS_CTRL_BLK_EN       0x04
#define SMBUS_CTRL_I2C_EN       0x08

/* Command Codes */
#define SMBUS_CMD_QUICK         0x00
#define SMBUS_CMD_BYTE          0x01
#define SMBUS_CMD_BYTE_DATA     0x02
#define SMBUS_CMD_WORD_DATA     0x03
#define SMBUS_CMD_PROC_CALL     0x04
#define SMBUS_CMD_BLOCK_DATA    0x05
#define SMBUS_CMD_I2C_BLOCK     0x06

static bool g_smbus_present;
static char g_smbus_status[64];
static uint16_t g_smbus_io_base;
static bool g_is_intel;

/* AMD legacy probe */
bool amd_legacy_probe(void)
{
    pci_device_info_t info;
    if (!pci_find_first(0x0C, 0x05, &info)) return false;
    if (info.vendor_id != 0x1022) return false;
    if (info.revision < 0x10) {
        log_write("smbus/amd_legacy: AMD legacy SMBus controller detected");
        return true;
    }
    return false;
}

/* AMD modern probe */
bool amd_modern_probe(void)
{
    pci_device_info_t info;
    if (!pci_find_first(0x0C, 0x05, &info)) return false;
    if (info.vendor_id != 0x1022) return false;
    if (info.revision >= 0x10) {
        log_write("smbus/amd_modern: AMD modern SMBus controller detected");
        return true;
    }
    return false;
}

/* Intel legacy probe */
bool intel_legacy_probe(void)
{
    pci_device_info_t info;
    if (!pci_find_first(0x0C, 0x05, &info)) return false;
    if (info.vendor_id != 0x8086) return false;
    if (info.revision < 0x10) {
        log_write("smbus/intel_legacy: Intel legacy SMBus controller detected");
        return true;
    }
    return false;
}

/* Intel modern probe */
bool intel_modern_probe(void)
{
    pci_device_info_t info;
    if (!pci_find_first(0x0C, 0x05, &info)) return false;
    if (info.vendor_id != 0x8086) return false;
    if (info.revision >= 0x10) {
        log_write("smbus/intel_modern: Intel modern SMBus controller detected");
        return true;
    }
    return false;
}

/* forward declarations for vendor probes (intel/amd) */
extern bool intelbus1_probe(const pci_device_info_t *info);
extern bool amdbus1_probe(const pci_device_info_t *info);

/* Wait for SMBus operation to complete */
static bool smbus_wait_ready(uint32_t timeout_ms)
{
    uint64_t start = timer_ticks();
    while (timer_ticks() - start < timeout_ms) {
        uint8_t status = inb(g_smbus_io_base + SMBUS_HOST_STATUS_REG);
        if (!(status & SMBUS_STATUS_INUSE)) {
            return true;
        }
        io_wait();
    }
    return false;
}

/* Clear SMBus status register */
static void smbus_clear_status(void)
{
    outb(g_smbus_io_base + SMBUS_HOST_STATUS_REG, SMBUS_STATUS_ERROR | SMBUS_STATUS_DONE);
}

/* Send SMBus command */
static bool smbus_send_command(uint8_t cmd, uint8_t addr, uint8_t data)
{
    if (!g_smbus_present || g_smbus_io_base == 0) return false;

    if (!smbus_wait_ready(100)) {
        log_write("smbus: timeout waiting for ready");
        return false;
    }

    smbus_clear_status();
    outb(g_smbus_io_base + SMBUS_HOST_ADDR_REG, addr);
    outb(g_smbus_io_base + SMBUS_HOST_CMD_REG, cmd);
    outb(g_smbus_io_base + SMBUS_HOST_DATA_REG, data);
    outb(g_smbus_io_base + SMBUS_HOST_CONTROL_REG, SMBUS_CMD_BYTE_DATA | SMBUS_CTRL_I2C_EN);

    if (!smbus_wait_ready(100)) {
        log_write("smbus: command timeout");
        return false;
    }

    uint8_t status = inb(g_smbus_io_base + SMBUS_HOST_STATUS_REG);
    if (status & SMBUS_STATUS_ERROR) {
        log_write("smbus: command error");
        return false;
    }

    return true;
}

/* Read byte from SMBus device */
bool smbus_read_byte(uint8_t addr, uint8_t cmd, uint8_t *data)
{
    if (!g_smbus_present || g_smbus_io_base == 0 || data == NULL) return false;

    if (!smbus_wait_ready(100)) {
        log_write("smbus: timeout waiting for ready");
        return false;
    }

    smbus_clear_status();
    outb(g_smbus_io_base + SMBUS_HOST_ADDR_REG, addr | 0x01);
    outb(g_smbus_io_base + SMBUS_HOST_CMD_REG, cmd);
    outb(g_smbus_io_base + SMBUS_HOST_CONTROL_REG, SMBUS_CMD_BYTE_DATA | SMBUS_CTRL_I2C_EN);

    if (!smbus_wait_ready(100)) {
        log_write("smbus: read timeout");
        return false;
    }

    uint8_t status = inb(g_smbus_io_base + SMBUS_HOST_STATUS_REG);
    if (status & SMBUS_STATUS_ERROR) {
        log_write("smbus: read error");
        return false;
    }

    *data = inb(g_smbus_io_base + SMBUS_HOST_DATA_REG);
    return true;
}

/* Write byte to SMBus device */
bool smbus_write_byte(uint8_t addr, uint8_t cmd, uint8_t data)
{
    return smbus_send_command(cmd, addr, data);
}

/* Read word from SMBus device */
bool smbus_read_word(uint8_t addr, uint8_t cmd, uint16_t *data)
{
    if (!g_smbus_present || g_smbus_io_base == 0 || data == NULL) return false;

    if (!smbus_wait_ready(100)) {
        log_write("smbus: timeout waiting for ready");
        return false;
    }

    smbus_clear_status();
    outb(g_smbus_io_base + SMBUS_HOST_ADDR_REG, addr | 0x01);
    outb(g_smbus_io_base + SMBUS_HOST_CMD_REG, cmd);
    outb(g_smbus_io_base + SMBUS_HOST_CONTROL_REG, SMBUS_CMD_WORD_DATA | SMBUS_CTRL_I2C_EN);

    if (!smbus_wait_ready(100)) {
        log_write("smbus: read timeout");
        return false;
    }

    uint8_t status = inb(g_smbus_io_base + SMBUS_HOST_STATUS_REG);
    if (status & SMBUS_STATUS_ERROR) {
        log_write("smbus: read error");
        return false;
    }

    *data = (inb(g_smbus_io_base + SMBUS_HOST_BLOCK_DATA) << 8) | 
            inb(g_smbus_io_base + SMBUS_HOST_DATA_REG);
    return true;
}

/* Write word to SMBus device */
bool smbus_write_word(uint8_t addr, uint8_t cmd, uint16_t data)
{
    if (!g_smbus_present || g_smbus_io_base == 0) return false;

    if (!smbus_wait_ready(100)) {
        log_write("smbus: timeout waiting for ready");
        return false;
    }

    smbus_clear_status();
    outb(g_smbus_io_base + SMBUS_HOST_ADDR_REG, addr);
    outb(g_smbus_io_base + SMBUS_HOST_CMD_REG, cmd);
    outb(g_smbus_io_base + SMBUS_HOST_DATA_REG, data & 0xFF);
    outb(g_smbus_io_base + SMBUS_HOST_BLOCK_DATA, (data >> 8) & 0xFF);
    outb(g_smbus_io_base + SMBUS_HOST_CONTROL_REG, SMBUS_CMD_WORD_DATA | SMBUS_CTRL_I2C_EN);

    if (!smbus_wait_ready(100)) {
        log_write("smbus: write timeout");
        return false;
    }

    uint8_t status = inb(g_smbus_io_base + SMBUS_HOST_STATUS_REG);
    if (status & SMBUS_STATUS_ERROR) {
        log_write("smbus: write error");
        return false;
    }

    return true;
}

/* Quick command (write only) */
bool smbus_quick_command(uint8_t addr)
{
    if (!g_smbus_present || g_smbus_io_base == 0) return false;

    if (!smbus_wait_ready(100)) {
        log_write("smbus: timeout waiting for ready");
        return false;
    }

    smbus_clear_status();
    outb(g_smbus_io_base + SMBUS_HOST_ADDR_REG, addr);
    outb(g_smbus_io_base + SMBUS_HOST_CONTROL_REG, SMBUS_CMD_QUICK | SMBUS_CTRL_I2C_EN);

    if (!smbus_wait_ready(100)) {
        log_write("smbus: quick command timeout");
        return false;
    }

    uint8_t status = inb(g_smbus_io_base + SMBUS_HOST_STATUS_REG);
    if (status & SMBUS_STATUS_ERROR) {
        log_write("smbus: quick command error");
        return false;
    }

    return true;
}

/* Probe for SMBus device */
bool smbus_probe_device(uint8_t addr)
{
    return smbus_quick_command(addr);
}

/* Get controller type */
bool smbus_is_intel(void)
{
    return g_is_intel;
}

bool smbus_driver_init(void)
{
    pci_device_info_t info;
    bool found = pci_find_first(0x0C, 0x05, &info);

    if (!found) {
        strcpy(g_smbus_status, "smbus: no controller detected");
        g_smbus_present = false;
        g_smbus_io_base = 0;
        log_write(g_smbus_status);
        return true;
    }

    /* Get SMBus IO base address from BAR0 */
    uint32_t bar0 = pci_config_read32(info.bus, info.slot, info.func, 0x10);
    g_smbus_io_base = bar0 & 0xFFFE;  /* Mask out the enable bit */
    
    if (g_smbus_io_base == 0) {
        strcpy(g_smbus_status, "smbus: invalid IO base address");
        g_smbus_present = false;
        log_write(g_smbus_status);
        return true;
    }

    /* Enable IO space in PCI command register */
    uint16_t cmd = pci_config_read16(info.bus, info.slot, info.func, 0x04);
    pci_config_write16(info.bus, info.slot, info.func, 0x04, cmd | 0x01);

    /* call vendor-specific probes based on vendor id */
    if (info.vendor_id == 0x8086) {
        g_is_intel = true;
        if (intelbus1_probe(&info)) {
            strcpy(g_smbus_status, "smbus: intelbus1 detected");
            g_smbus_present = true;
            log_write(g_smbus_status);
            return true;
        }
    } else if (info.vendor_id == 0x1022) {
        g_is_intel = false;
        if (amdbus1_probe(&info)) {
            strcpy(g_smbus_status, "smbus: amdbus1 detected");
            g_smbus_present = true;
            log_write(g_smbus_status);
            return true;
        }
    }

    /* fallback: generic PCI SMBus controller found */
    strcpy(g_smbus_status, "smbus: generic pci controller detected");
    g_smbus_present = true;
    log_write(g_smbus_status);
    return true;
}

void smbus_init(void)
{
}

void smbus_driver_shutdown(void)
{
    if (g_smbus_present) {
        strcpy(g_smbus_status, "smbus: shutdown");
        log_write(g_smbus_status);
    }
    g_smbus_present = false;
}

bool smbus_available(void)
{
    return g_smbus_present;
}

const char *smbus_status(void)
{
    return g_smbus_status;
}