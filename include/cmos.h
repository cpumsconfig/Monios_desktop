#ifndef _CMOS_H_
#define _CMOS_H_

#include "stdint.h"

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} cmos_time_t;

void cmos_read_time(cmos_time_t *out_time);
void cmos_log_time(void);

#endif
