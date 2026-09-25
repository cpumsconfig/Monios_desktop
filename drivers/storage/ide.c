#include "common.h"
#include "ide.h"
#include "kernel.h"

#define IDE_PRIMARY_IO       0x1F0
#define IDE_PRIMARY_CTRL     0x3F6
#define IDE_SECONDARY_IO     0x170
#define IDE_SECONDARY_CTRL   0x376
#define IDE_PRIMARY_BM       0x1F0
#define IDE_SECONDARY_BM     0x170

#define IDE_REG_DATA         0
#define IDE_REG_ERROR        1
#define IDE_REG_FEATURES     1
#define IDE_REG_SECCOUNT0    2
#define IDE_REG_SECCOUNT1    4
#define IDE_REG_LBA0         3
#define IDE_REG_LBA1         4
#define IDE_REG_LBA2         5
#define IDE_REG_HDDEVSEL     6
#define IDE_REG_COMMAND      7
#define IDE_REG_STATUS       7

#define IDE_STATUS_BSY       0x80
#define IDE_STATUS_DRDY      0x40
#define IDE_STATUS_DRQ       0x08
#define IDE_STATUS_ERR       0x01

#define IDE_CMD_IDENTIFY     0xEC
#define IDE_CMD_READ_PIO     0x20
#define IDE_CMD_READ_PIO_EXT 0x24
#define IDE_CMD_WRITE_PIO    0x30
#define IDE_CMD_WRITE_PIO_EXT 0x34
#define IDE_CMD_CACHE_FLUSH  0xE7

#define IDE_DRIVE_MASTER     0xA0
#define IDE_DRIVE_SLAVE      0xB0

/* Bus Master IDE register offsets (within the channel BM block) */
#define IDE_BM_COMMAND       0x00
#define IDE_BM_STATUS        0x02
#define IDE_BM_PRDT          0x04
#define IDE_BM_START         0x01
#define IDE_BM_WRITE         0x08
#define IDE_BM_SIMPLEX       0x04

#define IDE_SECTOR_SIZE      512U
#define IDE_WAIT_LIMIT       1000000U

static ide_info_t g_ide_info;
static bool g_ide_dma_enabled = false; /* BM registers programmed; data moves via PIO fallback */

static ide_device_t *ide_active_dev(void)
{
    if (g_ide_info.active >= IDE_MAX_DEVICES) {
        return NULL;
    }
    return &g_ide_info.devices[g_ide_info.active];
}

/* Mirror the active device into the legacy top-level fields used by
 * installer/shell/device listings. */
static void ide_sync_legacy(void)
{
    ide_device_t *d = ide_active_dev();

    g_ide_info.present = g_ide_info.device_count > 0;
    if (d == NULL || !d->present) {
        g_ide_info.io_base = 0;
        g_ide_info.control_base = 0;
        g_ide_info.drive = 0;
        g_ide_info.status_reg = 0;
        g_ide_info.sectors = 0;
        g_ide_info.sectors64 = 0;
        g_ide_info.model[0] = '\0';
        return;
    }
    g_ide_info.io_base = d->io_base;
    g_ide_info.control_base = d->control_base;
    g_ide_info.drive = d->drive;
    g_ide_info.status_reg = d->status_reg;
    g_ide_info.sectors = (uint32_t) d->sectors;
    g_ide_info.sectors64 = d->sectors;
    strcpy(g_ide_info.model, d->model);
}

static uint8_t ide_read8(ide_device_t *d, uint8_t reg)
{
    return inb((uint16_t) (d->io_base + reg));
}

static void ide_write8(ide_device_t *d, uint8_t reg, uint8_t value)
{
    outb((uint16_t) (d->io_base + reg), value);
}

static void ide_delay_400ns(ide_device_t *d)
{
    for (uint32_t i = 0; i < 4; i++) {
        (void) inb(d->control_base);
    }
}

static bool ide_wait_not_busy(ide_device_t *d)
{
    for (uint32_t i = 0; i < IDE_WAIT_LIMIT; i++) {
        if ((ide_read8(d, IDE_REG_STATUS) & IDE_STATUS_BSY) == 0) {
            return true;
        }
        io_wait();
    }
    return false;
}

static bool ide_wait_drq(ide_device_t *d)
{
    for (uint32_t i = 0; i < IDE_WAIT_LIMIT; i++) {
        uint8_t status = ide_read8(d, IDE_REG_STATUS);

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

static void ide_select(ide_device_t *d)
{
    ide_write8(d, IDE_REG_HDDEVSEL, d->drive);
    ide_delay_400ns(d);
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

/* Bus Master IDE framework: program the BM command/status/PRDT registers.
 * Actual data transfer falls back to PIO below; this leaves the controller
 * ready for a future bounce-buffer DMA path. */
static void ide_bm_program(ide_device_t *d, uint32_t prdt_phys, bool writing)
{
    uint16_t bm = d->bus_master_base;
    uint8_t cmd;

    if (bm == 0) {
        return;
    }
    cmd = inb((uint16_t) (bm + IDE_BM_COMMAND));
    cmd &= (uint8_t) ~IDE_BM_START;
    if (writing) {
        cmd |= IDE_BM_WRITE;
    } else {
        cmd &= (uint8_t) ~IDE_BM_WRITE;
    }
    outb((uint16_t) (bm + IDE_BM_COMMAND), cmd);
    outl((uint16_t) (bm + IDE_BM_PRDT), prdt_phys);
    /* clear interrupt / error / status bits */
    outb((uint16_t) (bm + IDE_BM_STATUS), inb((uint16_t) (bm + IDE_BM_STATUS)) | 0x17U);
}

static void ide_bm_start(ide_device_t *d)
{
    uint16_t bm = d->bus_master_base;

    if (bm == 0) {
        return;
    }
    outb((uint16_t) (bm + IDE_BM_COMMAND),
         inb((uint16_t) (bm + IDE_BM_COMMAND)) | IDE_BM_START);
    d->dma_ops++;
}

static void ide_bm_stop(ide_device_t *d)
{
    uint16_t bm = d->bus_master_base;

    if (bm == 0) {
        return;
    }
    outb((uint16_t) (bm + IDE_BM_COMMAND),
         inb((uint16_t) (bm + IDE_BM_COMMAND)) & (uint8_t) ~IDE_BM_START);
}

static bool ide_identify_drive(ide_device_t *d)
{
    uint16_t identify[256];
    uint8_t status;
    uint8_t channel = d->channel;
    uint8_t drive = d->drive;

    memset(d, 0, sizeof(*d));
    d->channel = channel;
    d->drive = drive;
    d->io_base = (channel == 0) ? IDE_PRIMARY_IO : IDE_SECONDARY_IO;
    d->control_base = (channel == 0) ? IDE_PRIMARY_CTRL : IDE_SECONDARY_CTRL;
    d->bus_master_base = (channel == 0) ? IDE_PRIMARY_BM : IDE_SECONDARY_BM;

    ide_select(d);
    status = ide_read8(d, IDE_REG_STATUS);
    if (status == 0x00 || status == 0xFF) {
        return false;
    }

    ide_write8(d, IDE_REG_SECCOUNT0, 0);
    ide_write8(d, IDE_REG_LBA0, 0);
    ide_write8(d, IDE_REG_LBA1, 0);
    ide_write8(d, IDE_REG_LBA2, 0);
    ide_write8(d, IDE_REG_COMMAND, IDE_CMD_IDENTIFY);
    status = ide_read8(d, IDE_REG_STATUS);
    if (status == 0x00 ||
        !ide_wait_not_busy(d) ||
        ide_read8(d, IDE_REG_LBA1) != 0 ||
        ide_read8(d, IDE_REG_LBA2) != 0 ||
        !ide_wait_drq(d)) {
        return false;
    }

    for (uint32_t i = 0; i < 256; i++) {
        identify[i] = inw((uint16_t) (d->io_base + IDE_REG_DATA));
    }

    d->present = true;
    d->status_reg = ide_read8(d, IDE_REG_STATUS);
    /* LBA48 support bit: word 83 bit 10 */
    d->lba48 = (identify[83] & (1U << 10)) != 0;
    if (d->lba48) {
        d->sectors = (uint64_t) identify[100] |
                     ((uint64_t) identify[101] << 16) |
                     ((uint64_t) identify[102] << 32) |
                     ((uint64_t) identify[103] << 48);
    } else {
        d->sectors = ((uint32_t) identify[61] << 16) | identify[60];
    }
    ide_decode_model(d->model, identify);
    strcpy(d->status, "ide: drive ready");
    return true;
}

static bool ide_do_read_pio(ide_device_t *d, uint64_t lba, uint32_t count, void *buffer)
{
    uint16_t *dst = (uint16_t *) buffer;
    uint32_t bytes_left = count * IDE_SECTOR_SIZE;
    uint16_t words_left = (uint16_t)(bytes_left / 2U);

    if (count == 0) {
        return false;
    }
    if (!ide_wait_not_busy(d)) {
        return false;
    }
    ide_select(d);

    if (d->lba48 && lba < (1ULL << 48)) {
        ide_write8(d, IDE_REG_FEATURES, 0);
        ide_write8(d, IDE_REG_SECCOUNT0, (uint8_t) (count >> 8));
        ide_write8(d, IDE_REG_LBA0, (uint8_t) (lba >> 24));
        ide_write8(d, IDE_REG_LBA1, (uint8_t) (lba >> 32));
        ide_write8(d, IDE_REG_LBA2, (uint8_t) (lba >> 40));
        ide_write8(d, IDE_REG_SECCOUNT0, (uint8_t) count);
        ide_write8(d, IDE_REG_LBA0, (uint8_t) lba);
        ide_write8(d, IDE_REG_LBA1, (uint8_t) (lba >> 8));
        ide_write8(d, IDE_REG_LBA2, (uint8_t) (lba >> 16));
        ide_write8(d, IDE_REG_HDDEVSEL, d->drive);
        ide_write8(d, IDE_REG_COMMAND, IDE_CMD_READ_PIO_EXT);
    } else {
        if (lba > 0x0FFFFFFFULL) {
            return false;
        }
        ide_write8(d, IDE_REG_FEATURES, 0);
        ide_write8(d, IDE_REG_SECCOUNT0, (uint8_t) count);
        ide_write8(d, IDE_REG_LBA0, (uint8_t) lba);
        ide_write8(d, IDE_REG_LBA1, (uint8_t) (lba >> 8));
        ide_write8(d, IDE_REG_LBA2, (uint8_t) (lba >> 16));
        ide_write8(d, IDE_REG_HDDEVSEL,
                   (uint8_t) (d->drive | ((lba >> 24) & 0x0FU)));
        ide_write8(d, IDE_REG_COMMAND, IDE_CMD_READ_PIO);
    }

    while (words_left > 0) {
        uint32_t chunk;

        if (!ide_wait_drq(d)) {
            return false;
        }
        chunk = 256U; /* words per sector */
        for (uint32_t i = 0; i < chunk; i++) {
            *dst++ = inw((uint16_t) (d->io_base + IDE_REG_DATA));
        }
        words_left = (uint16_t) (words_left - (uint16_t) chunk);
    }
    if (!ide_wait_not_busy(d)) {
        return false;
    }
    d->read_ops++;
    return true;
}

static bool ide_do_write_pio(ide_device_t *d, uint64_t lba, uint32_t count, const void *buffer)
{
    const uint16_t *src = (const uint16_t *) buffer;
    uint32_t words_left = count * (IDE_SECTOR_SIZE / 2U);

    if (count == 0) {
        return false;
    }
    if (!ide_wait_not_busy(d)) {
        return false;
    }
    ide_select(d);

    if (d->lba48 && lba < (1ULL << 48)) {
        ide_write8(d, IDE_REG_FEATURES, 0);
        ide_write8(d, IDE_REG_SECCOUNT0, (uint8_t) (count >> 8));
        ide_write8(d, IDE_REG_LBA0, (uint8_t) (lba >> 24));
        ide_write8(d, IDE_REG_LBA1, (uint8_t) (lba >> 32));
        ide_write8(d, IDE_REG_LBA2, (uint8_t) (lba >> 40));
        ide_write8(d, IDE_REG_SECCOUNT0, (uint8_t) count);
        ide_write8(d, IDE_REG_LBA0, (uint8_t) lba);
        ide_write8(d, IDE_REG_LBA1, (uint8_t) (lba >> 8));
        ide_write8(d, IDE_REG_LBA2, (uint8_t) (lba >> 16));
        ide_write8(d, IDE_REG_HDDEVSEL, d->drive);
        ide_write8(d, IDE_REG_COMMAND, IDE_CMD_WRITE_PIO_EXT);
    } else {
        if (lba > 0x0FFFFFFFULL) {
            return false;
        }
        ide_write8(d, IDE_REG_FEATURES, 0);
        ide_write8(d, IDE_REG_SECCOUNT0, (uint8_t) count);
        ide_write8(d, IDE_REG_LBA0, (uint8_t) lba);
        ide_write8(d, IDE_REG_LBA1, (uint8_t) (lba >> 8));
        ide_write8(d, IDE_REG_LBA2, (uint8_t) (lba >> 16));
        ide_write8(d, IDE_REG_HDDEVSEL,
                   (uint8_t) (d->drive | ((lba >> 24) & 0x0FU)));
        ide_write8(d, IDE_REG_COMMAND, IDE_CMD_WRITE_PIO);
    }

    while (words_left > 0) {
        if (!ide_wait_drq(d)) {
            return false;
        }
        for (uint32_t i = 0; i < 256U; i++) {
            outw((uint16_t) (d->io_base + IDE_REG_DATA), *src++);
        }
        words_left -= 256U;
    }
    if (!ide_wait_not_busy(d)) {
        return false;
    }
    /* flush write cache */
    ide_write8(d, IDE_REG_COMMAND, IDE_CMD_CACHE_FLUSH);
    (void) ide_wait_not_busy(d);
    d->write_ops++;
    return true;
}

bool ide_read_sectors_dev(uint8_t dev, uint64_t lba, uint32_t count, void *buffer)
{
    ide_device_t *d;

    if (dev >= IDE_MAX_DEVICES || buffer == NULL || count == 0) {
        return false;
    }
    d = &g_ide_info.devices[dev];
    if (!d->present) {
        return false;
    }
    if (d->sectors != 0 &&
        (lba >= d->sectors || count > d->sectors - lba)) {
        return false;
    }

    if (g_ide_dma_enabled) {
        /* framework: program BM registers, then fall back to PIO data path */
        ide_bm_program(d, 0, false);
        ide_bm_start(d);
    }

    if (!ide_do_read_pio(d, lba, count, buffer)) {
        strcpy(d->status, "ide: read error");
        ide_bm_stop(d);
        return false;
    }
    ide_bm_stop(d);
    strcpy(d->status, "ide: read ok");
    return true;
}

bool ide_write_sectors_dev(uint8_t dev, uint64_t lba, uint32_t count, const void *buffer)
{
    ide_device_t *d;

    if (dev >= IDE_MAX_DEVICES || buffer == NULL || count == 0) {
        return false;
    }
    d = &g_ide_info.devices[dev];
    if (!d->present) {
        return false;
    }
    if (d->sectors != 0 &&
        (lba >= d->sectors || count > d->sectors - lba)) {
        return false;
    }

    if (g_ide_dma_enabled) {
        ide_bm_program(d, 0, true);
        ide_bm_start(d);
    }

    if (!ide_do_write_pio(d, lba, count, buffer)) {
        strcpy(d->status, "ide: write error");
        ide_bm_stop(d);
        return false;
    }
    ide_bm_stop(d);
    strcpy(d->status, "ide: write ok");
    return true;
}

bool ide_read_sectors(uint64_t lba, uint32_t count, void *buffer)
{
    ide_device_t *d = ide_active_dev();

    if (d == NULL) {
        return false;
    }
    return ide_read_sectors_dev((uint8_t) (d - g_ide_info.devices), lba, count, buffer);
}

bool ide_write_sectors(uint64_t lba, uint32_t count, const void *buffer)
{
    ide_device_t *d = ide_active_dev();

    if (d == NULL) {
        return false;
    }
    return ide_write_sectors_dev((uint8_t) (d - g_ide_info.devices), lba, count, buffer);
}

bool ide_set_active(uint8_t dev)
{
    if (dev >= IDE_MAX_DEVICES || !g_ide_info.devices[dev].present) {
        return false;
    }
    g_ide_info.active = dev;
    ide_sync_legacy();
    return true;
}

uint32_t ide_probe(void)
{
    static const struct {
        uint8_t channel;
        uint8_t drive;
    } slots[IDE_MAX_DEVICES] = {
        {0, IDE_DRIVE_MASTER},
        {0, IDE_DRIVE_SLAVE},
        {1, IDE_DRIVE_MASTER},
        {1, IDE_DRIVE_SLAVE},
    };

    g_ide_info.device_count = 0;
    for (uint32_t i = 0; i < IDE_MAX_DEVICES; i++) {
        ide_device_t *d = &g_ide_info.devices[i];

        d->channel = slots[i].channel;
        d->drive = slots[i].drive;
        if (ide_identify_drive(d)) {
            g_ide_info.device_count++;
            if (g_ide_info.active >= IDE_MAX_DEVICES ||
                !g_ide_info.devices[g_ide_info.active].present) {
                g_ide_info.active = (uint8_t) i;
            }
        } else {
            memset(d, 0, sizeof(*d));
        }
    }
    g_ide_info.present = g_ide_info.device_count > 0;
    ide_sync_legacy();
    return (uint32_t) g_ide_info.device_count;
}

bool ide_driver_init(void)
{
    memset(&g_ide_info, 0, sizeof(g_ide_info));
    strcpy(g_ide_info.status, "ide: probing ATA disks");

    uint32_t count = ide_probe();

    if (count == 0) {
        strcpy(g_ide_info.status, "ide: not found");
        log_write(g_ide_info.status);
        return false;
    }
    strcpy(g_ide_info.status, "ide: ATA disks ready");
    log_write(g_ide_info.status);
    return true;
}

void ide_shutdown(void)
{
    if (g_ide_info.present) {
        g_ide_info.present = false;
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
