#ifndef _BIOS_H_
#define _BIOS_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool legacy_boot;
    uint16_t equipment_word;
    uint16_t conventional_kb;
    uint16_t ebda_segment;
    uint8_t com_ports;
    char status[64];
} bios_info_t;

void bios_init(void);
const bios_info_t *bios_info(void);
const char *bios_status(void);

#endif
