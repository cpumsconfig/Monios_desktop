
#include "common.h"
#include "kernel.h"
#include "pci.h"
#include "interrupt.h"

/* Intel SMBus Controller Specific Registers */
#define INTEL_SMBUS_HST_CNTL      0x00
#define INTEL_SMBUS_HST_STS       0x01
#define INTEL_SMBUS_HST_CMD       0x03
#define INTEL_SMBUS_XMIT_SLVA     0x04
#define INTEL_SMBUS_HST_D0        0x05
#define INTEL_SMBUS_HST_D1        0x06
#define INTEL_SMBUS_BLK_CNT       0x07
#define INTEL_SMBUS_AUX_STS       0x08
#define INTEL_SMBUS_AUX_CTL       0x09

/* Intel Host Control Register Bits */
#define INTEL_SMBUS_I2C_EN        0x01
#define INTEL_SMBUS_SMB_EN        0x02
#define INTEL_SMBUS_BLOCK_EN      0x04
#define INTEL_SMBUS_PEC_EN        0x08
#define INTEL_SMBUS_INT_EN        0x10
#define INTEL_SMBUS_KILL          0x20
#define INTEL_SMBUS_CYCLE         0x40
#define INTEL_SMBUS_START         0x80

/* Intel Host Status Register Bits */
#define INTEL_SMBUS_INUSE_STS     0x01
#define INTEL_SMBUS_ERROR_STS     0x02
#define INTEL_SMBUS_BUSY_STS      0x04
#define INTEL_SMBUS_SMBALERT_STS  0x08
#define INTEL_SMBUS_FAILED_STS    0x10
#define INTEL_SMBUS_DONE_STS      0x20
#define INTEL_SMBUS_TIMEOUT_STS   0x40

/* Intel Transaction Types */
#define INTEL_SMBUS_QUICK         0x00
#define INTEL_SMBUS_BYTE          0x01
#define INTEL_SMBUS_BYTE_DATA     0x02
#define INTEL_SMBUS_WORD_DATA     0x03
#define INTEL_SMBUS_PROC_CALL     0x04
#define INTEL_SMBUS_BLOCK_DATA    0x05

static uint16_t g_intel_smbus_io_base;
static bool g_intel_smbus_present;

bool intelbus1_probe(const pci_device_info_t *info)
{
    if (info == NULL) return false;
    if (info->vendor_id != 0x8086) return false;

    /* Get IO base address from BAR0 */
    uint32_t bar0 = pci_config_read32(info->bus, info->slot, info->func, 0x10);
    g_intel_smbus_io_base = bar0 & 0xFFFE;
    
    if (g_intel_smbus_io_base == 0) {
        log_write("smbus/intelbus1: invalid IO base address");
        return false;
    }

    /* Enable PCI IO space */
    uint16_t cmd = pci_config_read16(info->bus, info->slot, info->func, 0x04);
    pci_config_write16(info->bus, info->slot, info->func, 0x04, cmd | 0x01);

    /* Enable SMBus controller */
    outb(g_intel_smbus_io_base + INTEL_SMBUS_HST_CNTL, INTEL_SMBUS_SMB_EN | INTEL_SMBUS_I2C_EN);

    if (info->revision >= 0x10) {
        log_write("smbus/intelbus1: Intel modern SMBus controller detected");
    } else {
        log_write("smbus/intelbus1: Intel legacy SMBus controller detected");
    }
    
    g_intel_smbus_present = true;
    return true;
}

/* Intel SMBus wait for operation complete */
bool intelbus1_wait_ready(uint32_t timeout_ms)
{
    if (!g_intel_smbus_present || g_intel_smbus_io_base == 0) return false;
    
    uint64_t start = timer_ticks();
    while (timer_ticks() - start < timeout_ms) {
        uint8_t status = inb(g_intel_smbus_io_base + INTEL_SMBUS_HST_STS);
        if (!(status & INTEL_SMBUS_INUSE_STS)) {
            return true;
        }
        io_wait();
    }
    return false;
}

/* Intel SMBus read byte */
bool intelbus1_read_byte(uint8_t addr, uint8_t cmd, uint8_t *data)
{
    if (!g_intel_smbus_present || g_intel_smbus_io_base == 0 || data == NULL) return false;

    if (!intelbus1_wait_ready(100)) {
        log_write("smbus/intelbus1: timeout waiting for ready");
        return false;
    }

    /* Clear status */
    outb(g_intel_smbus_io_base + INTEL_SMBUS_HST_STS, INTEL_SMBUS_ERROR_STS | INTEL_SMBUS_FAILED_STS | INTEL_SMBUS_DONE_STS);
    
    /* Setup transaction */
    outb(g_intel_smbus_io_base + INTEL_SMBUS_XMIT_SLVA, addr | 0x01);  /* Read */
    outb(g_intel_smbus_io_base + INTEL_SMBUS_HST_CMD, cmd);
    outb(g_intel_smbus_io_base + INTEL_SMBUS_HST_CNTL, INTEL_SMBUS_SMB_EN | INTEL_SMBUS_I2C_EN | INTEL_SMBUS_BYTE_DATA | INTEL_SMBUS_START);

    if (!intelbus1_wait_ready(100)) {
        log_write("smbus/intelbus1: read timeout");
        return false;
    }

    uint8_t status = inb(g_intel_smbus_io_base + INTEL_SMBUS_HST_STS);
    if (status & (INTEL_SMBUS_ERROR_STS | INTEL_SMBUS_FAILED_STS)) {
        log_write("smbus/intelbus1: read error");
        return false;
    }

    *data = inb(g_intel_smbus_io_base + INTEL_SMBUS_HST_D0);
    return true;
}

/* Intel SMBus write byte */
bool intelbus1_write_byte(uint8_t addr, uint8_t cmd, uint8_t data)
{
    if (!g_intel_smbus_present || g_intel_smbus_io_base == 0) return false;

    if (!intelbus1_wait_ready(100)) {
        log_write("smbus/intelbus1: timeout waiting for ready");
        return false;
    }

    /* Clear status */
    outb(g_intel_smbus_io_base + INTEL_SMBUS_HST_STS, INTEL_SMBUS_ERROR_STS | INTEL_SMBUS_FAILED_STS | INTEL_SMBUS_DONE_STS);
    
    /* Setup transaction */
    outb(g_intel_smbus_io_base + INTEL_SMBUS_XMIT_SLVA, addr);  /* Write */
    outb(g_intel_smbus_io_base + INTEL_SMBUS_HST_CMD, cmd);
    outb(g_intel_smbus_io_base + INTEL_SMBUS_HST_D0, data);
    outb(g_intel_smbus_io_base + INTEL_SMBUS_HST_CNTL, INTEL_SMBUS_SMB_EN | INTEL_SMBUS_I2C_EN | INTEL_SMBUS_BYTE_DATA | INTEL_SMBUS_START);

    if (!intelbus1_wait_ready(100)) {
        log_write("smbus/intelbus1: write timeout");
        return false;
    }

    uint8_t status = inb(g_intel_smbus_io_base + INTEL_SMBUS_HST_STS);
    if (status & (INTEL_SMBUS_ERROR_STS | INTEL_SMBUS_FAILED_STS)) {
        log_write("smbus/intelbus1: write error");
        return false;
    }

    return true;
}

/* Intel SMBus read word */
bool intelbus1_read_word(uint8_t addr, uint8_t cmd, uint16_t *data)
{
    if (!g_intel_smbus_present || g_intel_smbus_io_base == 0 || data == NULL) return false;

    if (!intelbus1_wait_ready(100)) {
        log_write("smbus/intelbus1: timeout waiting for ready");
        return false;
    }

    /* Clear status */
    outb(g_intel_smbus_io_base + INTEL_SMBUS_HST_STS, INTEL_SMBUS_ERROR_STS | INTEL_SMBUS_FAILED_STS | INTEL_SMBUS_DONE_STS);
    
    /* Setup transaction */
    outb(g_intel_smbus_io_base + INTEL_SMBUS_XMIT_SLVA, addr | 0x01);  /* Read */
    outb(g_intel_smbus_io_base + INTEL_SMBUS_HST_CMD, cmd);
    outb(g_intel_smbus_io_base + INTEL_SMBUS_HST_CNTL, INTEL_SMBUS_SMB_EN | INTEL_SMBUS_I2C_EN | INTEL_SMBUS_WORD_DATA | INTEL_SMBUS_START);

    if (!intelbus1_wait_ready(100)) {
        log_write("smbus/intelbus1: read timeout");
        return false;
    }

    uint8_t status = inb(g_intel_smbus_io_base + INTEL_SMBUS_HST_STS);
    if (status & (INTEL_SMBUS_ERROR_STS | INTEL_SMBUS_FAILED_STS)) {
        log_write("smbus/intelbus1: read error");
        return false;
    }

    *data = (inb(g_intel_smbus_io_base + INTEL_SMBUS_HST_D1) << 8) | inb(g_intel_smbus_io_base + INTEL_SMBUS_HST_D0);
    return true;
}

/* Intel SMBus write word */
bool intelbus1_write_word(uint8_t addr, uint8_t cmd, uint16_t data)
{
    if (!g_intel_smbus_present || g_intel_smbus_io_base == 0) return false;

    if (!intelbus1_wait_ready(100)) {
        log_write("smbus/intelbus1: timeout waiting for ready");
        return false;
    }

    /* Clear status */
    outb(g_intel_smbus_io_base + INTEL_SMBUS_HST_STS, INTEL_SMBUS_ERROR_STS | INTEL_SMBUS_FAILED_STS | INTEL_SMBUS_DONE_STS);
    
    /* Setup transaction */
    outb(g_intel_smbus_io_base + INTEL_SMBUS_XMIT_SLVA, addr);  /* Write */
    outb(g_intel_smbus_io_base + INTEL_SMBUS_HST_CMD, cmd);
    outb(g_intel_smbus_io_base + INTEL_SMBUS_HST_D0, data & 0xFF);
    outb(g_intel_smbus_io_base + INTEL_SMBUS_HST_D1, (data >> 8) & 0xFF);
    outb(g_intel_smbus_io_base + INTEL_SMBUS_HST_CNTL, INTEL_SMBUS_SMB_EN | INTEL_SMBUS_I2C_EN | INTEL_SMBUS_WORD_DATA | INTEL_SMBUS_START);

    if (!intelbus1_wait_ready(100)) {
        log_write("smbus/intelbus1: write timeout");
        return false;
    }

    uint8_t status = inb(g_intel_smbus_io_base + INTEL_SMBUS_HST_STS);
    if (status & (INTEL_SMBUS_ERROR_STS | INTEL_SMBUS_FAILED_STS)) {
        log_write("smbus/intelbus1: write error");
        return false;
    }

    return true;
}

/* Intel SMBus quick command */
bool intelbus1_quick_command(uint8_t addr)
{
    if (!g_intel_smbus_present || g_intel_smbus_io_base == 0) return false;

    if (!intelbus1_wait_ready(100)) {
        log_write("smbus/intelbus1: timeout waiting for ready");
        return false;
    }

    /* Clear status */
    outb(g_intel_smbus_io_base + INTEL_SMBUS_HST_STS, INTEL_SMBUS_ERROR_STS | INTEL_SMBUS_FAILED_STS | INTEL_SMBUS_DONE_STS);
    
    /* Setup transaction */
    outb(g_intel_smbus_io_base + INTEL_SMBUS_XMIT_SLVA, addr);
    outb(g_intel_smbus_io_base + INTEL_SMBUS_HST_CNTL, INTEL_SMBUS_SMB_EN | INTEL_SMBUS_I2C_EN | INTEL_SMBUS_QUICK | INTEL_SMBUS_START);

    if (!intelbus1_wait_ready(100)) {
        log_write("smbus/intelbus1: quick command timeout");
        return false;
    }

    uint8_t status = inb(g_intel_smbus_io_base + INTEL_SMBUS_HST_STS);
    if (status & (INTEL_SMBUS_ERROR_STS | INTEL_SMBUS_FAILED_STS)) {
        log_write("smbus/intelbus1: quick command error");
        return false;
    }

    return true;
}

/* Get Intel SMBus IO base */
uint16_t intelbus1_get_io_base(void)
{
    return g_intel_smbus_io_base;
}

/* Check if Intel SMBus is present */
bool intelbus1_is_present(void)
{
    return g_intel_smbus_present;
}