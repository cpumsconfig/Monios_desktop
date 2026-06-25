#include "spi.h"
#include "common.h"
#include "pci.h"

static spi_info_t g_spi_info;

void spi_init(void)
{
    memset(&g_spi_info, 0, sizeof(g_spi_info));
    g_spi_info.available = false;
    g_spi_info.bus_count = 0;
    g_spi_info.max_speed = 50000000; /* 50 MHz typical max */
    g_spi_info.current_speed = 1000000; /* 1 MHz default */
    g_spi_info.mode = SPI_MODE_0;
    g_spi_info.bits_per_word = 8;
    strcpy(g_spi_info.status, "spi: controller not detected");
}

bool spi_setup(uint8_t bus, uint8_t mode, uint32_t speed, uint8_t bits)
{
    (void) bus;
    if (!g_spi_info.available) {
        strcpy(g_spi_info.status, "spi: controller unavailable");
        return false;
    }
    if (speed > g_spi_info.max_speed) {
        speed = g_spi_info.max_speed;
    }
    g_spi_info.mode = mode & 0x03;
    g_spi_info.current_speed = speed;
    g_spi_info.bits_per_word = bits ? bits : 8;
    strcpy(g_spi_info.status, "spi: setup complete");
    return true;
}

int32_t spi_transfer(uint8_t bus, uint8_t cs, spi_transfer_t *xfers, uint32_t num)
{
    (void) bus;
    (void) cs;
    (void) xfers;
    (void) num;
    if (!g_spi_info.available || xfers == NULL || num == 0) {
        return -1;
    }
    g_spi_info.transfer_count++;
    /* TODO: implement SPI transfer */
    strcpy(g_spi_info.status, "spi: transfer stub");
    return 0;
}

int32_t spi_write(uint8_t bus, uint8_t cs, const uint8_t *buf, uint32_t len)
{
    spi_transfer_t xfer;

    if (buf == NULL || len == 0) {
        return -1;
    }

    xfer.tx_buf = buf;
    xfer.rx_buf = NULL;
    xfer.len = len;
    xfer.speed_hz = g_spi_info.current_speed;
    xfer.delay_usecs = 0;
    xfer.bits_per_word = g_spi_info.bits_per_word;
    xfer.cs_change = 0;

    return spi_transfer(bus, cs, &xfer, 1);
}

int32_t spi_read(uint8_t bus, uint8_t cs, uint8_t *buf, uint32_t len)
{
    spi_transfer_t xfer;

    if (buf == NULL || len == 0) {
        return -1;
    }

    xfer.tx_buf = NULL;
    xfer.rx_buf = buf;
    xfer.len = len;
    xfer.speed_hz = g_spi_info.current_speed;
    xfer.delay_usecs = 0;
    xfer.bits_per_word = g_spi_info.bits_per_word;
    xfer.cs_change = 0;

    return spi_transfer(bus, cs, &xfer, 1);
}

int32_t spi_write_then_read(uint8_t bus, uint8_t cs, const uint8_t *tx, uint32_t tx_len, uint8_t *rx, uint32_t rx_len)
{
    spi_transfer_t xfers[2];

    if (tx == NULL || rx == NULL || tx_len == 0 || rx_len == 0) {
        return -1;
    }

    xfers[0].tx_buf = tx;
    xfers[0].rx_buf = NULL;
    xfers[0].len = tx_len;
    xfers[0].speed_hz = g_spi_info.current_speed;
    xfers[0].delay_usecs = 0;
    xfers[0].bits_per_word = g_spi_info.bits_per_word;
    xfers[0].cs_change = 0;

    xfers[1].tx_buf = NULL;
    xfers[1].rx_buf = rx;
    xfers[1].len = rx_len;
    xfers[1].speed_hz = g_spi_info.current_speed;
    xfers[1].delay_usecs = 0;
    xfers[1].bits_per_word = g_spi_info.bits_per_word;
    xfers[1].cs_change = 0;

    return spi_transfer(bus, cs, xfers, 2);
}

const spi_info_t *spi_info(void)
{
    return &g_spi_info;
}

const char *spi_status(void)
{
    return g_spi_info.status;
}
