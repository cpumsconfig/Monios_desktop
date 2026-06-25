#ifndef _CDROM_H_
#define _CDROM_H_

#include "stdbool.h"
#include "stdint.h"

#define CDROM_SECTOR_SIZE    2048
#define CDROM_ATA_SECTORS    4

#define ATAPI_CMD_READ       0xA8
#define ATAPI_CMD_TEST_UNIT  0x00
#define ATAPI_CMD_INQUIRY    0x12
#define ATAPI_CMD_READ_CAP   0x25

typedef struct {
    bool present;
    bool ready;
    uint32_t total_sectors;
    uint32_t sector_size;
    char vendor[9];
    char product[17];
    char revision[5];
    char status[64];
} cdrom_info_t;

void cdrom_init(void);
bool cdrom_read_sector(uint32_t lba, void *buffer);
bool cdrom_read_sectors(uint32_t lba, uint32_t count, void *buffer);
bool cdrom_is_present(void);
bool cdrom_is_ready(void);
uint32_t cdrom_total_sectors(void);
const cdrom_info_t *cdrom_info(void);
const char *cdrom_status(void);

#endif
