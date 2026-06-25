#include "cmos.h"
#include "common.h"
#include "kernel.h"

#define CMOS_INDEX_PORT 0x70
#define CMOS_DATA_PORT  0x71

static uint8_t cmos_read(uint8_t index)
{
    outb(CMOS_INDEX_PORT, index);
    return inb(CMOS_DATA_PORT);
}

static bool cmos_update_in_progress(void)
{
    return (cmos_read(0x0A) & 0x80u) != 0;
}

static uint8_t cmos_bcd_to_bin(uint8_t value)
{
    return (uint8_t) ((value & 0x0F) + ((value / 16u) * 10u));
}

void cmos_read_time(cmos_time_t *out_time)
{
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t century;
    bool bcd;

    if (out_time == NULL) {
        return;
    }

    while (cmos_update_in_progress()) {
    }

    second = cmos_read(0x00);
    minute = cmos_read(0x02);
    hour = cmos_read(0x04);
    day = cmos_read(0x07);
    month = cmos_read(0x08);
    year = cmos_read(0x09);
    century = cmos_read(0x32);
    bcd = (cmos_read(0x0B) & 0x04u) == 0;

    if (bcd) {
        second = cmos_bcd_to_bin(second);
        minute = cmos_bcd_to_bin(minute);
        hour = cmos_bcd_to_bin(hour);
        day = cmos_bcd_to_bin(day);
        month = cmos_bcd_to_bin(month);
        year = cmos_bcd_to_bin(year);
        century = cmos_bcd_to_bin(century);
    }

    out_time->second = second;
    out_time->minute = minute;
    out_time->hour = hour;
    out_time->day = day;
    out_time->month = month;
    out_time->year = (uint16_t) (century * 100u + year);
}

void cmos_log_time(void)
{
    cmos_time_t time;
    char line[64];

    cmos_read_time(&time);
    strcpy(line, "cmos: ");
    line[6] = (char) ('0' + (time.year / 1000) % 10);
    line[7] = (char) ('0' + (time.year / 100) % 10);
    line[8] = (char) ('0' + (time.year / 10) % 10);
    line[9] = (char) ('0' + time.year % 10);
    line[10] = '-';
    line[11] = (char) ('0' + time.month / 10);
    line[12] = (char) ('0' + time.month % 10);
    line[13] = '-';
    line[14] = (char) ('0' + time.day / 10);
    line[15] = (char) ('0' + time.day % 10);
    line[16] = ' ';
    line[17] = (char) ('0' + time.hour / 10);
    line[18] = (char) ('0' + time.hour % 10);
    line[19] = ':';
    line[20] = (char) ('0' + time.minute / 10);
    line[21] = (char) ('0' + time.minute % 10);
    line[22] = ':';
    line[23] = (char) ('0' + time.second / 10);
    line[24] = (char) ('0' + time.second % 10);
    line[25] = '\0';
    log_write(line);
}
