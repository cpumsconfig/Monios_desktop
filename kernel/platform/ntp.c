/*
 * ntp.c - Network Time Protocol client (Feature 12)
 *
 * Talks to an NTP server over UDP/123 using the project socket layer
 * (include/socket.h, kernel/net/socket.c). The default pool is
 * pool.ntp.org. On a successful response the 64-bit transmit timestamp is
 * decoded and pushed into the CMOS RTC via rtc.c.
 *
 * This file is owned by the system-services group. The integrated, callable
 * entry point used by the rest of the kernel lives in rtc.c (rtc_ntp_sync);
 * this module holds the packet layout, server pool and request framing and is
 * wired into the build by adding ntp.o to KERNEL_PLATFORM_OBJS.
 */

#include "rtc.h"
#include "socket.h"
#include "string.h"

#define NTP_LEAP_NO_WARNING 0u
#define NTP_VERSION_3       3u
#define NTP_MODE_CLIENT     3u
#define NTP_PORT            123u
#define NTP_PACKET_SIZE     48u
#define NTP_UNIX_EPOCH      2208988800ULL

typedef struct {
    uint8_t  flags;       /* LI | VN | Mode */
    uint8_t  stratum;
    int8_t   poll;
    int8_t   precision;
    uint32_t root_delay;
    uint32_t root_dispersion;
    uint32_t reference_id;
    uint32_t ref_ts_sec;
    uint32_t ref_ts_frac;
    uint32_t orig_ts_sec;
    uint32_t orig_ts_frac;
    uint32_t rx_ts_sec;
    uint32_t rx_ts_frac;
    uint32_t tx_ts_sec;   /* bytes 40..43: server transmit time */
    uint32_t tx_ts_frac;
} __attribute__((packed)) ntp_packet_t;

static const char *g_ntp_servers[] = {
    "pool.ntp.org",
    "cn.ntp.org.cn",
    "time.windows.com"
};

#define NTP_SERVER_COUNT (sizeof(g_ntp_servers) / sizeof(g_ntp_servers[0]))

static void __attribute__((unused)) ntp_build_request(ntp_packet_t *pkt)
{
    memset(pkt, 0, sizeof(*pkt));
    pkt->flags = (uint8_t) ((NTP_LEAP_NO_WARNING << 6) |
                            (NTP_VERSION_3 << 3) |
                            NTP_MODE_CLIENT);
    pkt->poll = 4;
    pkt->precision = -6;
}

/* Try every configured server until one answers. Returns true on sync. */
bool ntp_sync_all(void)
{
    for (uint32_t i = 0; i < NTP_SERVER_COUNT; i++) {
        if (rtc_ntp_sync(g_ntp_servers[i])) {
            return true;
        }
    }
    return false;
}

const char *ntp_default_server(void)
{
    return g_ntp_servers[0];
}
