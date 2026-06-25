#ifndef _IDE_H_
#define _IDE_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool present;
    uint16_t io_base;
    uint16_t control_base;
    uint8_t drive;
    uint8_t status_reg;
    uint32_t sectors;
    char model[41];
    char status[64];
} ide_info_t;

bool ide_driver_init(void);
void ide_shutdown(void);
const ide_info_t *ide_info(void);
const char *ide_status(void);

#endif
