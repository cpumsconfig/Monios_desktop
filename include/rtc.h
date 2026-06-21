#ifndef _RTC_H_
#define _RTC_H_

#include "cmos.h"
#include "stdbool.h"
#include "stdint.h"

typedef cmos_time_t rtc_time_t;

void rtc_init(void);
void rtc_read_time(rtc_time_t *out_time);
bool rtc_format_time(char *buffer, uint32_t buffer_size);
const char *rtc_status(void);

#endif
