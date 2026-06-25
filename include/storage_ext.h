#ifndef _STORAGE_EXT_H_
#define _STORAGE_EXT_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool initialized;
    uint32_t ide_controllers;
    uint32_t sata_controllers;
    uint32_t nvme_controllers;
    uint32_t scsi_controllers;
    uint32_t raid_controllers;
    uint32_t other_storage;
    char status[64];
} storage_ext_info_t;

void storage_ext_init(void);
const storage_ext_info_t *storage_ext_info(void);
const char *storage_ext_status(void);

#endif
