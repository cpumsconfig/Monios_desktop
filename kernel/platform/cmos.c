#include "cmos.h"
#include "common.h"
#include "kernel.h"

#define CMOS_INDEX_PORT 0x70
#define CMOS_DATA_PORT  0x71
#define CMOS_LOCAL_TIME_OFFSET_HOURS 8u

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

static bool cmos_is_leap_year(uint16_t year)
{
    return (year % 4u == 0u && year % 100u != 0u) || year % 400u == 0u;
}

static uint8_t cmos_days_in_month(uint16_t year, uint8_t month)
{
    switch (month) {
    case 1:
    case 3:
    case 5:
    case 7:
    case 8:
    case 10:
    case 12:
        return 31;
    case 4:
    case 6:
    case 9:
    case 11:
        return 30;
    case 2:
        return cmos_is_leap_year(year) ? 29 : 28;
    default:
        return 0;
    }
}

static void cmos_add_hours(cmos_time_t *time, uint8_t hours)
{
    uint8_t days_in_month;

    if (time == NULL) {
        return;
    }

    while (hours-- > 0u) {
        time->hour++;
        if (time->hour < 24u) {
            continue;
        }

        time->hour = 0;
        days_in_month = cmos_days_in_month(time->year, time->month);
        if (days_in_month == 0u || time->day == 0u || time->day > days_in_month) {
            return;
        }

        time->day++;
        if (time->day <= days_in_month) {
            continue;
        }

        time->day = 1;
        time->month++;
        if (time->month <= 12u) {
            continue;
        }

        time->month = 1;
        time->year++;
    }
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
    cmos_add_hours(out_time, CMOS_LOCAL_TIME_OFFSET_HOURS);
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

static uint8_t cmos_dec_to_bcd(uint8_t value)
{
    return (uint8_t) (((value / 10u) << 4) | (value % 10u));
}

static void cmos_write_reg(uint8_t index, uint8_t value)
{
    outb(CMOS_INDEX_PORT, index);
    outb(CMOS_DATA_PORT, value);
}

/* Write a broken-down time back into the CMOS RTC. Feature 12 (NTP sync). */
void cmos_write_time(const cmos_time_t *in_time)
{
    uint8_t bcd;
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t century;

    if (in_time == NULL) {
        return;
    }
    while (cmos_update_in_progress()) {
    }
    bcd = (cmos_read(0x0B) & 0x04u) == 0;

    second = in_time->second;
    minute = in_time->minute;
    hour = in_time->hour;
    day = in_time->day;
    month = in_time->month;
    year = (uint8_t) (in_time->year % 100u);
    century = (uint8_t) (in_time->year / 100u);

    if (bcd) {
        second = cmos_dec_to_bcd(second);
        minute = cmos_dec_to_bcd(minute);
        hour = cmos_dec_to_bcd(hour);
        day = cmos_dec_to_bcd(day);
        month = cmos_dec_to_bcd(month);
        year = cmos_dec_to_bcd(year);
        century = cmos_dec_to_bcd(century);
    }
    cmos_write_reg(0x00, second);
    cmos_write_reg(0x02, minute);
    cmos_write_reg(0x04, hour);
    cmos_write_reg(0x07, day);
    cmos_write_reg(0x08, month);
    cmos_write_reg(0x09, year);
    cmos_write_reg(0x32, century);
}
