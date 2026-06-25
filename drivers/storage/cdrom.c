#include "cdrom.h"
#include "common.h"
#include "string.h"

#define ATA_DATA_PORT         0x1F0
#define ATA_SECTOR_COUNT_PORT 0x1F2
#define ATA_LBA_LOW_PORT      0x1F3
#define ATA_LBA_MID_PORT      0x1F4
#define ATA_LBA_HIGH_PORT     0x1F5
#define ATA_DRIVE_PORT        0x1F6
#define ATA_COMMAND_PORT      0x1F7
#define ATA_STATUS_PORT       0x1F7

#define ATA_CMD_PACKET        0xA0
#define ATA_STATUS_BSY        0x80
#define ATA_STATUS_DRQ        0x08
#define ATA_STATUS_ERR        0x01
#define ATA_WAIT_LIMIT        1000000U
#define ATA_DRIVE_MASTER      0xA0
#define ATA_DRIVE_SLAVE       0xB0
#define CDROM_MAX_PACKET_SECTORS 16U

static cdrom_info_t g_cdrom_info;
static uint8_t g_atapi_packet[12];
static uint8_t g_cdrom_drive = ATA_DRIVE_MASTER;
static uint32_t g_cdrom_packet_bytes = CDROM_SECTOR_SIZE;

static bool cdrom_wait_not_busy(void)
{
    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; i++) {
        uint8_t status = inb(ATA_STATUS_PORT);

        if (status == 0xFF) {
            return false;
        }
        if ((status & ATA_STATUS_BSY) == 0) {
            return true;
        }
        io_wait();
    }
    return false;
}

static bool cdrom_wait_data_ready(void)
{
    if (!cdrom_wait_not_busy()) {
        return false;
    }
    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; i++) {
        uint8_t status = inb(ATA_STATUS_PORT);

        if (status == 0xFF) {
            return false;
        }
        if ((status & ATA_STATUS_ERR) != 0) {
            return false;
        }
        if ((status & ATA_STATUS_DRQ) != 0) {
            return true;
        }
        io_wait();
    }
    return false;
}

static bool cdrom_select_drive(uint8_t drive)
{
    outb(ATA_DRIVE_PORT, drive);
    for (uint32_t i = 0; i < 4; i++) {
        (void) inb(ATA_STATUS_PORT);
    }
    if (inb(ATA_STATUS_PORT) == 0x00 || inb(ATA_STATUS_PORT) == 0xFF) {
        return false;
    }
    return cdrom_wait_not_busy();
}

static bool cdrom_send_packet(const uint8_t *packet)
{
    uint16_t *pkt16 = (uint16_t *) packet;

    if (!cdrom_select_drive(g_cdrom_drive)) {
        return false;
    }
    outb(ATA_SECTOR_COUNT_PORT, 0);
    outb(ATA_LBA_LOW_PORT, 0);
    outb(ATA_LBA_MID_PORT, (uint8_t) (g_cdrom_packet_bytes & 0xFF));
    outb(ATA_LBA_HIGH_PORT, (uint8_t) ((g_cdrom_packet_bytes >> 8) & 0xFF));
    outb(ATA_COMMAND_PORT, ATA_CMD_PACKET);

    if (!cdrom_wait_data_ready()) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        outw(ATA_DATA_PORT, pkt16[i]);
    }
    return true;
}

static bool cdrom_read_data(void *buffer, uint32_t size)
{
    uint16_t *dst = (uint16_t *) buffer;
    uint32_t words = size / 2;

    if (!cdrom_wait_data_ready()) {
        return false;
    }
    for (uint32_t i = 0; i < words; i++) {
        dst[i] = inw(ATA_DATA_PORT);
    }
    return true;
}

static bool cdrom_test_unit_ready(void)
{
    memset(g_atapi_packet, 0, sizeof(g_atapi_packet));
    g_atapi_packet[0] = ATAPI_CMD_TEST_UNIT;
    g_cdrom_packet_bytes = CDROM_SECTOR_SIZE;

    if (!cdrom_send_packet(g_atapi_packet)) {
        return false;
    }
    if (!cdrom_wait_not_busy()) {
        return false;
    }
    return (inb(ATA_STATUS_PORT) & ATA_STATUS_ERR) == 0;
}

static bool cdrom_inquiry(void)
{
    uint8_t buffer[36];

    memset(g_atapi_packet, 0, sizeof(g_atapi_packet));
    g_atapi_packet[0] = ATAPI_CMD_INQUIRY;
    g_atapi_packet[4] = sizeof(buffer);
    g_cdrom_packet_bytes = sizeof(buffer);

    if (!cdrom_send_packet(g_atapi_packet)) {
        return false;
    }
    if (!cdrom_read_data(buffer, sizeof(buffer))) {
        return false;
    }
    for (int i = 0; i < 8; i++) {
        g_cdrom_info.vendor[i] = buffer[8 + i];
    }
    g_cdrom_info.vendor[8] = '\0';
    for (int i = 0; i < 16; i++) {
        g_cdrom_info.product[i] = buffer[16 + i];
    }
    g_cdrom_info.product[16] = '\0';
    for (int i = 0; i < 4; i++) {
        g_cdrom_info.revision[i] = buffer[32 + i];
    }
    g_cdrom_info.revision[4] = '\0';
    return true;
}

static bool cdrom_read_capacity(void)
{
    uint8_t buffer[8];

    memset(g_atapi_packet, 0, sizeof(g_atapi_packet));
    g_atapi_packet[0] = ATAPI_CMD_READ_CAP;
    g_cdrom_packet_bytes = sizeof(buffer);

    if (!cdrom_send_packet(g_atapi_packet)) {
        return false;
    }
    if (!cdrom_read_data(buffer, sizeof(buffer))) {
        return false;
    }
    g_cdrom_info.total_sectors = ((uint32_t) buffer[0] << 24) |
                                  ((uint32_t) buffer[1] << 16) |
                                  ((uint32_t) buffer[2] << 8) |
                                  ((uint32_t) buffer[3]);
    g_cdrom_info.total_sectors++;
    g_cdrom_info.sector_size = ((uint32_t) buffer[4] << 24) |
                                ((uint32_t) buffer[5] << 16) |
                                ((uint32_t) buffer[6] << 8) |
                                ((uint32_t) buffer[7]);
    return true;
}

bool cdrom_read_sector(uint32_t lba, void *buffer)
{
    return cdrom_read_sectors(lba, 1, buffer);
}

bool cdrom_read_sectors(uint32_t lba, uint32_t count, void *buffer)
{
    uint8_t *dst = (uint8_t *) buffer;

    if (!g_cdrom_info.present || !g_cdrom_info.ready || buffer == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < count;) {
        uint32_t packet_sectors = count - i;

        if (packet_sectors > CDROM_MAX_PACKET_SECTORS) {
            packet_sectors = CDROM_MAX_PACKET_SECTORS;
        }
        memset(g_atapi_packet, 0, sizeof(g_atapi_packet));
        g_atapi_packet[0] = ATAPI_CMD_READ;
        g_atapi_packet[2] = (uint8_t) ((lba + i) >> 24);
        g_atapi_packet[3] = (uint8_t) ((lba + i) >> 16);
        g_atapi_packet[4] = (uint8_t) ((lba + i) >> 8);
        g_atapi_packet[5] = (uint8_t) (lba + i);
        g_atapi_packet[6] = (uint8_t) (packet_sectors >> 24);
        g_atapi_packet[7] = (uint8_t) (packet_sectors >> 16);
        g_atapi_packet[8] = (uint8_t) (packet_sectors >> 8);
        g_atapi_packet[9] = (uint8_t) packet_sectors;
        g_cdrom_packet_bytes = packet_sectors * CDROM_SECTOR_SIZE;

        if (!cdrom_send_packet(g_atapi_packet)) {
            strcpy(g_cdrom_info.status, "cdrom: packet error");
            return false;
        }
        if (!cdrom_read_data(dst + i * CDROM_SECTOR_SIZE, g_cdrom_packet_bytes)) {
            strcpy(g_cdrom_info.status, "cdrom: read error");
            return false;
        }
        i += packet_sectors;
    }
    strcpy(g_cdrom_info.status, "cdrom: read ok");
    return true;
}

static bool cdrom_probe_drive(uint8_t drive)
{
    g_cdrom_drive = drive;
    g_cdrom_info.sector_size = CDROM_SECTOR_SIZE;

    if (!cdrom_test_unit_ready()) {
        return false;
    }
    g_cdrom_info.present = true;
    if (!cdrom_inquiry()) {
        strcpy(g_cdrom_info.status, "cdrom: inquiry failed");
        return false;
    }
    if (!cdrom_read_capacity()) {
        strcpy(g_cdrom_info.status, "cdrom: capacity failed");
        return false;
    }
    g_cdrom_info.ready = true;
    strcpy(g_cdrom_info.status,
           drive == ATA_DRIVE_MASTER ? "cdrom: primary master ready" : "cdrom: primary slave ready");
    return true;
}

void cdrom_init(void)
{
    memset(&g_cdrom_info, 0, sizeof(g_cdrom_info));
    g_cdrom_info.sector_size = CDROM_SECTOR_SIZE;
    strcpy(g_cdrom_info.status, "cdrom: detecting...");

    if (cdrom_probe_drive(ATA_DRIVE_MASTER) || cdrom_probe_drive(ATA_DRIVE_SLAVE)) {
        return;
    }
    memset(&g_cdrom_info, 0, sizeof(g_cdrom_info));
    g_cdrom_info.sector_size = CDROM_SECTOR_SIZE;
    strcpy(g_cdrom_info.status, "cdrom: not found");
}

bool cdrom_is_present(void)
{
    return g_cdrom_info.present;
}

bool cdrom_is_ready(void)
{
    return g_cdrom_info.ready;
}

uint32_t cdrom_total_sectors(void)
{
    return g_cdrom_info.total_sectors;
}

const cdrom_info_t *cdrom_info(void)
{
    return &g_cdrom_info;
}

const char *cdrom_status(void)
{
    return g_cdrom_info.status;
}
