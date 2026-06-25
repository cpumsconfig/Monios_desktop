#ifndef _SPI_H_
#define _SPI_H_

#include "stdbool.h"
#include "stdint.h"

#define SPI_MODE_0            0x00
#define SPI_MODE_1            0x01
#define SPI_MODE_2            0x02
#define SPI_MODE_3            0x03

#define SPI_CPHA              0x01
#define SPI_CPOL              0x02

#define SPI_CS_HIGH           0x04
#define SPI_LSB_FIRST         0x08
#define SPI_3WIRE             0x10
#define SPI_LOOP              0x20
#define SPI_NO_CS             0x40
#define SPI_READY             0x80

#define SPI_MAX_BUSSES        4
#define SPI_MAX_CHIPSELECT    8

typedef struct {
    bool available;
    uint8_t bus_count;
    uint32_t max_speed;
    uint32_t current_speed;
    uint8_t mode;
    uint8_t bits_per_word;
    uint32_t transfer_count;
    char status[64];
} spi_info_t;

typedef struct {
    const uint8_t *tx_buf;
    uint8_t *rx_buf;
    uint32_t len;
    uint32_t speed_hz;
    uint16_t delay_usecs;
    uint8_t bits_per_word;
    uint8_t cs_change;
} spi_transfer_t;

void spi_init(void);
bool spi_setup(uint8_t bus, uint8_t mode, uint32_t speed, uint8_t bits);
int32_t spi_transfer(uint8_t bus, uint8_t cs, spi_transfer_t *xfers, uint32_t num);
int32_t spi_write(uint8_t bus, uint8_t cs, const uint8_t *buf, uint32_t len);
int32_t spi_read(uint8_t bus, uint8_t cs, uint8_t *buf, uint32_t len);
int32_t spi_write_then_read(uint8_t bus, uint8_t cs, const uint8_t *tx, uint32_t tx_len, uint8_t *rx, uint32_t rx_len);
const spi_info_t *spi_info(void);
const char *spi_status(void);

#endif
