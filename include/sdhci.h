#ifndef _SDHCI_H_
#define _SDHCI_H_

#include "stdbool.h"
#include "stdint.h"

/*
 * SDHCI (SD Host Controller Interface) driver.
 *
 * Targets a PCI SDHCI controller (base class 0x08, subclass 0x05) and brings
 * up an SD/SDHC card through the standard command sequence. Block I/O uses
 * PIO data transfers (simplified path) so no DMA engine is required.
 */

typedef struct {
    bool     present;
    bool     card_present;
    uint32_t block_size;       /* 512 */
    uint64_t capacity_sectors; /* from CSD */
    uint16_t rca;              /* relative card address */
    bool     sdhc;             /* high-capacity (block addressing) */
} sdhci_info_t;

bool sdhci_init(void);
bool sdhci_ready(void);
const sdhci_info_t *sdhci_info(void);

bool sdhci_read_sector(uint64_t lba, void *buffer);
bool sdhci_read_sectors(uint64_t lba, uint32_t count, void *buffer);
bool sdhci_write_sector(uint64_t lba, const void *buffer);
bool sdhci_write_sectors(uint64_t lba, uint32_t count, const void *buffer);

/* Hot-plug: returns true if a card change was noticed since last call. */
bool sdhci_detect_hotplug(void);

#endif
