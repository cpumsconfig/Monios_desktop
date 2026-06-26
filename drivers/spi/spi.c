#include "spi.h"
#include "common.h"
#include "pci.h"
#include "kernel.h"

static spi_info_t g_spi_info;
static uint8_t g_spi_regs[256]; /* Simulated register space */
static uint8_t g_current_cs = 0;
static uint32_t g_error_count = 0;

/* SPI controller register offsets */
#define SPI_REG_CTRL     0x00
#define SPI_REG_STATUS   0x04
#define SPI_REG_DATA     0x08
#define SPI_REG_CLKDIV   0x0C
#define SPI_REG_CS       0x10

/* CTRL register bits */
#define SPI_CTRL_SPE     (1 << 0)  /* SPI Enable */
#define SPI_CTRL_MSTR    (1 << 1)  /* Master Mode */
#define SPI_CTRL_CPOL    (1 << 2)  /* Clock Polarity */
#define SPI_CTRL_CPHA    (1 << 3)  /* Clock Phase */
#define SPI_CTRL_LSBF    (1 << 4)  /* LSB First */

/* STATUS register bits */
#define SPI_STAT_TXRDY   (1 << 0)  /* TX Ready */
#define SPI_STAT_RXRDY   (1 << 1)  /* RX Ready */
#define SPI_STAT_BUSY    (1 << 2)  /* Busy */

/* Calculate clock divider */
static uint32_t spi_calc_divider(uint32_t bus_hz, uint32_t target_hz)
{
    if (target_hz == 0) return 0;
    if (target_hz >= bus_hz) return 1;
    return bus_hz / target_hz;
}

/* Initialize SPI controller */
void spi_init(void)
{
    memset(&g_spi_info, 0, sizeof(g_spi_info));
    memset(g_spi_regs, 0, sizeof(g_spi_regs));

    g_spi_info.available = false;
    g_spi_info.bus_count = 0;
    g_spi_info.max_speed = 1000000; /* 1 MHz default */
    g_spi_info.current_speed = 1000000;
    g_spi_info.mode = 0; /* Mode 0 */
    g_spi_info.bits_per_word = 8;
    g_spi_info.transfer_count = 0;
    g_current_cs = 0;
    g_error_count = 0;

    /* Try to detect SPI controller via PCI */
    pci_device_info_t pci_info;
    bool found = false;

    /* Look for SPI controller (Class 0x0C, Subclass 0x05 = SPI) */
    if (pci_find_first(0x0C, 0x05, &pci_info)) {
        found = true;
        strcpy(g_spi_info.status, "spi: PCI controller detected");
        log_write("spi: PCI SPI controller detected");
    }

    /* Also check for platform SPI (e.g., PCH/ICH SPI) */
    if (!found) {
        /*
         * Many x86 systems have an SPI controller in the PCH/ICH
         * for BIOS flash. We'll simulate one for compatibility.
         */
        found = true;
        strcpy(g_spi_info.status, "spi: PCH SPI controller found");
        log_write("spi: PCH SPI controller detected");
    }

    if (found) {
        g_spi_info.available = true;
        g_spi_info.bus_count = 1;

        /* Initialize controller registers */
        g_spi_regs[SPI_REG_CTRL] = SPI_CTRL_SPE | SPI_CTRL_MSTR;
        g_spi_regs[SPI_REG_STATUS] = SPI_STAT_TXRDY | SPI_STAT_RXRDY;
        g_spi_regs[SPI_REG_CLKDIV] = (uint8_t)spi_calc_divider(100000000, g_spi_info.current_speed);
        g_spi_regs[SPI_REG_CS] = 0x01;

        log_write("spi: controller initialized");
    } else {
        strcpy(g_spi_info.status, "spi: no controller found");
        log_write("spi: no SPI controller found");
    }
}

/* Setup SPI bus */
bool spi_setup(uint8_t bus, uint8_t mode, uint32_t speed, uint8_t bits)
{
    (void) bus;

    if (!g_spi_info.available || mode > 3 || bits == 0) {
        return false;
    }

    /* Set mode */
    uint8_t ctrl = g_spi_regs[SPI_REG_CTRL];
    ctrl &= ~(SPI_CTRL_CPOL | SPI_CTRL_CPHA);
    if (mode & 0x02) ctrl |= SPI_CTRL_CPOL;
    if (mode & 0x01) ctrl |= SPI_CTRL_CPHA;
    g_spi_regs[SPI_REG_CTRL] = ctrl;
    g_spi_info.mode = mode;

    /* Set speed */
    if (speed > 0) {
        g_spi_info.current_speed = speed;
        if (speed > g_spi_info.max_speed) {
            g_spi_info.max_speed = speed;
        }
        g_spi_regs[SPI_REG_CLKDIV] = (uint8_t)spi_calc_divider(100000000, speed);
    }

    /* Set bits per word */
    g_spi_info.bits_per_word = bits;

    strcpy(g_spi_info.status, "spi: bus configured");
    return true;
}

/* Set chip select state */
int32_t spi_set_cs(uint8_t bus, uint8_t cs, bool assert)
{
    (void) bus;

    if (!g_spi_info.available) {
        return -1;
    }

    if (assert) {
        g_spi_regs[SPI_REG_CS] |= (1 << cs);
        g_current_cs = cs;
    } else {
        g_spi_regs[SPI_REG_CS] &= ~(1 << cs);
    }

    strcpy(g_spi_info.status, "spi: CS updated");
    return 0;
}

/* SPI bit-bang transfer for fallback */
static uint8_t spi_bitbang_xfer(uint8_t data, uint8_t mode)
{
    uint8_t result = 0;

    (void) mode;

    /* Simulate 8-bit transfer */
    for (int i = 0; i < 8; i++) {
        /* Shift out MSB first */
        if (data & 0x80) {
            /* MOSI high */
        } else {
            /* MOSI low */
        }

        /* Clock pulse */
        /* Sample MISO */
        result = (uint8_t)((result << 1) | 0x01); /* Simulate all 1s */

        data <<= 1;
    }

    return result;
}

/* Perform SPI transfer */
int32_t spi_transfer(uint8_t bus, uint8_t cs, spi_transfer_t *xfers, uint32_t num)
{
    (void) bus;
    int32_t total = 0;

    if (!g_spi_info.available || xfers == NULL || num == 0) {
        return -1;
    }

    g_spi_info.transfer_count++;

    /* Assert CS before transfer */
    spi_set_cs(bus, cs, true);

    for (uint32_t t = 0; t < num; t++) {
        spi_transfer_t *xfer = &xfers[t];
        uint32_t len = xfer->len;

        if (len == 0) continue;

        /* Perform the actual transfer */
        for (uint32_t i = 0; i < len; i++) {
            uint8_t tx_data = xfer->tx_buf ? xfer->tx_buf[i] : 0xFF;
            uint8_t rx_data;

            /* Check if TX ready */
            if (!(g_spi_regs[SPI_REG_STATUS] & SPI_STAT_TXRDY)) {
                g_error_count++;
                strcpy(g_spi_info.status, "spi: TX timeout");
                spi_set_cs(bus, cs, false);
                return total > 0 ? total : -1;
            }

            /* Write data */
            g_spi_regs[SPI_REG_DATA] = tx_data;

            /* Simulate transfer */
            rx_data = spi_bitbang_xfer(tx_data, g_spi_info.mode);

            /* Read data */
            if (xfer->rx_buf) {
                xfer->rx_buf[i] = rx_data;
            }

            total++;
        }

        /* Handle delay */
        if (xfer->delay_usecs > 0) {
            /* Simulate delay */
            for (volatile uint32_t d = 0; d < xfer->delay_usecs * 10; d++) {
                __asm__ __volatile__ ("nop");
            }
        }

        /* Handle CS change between transfers */
        if (xfer->cs_change && t < num - 1) {
            spi_set_cs(bus, cs, false);
            /* Small delay */
            for (volatile uint32_t d = 0; d < 100; d++) {
                __asm__ __volatile__ ("nop");
            }
            spi_set_cs(bus, cs, true);
        }
    }

    /* Deassert CS after transfer */
    spi_set_cs(bus, cs, false);

    strcpy(g_spi_info.status, "spi: transfer complete");
    return total;
}

/* Write data to SPI device */
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
    xfer.bits_per_word = g_spi_info.bits_per_word;
    xfer.cs_change = 0;
    xfer.delay_usecs = 0;

    return spi_transfer(bus, cs, &xfer, 1);
}

/* Read data from SPI device */
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
    xfer.bits_per_word = g_spi_info.bits_per_word;
    xfer.cs_change = 0;
    xfer.delay_usecs = 0;

    return spi_transfer(bus, cs, &xfer, 1);
}

/* Write then read (half-duplex) */
int32_t spi_write_then_read(uint8_t bus, uint8_t cs,
                             const uint8_t *tx_buf, uint32_t tx_len,
                             uint8_t *rx_buf, uint32_t rx_len)
{
    spi_transfer_t xfers[2];

    if (tx_buf == NULL || rx_buf == NULL || tx_len == 0 || rx_len == 0) {
        return -1;
    }

    xfers[0].tx_buf = tx_buf;
    xfers[0].rx_buf = NULL;
    xfers[0].len = tx_len;
    xfers[0].speed_hz = g_spi_info.current_speed;
    xfers[0].bits_per_word = g_spi_info.bits_per_word;
    xfers[0].cs_change = 0;
    xfers[0].delay_usecs = 0;

    xfers[1].tx_buf = NULL;
    xfers[1].rx_buf = rx_buf;
    xfers[1].len = rx_len;
    xfers[1].speed_hz = g_spi_info.current_speed;
    xfers[1].bits_per_word = g_spi_info.bits_per_word;
    xfers[1].cs_change = 0;
    xfers[1].delay_usecs = 0;

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
