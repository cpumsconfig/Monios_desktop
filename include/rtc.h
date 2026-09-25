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

/* --- Feature 12: NTP time sync / timezone --- */
bool rtc_ntp_sync(const char *server);          /* NULL -> pool.ntp.org */
bool rtc_set_system_time(const rtc_time_t *time); /* write back to CMOS */
void rtc_set_timezone(int8_t offset_hours);     /* e.g. 8 = UTC+8 */
int8_t rtc_timezone(void);
const char *rtc_ntp_status(void);

#endif
