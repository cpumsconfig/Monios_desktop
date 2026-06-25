#include "rtc.h"
#include "common.h"

static char g_rtc_status[64];

static void rtc_write_2dec(char *dst, uint32_t value)
{
    dst[0] = (char) ('0' + ((value / 10u) % 10u));
    dst[1] = (char) ('0' + (value % 10u));
}

void rtc_init(void)
{
    strcpy(g_rtc_status, "rtc: cmos bridge online");
}

void rtc_read_time(rtc_time_t *out_time)
{
    cmos_read_time(out_time);
}

bool rtc_format_time(char *buffer, uint32_t buffer_size)
{
    rtc_time_t time;

    if (buffer == NULL || buffer_size < 20) {
        return false;
    }
    rtc_read_time(&time);
    buffer[0] = (char) ('0' + ((time.year / 1000u) % 10u));
    buffer[1] = (char) ('0' + ((time.year / 100u) % 10u));
    buffer[2] = (char) ('0' + ((time.year / 10u) % 10u));
    buffer[3] = (char) ('0' + (time.year % 10u));
    buffer[4] = '-';
    rtc_write_2dec(&buffer[5], time.month);
    buffer[7] = '-';
    rtc_write_2dec(&buffer[8], time.day);
    buffer[10] = ' ';
    rtc_write_2dec(&buffer[11], time.hour);
    buffer[13] = ':';
    rtc_write_2dec(&buffer[14], time.minute);
    buffer[16] = ':';
    rtc_write_2dec(&buffer[17], time.second);
    buffer[19] = '\0';
    return true;
}

const char *rtc_status(void)
{
    return g_rtc_status;
}
