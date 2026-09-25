#include "rtc.h"
#include "common.h"
#include "interrupt.h"
#include "kernel.h"
#include "socket.h"

static char g_rtc_status[64];
static char g_ntp_status[64];
static int8_t g_timezone_offset = 8; /* Monios ships on UTC+8 */

static void rtc_write_2dec(char *dst, uint32_t value)
{
    dst[0] = (char) ('0' + ((value / 10u) % 10u));
    dst[1] = (char) ('0' + (value % 10u));
}

void rtc_init(void)
{
    strcpy(g_rtc_status, "rtc: cmos bridge online");
    strcpy(g_ntp_status, "ntp: idle");
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

/* ---------------------------------------------------------------------- */
/* Feature 12: NTP client                                                 */
/* ---------------------------------------------------------------------- */

#define NTP_PACKET_SIZE   48U
#define NTP_PORT          123U
#define NTP_UNIX_EPOCH    2208988800ULL /* seconds between 1900 and 1970 */
#define NTP_DEFAULT_SERVER "pool.ntp.org"
#define NTP_RECV_TICKS_MAX 2000U

static uint32_t rtc_be32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) | (uint32_t) p[3];
}

static bool rtc_is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

/* Convert seconds since 1970-01-01 UTC into a broken-down time (UTC). */
static void rtc_seconds_to_civil(uint64_t total_seconds, cmos_time_t *out)
{
    uint32_t days = (uint32_t) (total_seconds / 86400ULL);
    uint32_t rem  = (uint32_t) (total_seconds % 86400ULL);
    uint32_t year = 1970;
    uint8_t month;
    static const uint8_t mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

    out->hour = (uint8_t) (rem / 3600u);
    rem %= 3600u;
    out->minute = (uint8_t) (rem / 60u);
    out->second = (uint8_t) (rem % 60u);

    while (1) {
        uint32_t year_days = rtc_is_leap((int) year) ? 366u : 365u;
        if (days < year_days) {
            break;
        }
        days -= year_days;
        year++;
    }
    month = 1;
    for (uint32_t i = 0; i < 12u; i++) {
        uint32_t dim = mdays[i];
        if (i == 1u && rtc_is_leap((int) year)) {
            dim = 29u;
        }
        if (days < dim) {
            break;
        }
        days -= dim;
        month++;
    }
    out->year = (uint16_t) year;
    out->month = month;
    out->day = (uint8_t) (days + 1u);
}

bool rtc_ntp_sync(const char *server)
{
    uint8_t pkt[NTP_PACKET_SIZE];
    uint8_t reply[NTP_PACKET_SIZE];
    int32_t handle;
    uint64_t start;
    uint32_t ntp_secs;
    uint64_t unix_secs;
    cmos_time_t time;

    if (server == NULL) {
        server = NTP_DEFAULT_SERVER;
    }

    handle = socket_udp_open(0);
    if (handle < 0) {
        strcpy(g_ntp_status, "ntp: udp open failed");
        return false;
    }

    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x1Bu; /* LI=0, VN=3, Mode=3 (client) */
    if (socket_sendto_ipv4(handle, server, NTP_PORT, pkt, sizeof(pkt)) < 0) {
        strcpy(g_ntp_status, "ntp: send failed");
        socket_close(handle);
        return false;
    }

    start = timer_ticks();
    for (;;) {
        int32_t n = socket_recvfrom_ipv4(handle, NULL, NULL, reply, sizeof(reply));
        if (n >= 48) {
            break;
        }
        if (timer_ticks() - start > NTP_RECV_TICKS_MAX) {
            strcpy(g_ntp_status, "ntp: recv timeout");
            socket_close(handle);
            return false;
        }
    }
    socket_close(handle);

    /* Transmit timestamp of server = bytes 40..43 (seconds since 1900). */
    ntp_secs = rtc_be32(&reply[40]);
    unix_secs = (uint64_t) ntp_secs - NTP_UNIX_EPOCH;
    /* CMOS hardware stores UTC here; cmos_read_time adds the local offset. */
    rtc_seconds_to_civil(unix_secs, &time);
    cmos_write_time(&time);

    strcpy(g_ntp_status, "ntp: synchronized");
    log_write("rtc: ntp synchronized");
    return true;
}

bool rtc_set_system_time(const rtc_time_t *time)
{
    if (time == NULL) {
        return false;
    }
    cmos_write_time(time);
    return true;
}

void rtc_set_timezone(int8_t offset_hours)
{
    g_timezone_offset = offset_hours;
}

int8_t rtc_timezone(void)
{
    return g_timezone_offset;
}

const char *rtc_ntp_status(void)
{
    return g_ntp_status;
}
