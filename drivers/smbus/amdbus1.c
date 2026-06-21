
#include "common.h"
#include "kernel.h"
#include "pci.h"
#include "interrupt.h"

/* AMD SMBus Controller Specific Registers */
#define AMD_SMBUS_SMB_HOST_STS   0x00
#define AMD_SMBUS_SMB_HOST_CNT   0x02
#define AMD_SMBUS_SMB_HOST_CMD   0x03
#define AMD_SMBUS_SMB_HOST_ADDR  0x04
#define AMD_SMBUS_SMB_HOST_DATA0 0x05
#define AMD_SMBUS_SMB_HOST_DATA1 0x06
#define AMD_SMBUS_SMB_HOST_BLK   0x07
#define AMD_SMBUS_SMB_HOST_AUX   0x08
#define AMD_SMBUS_SMB_HOST_CRC   0x09

/* AMD Host Status Register Bits */
#define AMD_SMBUS_STS_DONE       0x01
#define AMD_SMBUS_STS_INUSE      0x02
#define AMD_SMBUS_STS_ALERT      0x04
#define AMD_SMBUS_STS_TIMEOUT    0x08
#define AMD_SMBUS_STS_ERROR      0x10
#define AMD_SMBUS_STS_BUSY       0x20
#define AMD_SMBUS_STS_PEC_ERR    0x40
#define AMD_SMBUS_STS_DEVERR     0x80

/* AMD Host Control Register Bits */
#define AMD_SMBUS_CNT_INT_EN     0x01
#define AMD_SMBUS_CNT_PEC_EN     0x02
#define AMD_SMBUS_CNT_BLK_EN     0x04
#define AMD_SMBUS_CNT_I2C_EN     0x08
#define AMD_SMBUS_CNT_SMB_EN     0x10
#define AMD_SMBUS_CNT_START      0x80

/* AMD Transaction Types */
#define AMD_SMBUS_QUICK          0x00
#define AMD_SMBUS_BYTE           0x20
#define AMD_SMBUS_BYTE_DATA      0x40
#define AMD_SMBUS_WORD_DATA      0x60
#define AMD_SMBUS_PROC_CALL      0x80
#define AMD_SMBUS_BLOCK_DATA     0xA0
#define AMD_SMBUS_I2C_BLOCK      0xC0

static uint16_t g_amd_smbus_io_base;
static bool g_amd_smbus_present;

bool amdbus1_probe(const pci_device_info_t *info)
{
    if (info == NULL) return false;
    if (info->vendor_id != 0x1022) return false;

    /* Get IO base address from BAR0 */
    uint32_t bar0 = pci_config_read32(info->bus, info->slot, info->func, 0x10);
    g_amd_smbus_io_base = bar0 & 0xFFFE;
    
    if (g_amd_smbus_io_base == 0) {
        log_write("smbus/amdbus1: invalid IO base address");
        return false;
    }

    /* Enable PCI IO space */
    uint16_t cmd = pci_config_read16(info->bus, info->slot, info->func, 0x04);
    pci_config_write16(info->bus, info->slot, info->func, 0x04, cmd | 0x01);

    /* Enable SMBus controller */
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_CNT, AMD_SMBUS_CNT_SMB_EN | AMD_SMBUS_CNT_I2C_EN);

    if (info->revision >= 0x10) {
        log_write("smbus/amdbus1: AMD modern SMBus controller detected");
    } else {
        log_write("smbus/amdbus1: AMD legacy SMBus controller detected");
    }
    
    g_amd_smbus_present = true;
    return true;
}

/* AMD SMBus wait for operation complete */
bool amdbus1_wait_ready(uint32_t timeout_ms)
{
    if (!g_amd_smbus_present || g_amd_smbus_io_base == 0) return false;
    
    uint64_t start = timer_ticks();
    while (timer_ticks() - start < timeout_ms) {
        uint8_t status = inb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_STS);
        if (!(status & AMD_SMBUS_STS_INUSE)) {
            return true;
        }
        io_wait();
    }
    return false;
}

/* AMD SMBus read byte */
bool amdbus1_read_byte(uint8_t addr, uint8_t cmd, uint8_t *data)
{
    if (!g_amd_smbus_present || g_amd_smbus_io_base == 0 || data == NULL) return false;

    if (!amdbus1_wait_ready(100)) {
        log_write("smbus/amdbus1: timeout waiting for ready");
        return false;
    }

    /* Clear status */
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_STS, AMD_SMBUS_STS_DONE | AMD_SMBUS_STS_ERROR | AMD_SMBUS_STS_PEC_ERR);
    
    /* Setup transaction */
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_ADDR, addr | 0x01);  /* Read */
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_CMD, cmd);
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_CNT, AMD_SMBUS_CNT_SMB_EN | AMD_SMBUS_CNT_I2C_EN | AMD_SMBUS_BYTE_DATA | AMD_SMBUS_CNT_START);

    if (!amdbus1_wait_ready(100)) {
        log_write("smbus/amdbus1: read timeout");
        return false;
    }

    uint8_t status = inb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_STS);
    if (status & (AMD_SMBUS_STS_ERROR | AMD_SMBUS_STS_PEC_ERR)) {
        log_write("smbus/amdbus1: read error");
        return false;
    }

    *data = inb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_DATA0);
    return true;
}

/* AMD SMBus write byte */
bool amdbus1_write_byte(uint8_t addr, uint8_t cmd, uint8_t data)
{
    if (!g_amd_smbus_present || g_amd_smbus_io_base == 0) return false;

    if (!amdbus1_wait_ready(100)) {
        log_write("smbus/amdbus1: timeout waiting for ready");
        return false;
    }

    /* Clear status */
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_STS, AMD_SMBUS_STS_DONE | AMD_SMBUS_STS_ERROR | AMD_SMBUS_STS_PEC_ERR);
    
    /* Setup transaction */
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_ADDR, addr);  /* Write */
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_CMD, cmd);
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_DATA0, data);
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_CNT, AMD_SMBUS_CNT_SMB_EN | AMD_SMBUS_CNT_I2C_EN | AMD_SMBUS_BYTE_DATA | AMD_SMBUS_CNT_START);

    if (!amdbus1_wait_ready(100)) {
        log_write("smbus/amdbus1: write timeout");
        return false;
    }

    uint8_t status = inb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_STS);
    if (status & (AMD_SMBUS_STS_ERROR | AMD_SMBUS_STS_PEC_ERR)) {
        log_write("smbus/amdbus1: write error");
        return false;
    }

    return true;
}

/* AMD SMBus read word */
bool amdbus1_read_word(uint8_t addr, uint8_t cmd, uint16_t *data)
{
    if (!g_amd_smbus_present || g_amd_smbus_io_base == 0 || data == NULL) return false;

    if (!amdbus1_wait_ready(100)) {
        log_write("smbus/amdbus1: timeout waiting for ready");
        return false;
    }

    /* Clear status */
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_STS, AMD_SMBUS_STS_DONE | AMD_SMBUS_STS_ERROR | AMD_SMBUS_STS_PEC_ERR);
    
    /* Setup transaction */
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_ADDR, addr | 0x01);  /* Read */
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_CMD, cmd);
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_CNT, AMD_SMBUS_CNT_SMB_EN | AMD_SMBUS_CNT_I2C_EN | AMD_SMBUS_WORD_DATA | AMD_SMBUS_CNT_START);

    if (!amdbus1_wait_ready(100)) {
        log_write("smbus/amdbus1: read timeout");
        return false;
    }

    uint8_t status = inb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_STS);
    if (status & (AMD_SMBUS_STS_ERROR | AMD_SMBUS_STS_PEC_ERR)) {
        log_write("smbus/amdbus1: read error");
        return false;
    }

    *data = (inb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_DATA1) << 8) | inb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_DATA0);
    return true;
}

/* AMD SMBus write word */
bool amdbus1_write_word(uint8_t addr, uint8_t cmd, uint16_t data)
{
    if (!g_amd_smbus_present || g_amd_smbus_io_base == 0) return false;

    if (!amdbus1_wait_ready(100)) {
        log_write("smbus/amdbus1: timeout waiting for ready");
        return false;
    }

    /* Clear status */
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_STS, AMD_SMBUS_STS_DONE | AMD_SMBUS_STS_ERROR | AMD_SMBUS_STS_PEC_ERR);
    
    /* Setup transaction */
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_ADDR, addr);  /* Write */
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_CMD, cmd);
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_DATA0, data & 0xFF);
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_DATA1, (data >> 8) & 0xFF);
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_CNT, AMD_SMBUS_CNT_SMB_EN | AMD_SMBUS_CNT_I2C_EN | AMD_SMBUS_WORD_DATA | AMD_SMBUS_CNT_START);

    if (!amdbus1_wait_ready(100)) {
        log_write("smbus/amdbus1: write timeout");
        return false;
    }

    uint8_t status = inb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_STS);
    if (status & (AMD_SMBUS_STS_ERROR | AMD_SMBUS_STS_PEC_ERR)) {
        log_write("smbus/amdbus1: write error");
        return false;
    }

    return true;
}

/* AMD SMBus quick command */
bool amdbus1_quick_command(uint8_t addr)
{
    if (!g_amd_smbus_present || g_amd_smbus_io_base == 0) return false;

    if (!amdbus1_wait_ready(100)) {
        log_write("smbus/amdbus1: timeout waiting for ready");
        return false;
    }

    /* Clear status */
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_STS, AMD_SMBUS_STS_DONE | AMD_SMBUS_STS_ERROR | AMD_SMBUS_STS_PEC_ERR);
    
    /* Setup transaction */
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_ADDR, addr);
    outb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_CNT, AMD_SMBUS_CNT_SMB_EN | AMD_SMBUS_CNT_I2C_EN | AMD_SMBUS_QUICK | AMD_SMBUS_CNT_START);

    if (!amdbus1_wait_ready(100)) {
        log_write("smbus/amdbus1: quick command timeout");
        return false;
    }

    uint8_t status = inb(g_amd_smbus_io_base + AMD_SMBUS_SMB_HOST_STS);
    if (status & (AMD_SMBUS_STS_ERROR | AMD_SMBUS_STS_PEC_ERR)) {
        log_write("smbus/amdbus1: quick command error");
        return false;
    }

    return true;
}

/* Get AMD SMBus IO base */
uint16_t amdbus1_get_io_base(void)
{
    return g_amd_smbus_io_base;
}

/* Check if AMD SMBus is present */
bool amdbus1_is_present(void)
{
    return g_amd_smbus_present;
}