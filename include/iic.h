#ifndef _IIC_H_
#define _IIC_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool available;
    bool intel_style;
    uint8_t adapters;
    uint8_t found_count;
    uint8_t last_probe;
    char status[64];
} iic_info_t;

void iic_init(void);
bool iic_probe(uint8_t address);
uint32_t iic_scan(uint8_t *buffer, uint32_t capacity);
const iic_info_t *iic_info(void);
const char *iic_status(void);

#endif
