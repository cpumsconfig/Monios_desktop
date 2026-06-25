#include "common.h"
#include "ide.h"
#include "kernel.h"

#define IDE_PRIMARY_IO       0x1F0
#define IDE_PRIMARY_CTRL     0x3F6
#define IDE_REG_DATA         0
#define IDE_REG_ERROR        1
#define IDE_REG_SECCOUNT0    2
#define IDE_REG_LBA0         3
#define IDE_REG_LBA1         4
#define IDE_REG_LBA2         5
#define IDE_REG_HDDEVSEL     6
#define IDE_REG_COMMAND      7
#define IDE_STATUS_BSY       0x80
#define IDE_STATUS_DRQ       0x08
#define IDE_STATUS_ERR       0x01
#define IDE_CMD_IDENTIFY     0xEC

static ide_info_t g_ide_info;

static uint8_t ide_read8(uint16_t io_base, uint8_t reg)
{
    return inb((uint16_t) (io_base + reg));
}

static void ide_write8(uint16_t io_base, uint8_t reg, uint8_t value)
{
    outb((uint16_t) (io_base + reg), value);
}

static bool ide_wait_not_busy(uint16_t io_base)
{
    for (uint32_t i = 0; i < 100000; i++) {
        if ((ide_read8(io_base, IDE_REG_COMMAND) & IDE_STATUS_BSY) == 0) {
            return true;
        }
        io_wait();
    }
    return false;
}

static bool ide_wait_drq(uint16_t io_base)
{
    for (uint32_t i = 0; i < 100000; i++) {
        uint8_t status = ide_read8(io_base, IDE_REG_COMMAND);

        if ((status & IDE_STATUS_ERR) != 0) {
            return false;
        }
        if ((status & IDE_STATUS_BSY) == 0 && (status & IDE_STATUS_DRQ) != 0) {
            return true;
        }
        io_wait();
    }
    return false;
}

static void ide_decode_model(char out[41], const uint16_t identify[256])
{
    uint32_t pos = 0;

    for (uint32_t word = 27; word <= 46 && pos + 1 < 41; word++) {
        out[pos++] = (char) (identify[word] >> 8);
        out[pos++] = (char) identify[word];
    }
    while (pos > 0 && out[pos - 1] == ' ') {
        pos--;
    }
    out[pos] = '\0';
    if (out[0] == '\0') {
        strcpy(out, "ATA device");
    }
}

static bool ide_identify_primary_master(void)
{
    uint16_t identify[256];
    uint8_t status;

    memset(&g_ide_info, 0, sizeof(g_ide_info));
    g_ide_info.io_base = IDE_PRIMARY_IO;
    g_ide_info.control_base = IDE_PRIMARY_CTRL;
    g_ide_info.drive = 0xA0;
    strcpy(g_ide_info.status, "ide: probing primary master");

    ide_write8(IDE_PRIMARY_IO, IDE_REG_HDDEVSEL, 0xA0);
    for (uint32_t i = 0; i < 4; i++) {
        (void) inb(IDE_PRIMARY_CTRL);
    }
    status = ide_read8(IDE_PRIMARY_IO, IDE_REG_COMMAND);
    g_ide_info.status_reg = status;
    if (status == 0x00 || status == 0xFF) {
        strcpy(g_ide_info.status, "ide: no primary master");
        return false;
    }

    ide_write8(IDE_PRIMARY_IO, IDE_REG_SECCOUNT0, 0);
    ide_write8(IDE_PRIMARY_IO, IDE_REG_LBA0, 0);
    ide_write8(IDE_PRIMARY_IO, IDE_REG_LBA1, 0);
    ide_write8(IDE_PRIMARY_IO, IDE_REG_LBA2, 0);
    ide_write8(IDE_PRIMARY_IO, IDE_REG_COMMAND, IDE_CMD_IDENTIFY);
    if (!ide_wait_not_busy(IDE_PRIMARY_IO) || !ide_wait_drq(IDE_PRIMARY_IO)) {
        g_ide_info.status_reg = ide_read8(IDE_PRIMARY_IO, IDE_REG_COMMAND);
        strcpy(g_ide_info.status, "ide: identify failed");
        return false;
    }

    for (uint32_t i = 0; i < 256; i++) {
        identify[i] = inw(IDE_PRIMARY_IO + IDE_REG_DATA);
    }
    g_ide_info.present = true;
    g_ide_info.status_reg = ide_read8(IDE_PRIMARY_IO, IDE_REG_COMMAND);
    g_ide_info.sectors = ((uint32_t) identify[61] << 16) | identify[60];
    ide_decode_model(g_ide_info.model, identify);
    strcpy(g_ide_info.status, "ide: primary master ready");
    return true;
}

bool ide_driver_init(void)
{
    bool ready = ide_identify_primary_master();

    log_write(g_ide_info.status);
    return ready;
}

void ide_shutdown(void)
{
    if (g_ide_info.present) {
        strcpy(g_ide_info.status, "ide: shutdown");
        log_write(g_ide_info.status);
    }
}

const ide_info_t *ide_info(void)
{
    return &g_ide_info;
}

const char *ide_status(void)
{
    return g_ide_info.status;
}
