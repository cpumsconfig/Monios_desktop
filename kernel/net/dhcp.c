/* ============================================================
 *  MoniOS DHCP client (RFC 2131 / RFC 2132).
 *
 *  State machine:
 *    DISCOVER -> OFFER -> REQUEST -> ACK (BOUND)
 *    BOUND:  at T1 send unicast REQUEST (RENEWING)
 *            at T2 send broadcast REQUEST (REBINDING)
 *            at lease expiry fall back to DISCOVER.
 *
 *  net.c owns the NIC and the Ethernet/IP/UDP wire helpers; this file
 *  only builds/parses DHCP messages and drives the lease timers.
 * ============================================================ */

#include "common.h"
#include "cpu.h"
#include "dhcp.h"
#include "ip.h"
#include "kernel.h"
#include "net.h"

#define DHCP_SERVER_PORT        67
#define DHCP_CLIENT_PORT        68
#define DHCP_MAGIC_COOKIE       0x63825363U

#define DHCP_OP_BOOTREQUEST     1
#define DHCP_OP_BOOTREPLY       2
#define DHCP_HTYPE_ETHER        1
#define DHCP_HLEN_ETHER         6

#define DHCP_MSG_DISCOVER       1
#define DHCP_MSG_OFFER          2
#define DHCP_MSG_REQUEST        3
#define DHCP_MSG_DECLINE        4
#define DHCP_MSG_ACK            5
#define DHCP_MSG_NAK            6
#define DHCP_MSG_RELEASE        7
#define DHCP_MSG_INFORM         8

#define DHCP_OPT_PAD           0
#define DHCP_OPT_SUBNET_MASK    1
#define DHCP_OPT_ROUTER         3
#define DHCP_OPT_DNS            6
#define DHCP_OPT_HOST_NAME      12
#define DHCP_OPT_REQ_IP         50
#define DHCP_OPT_MSG_TYPE       53
#define DHCP_OPT_SERVER_ID      54
#define DHCP_OPT_PARAM_REQ      55
#define DHCP_OPT_VENDOR         60
#define DHCP_OPT_LEASE_TIME     51
#define DHCP_OPT_RENEW_T1       58
#define DHCP_OPT_REBIND_T2      59
#define DHCP_OPT_END            255

#define DHCP_BROADCAST_FLAG     0x8000
#define DHCP_MIN_PACKET         240

/* Discovery / request retry budget (busy-wait iterations per attempt). */
#define DHCP_RETRY_ATTEMPTS     3
#define DHCP_WAIT_LOOPS         400000U

static dhcp_state_t   g_state = DHCP_STATE_INIT;
static uint32_t       g_xid;
static bool           g_got_reply;        /* expected message arrived      */
static uint8_t        g_reply_msgtype;     /* OFFER / ACK / NAK             */
static dhcp_lease_t   g_offer;            /* candidate from OFFER          */
static dhcp_lease_t   g_lease;            /* current bound lease           */
static uint64_t      g_lease_start;       /* timer_ticks() at ACK          */
static uint64_t       g_last_renew_tx;     /* rate-limit renew/rebind       */
static char           g_status[64];

/* ---------- wire helpers (big-endian read/write) -------------------- */

static uint32_t dhcp_read32_be(const uint8_t *d)
{
    return ((uint32_t) d[0] << 24) | ((uint32_t) d[1] << 16) |
           ((uint32_t) d[2] << 8)  |  (uint32_t) d[3];
}

static void dhcp_put32_be(uint8_t *d, uint32_t v)
{
    d[0] = (uint8_t) (v >> 24);
    d[1] = (uint8_t) (v >> 16);
    d[2] = (uint8_t) (v >> 8);
    d[3] = (uint8_t) v;
}

static bool dhcp_ip_zero(const uint8_t ip[4])
{
    return ip == NULL || (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
}

static uint32_t dhcp_make_xid(void)
{
    const uint8_t *mac = net_hw_mac();
    uint64_t tsc = cpu_read_tsc();
    uint32_t mix = (uint32_t) timer_ticks() ^ (uint32_t) tsc ^ (uint32_t) (tsc >> 32);

    mix ^= (uint32_t) mac[0] << 24;
    mix ^= (uint32_t) mac[1] << 16;
    mix ^= (uint32_t) mac[2] << 8;
    mix ^= (uint32_t) mac[3];
    mix ^= (uint32_t) mac[4] << 11;
    mix ^= (uint32_t) mac[5] << 3;
    mix ^= mix << 13;
    mix ^= mix >> 17;
    mix ^= mix << 5;
    return 0x4D4F0000u ^ mix;
}

/* Build a DHCP message into payload (>= 300 bytes). Returns length.
 *   msg_type   : DISCOVER / REQUEST
 *   ciaddr     : our current IP (0.0.0.0 during initial discovery)
 *   req_ip     : requested IP (REQUEST only), may be NULL
 *   server_id  : server identifier (REQUEST only), may be NULL
 */
static uint16_t dhcp_build_message(uint8_t *payload,
                                   uint8_t msg_type,
                                   const uint8_t ciaddr[4],
                                   const uint8_t req_ip[4],
                                   const uint8_t server_id[4])
{
    uint8_t *opt;

    memset(payload, 0, 300);
    payload[0] = DHCP_OP_BOOTREQUEST;
    payload[1] = DHCP_HTYPE_ETHER;
    payload[2] = DHCP_HLEN_ETHER;
    dhcp_put32_be(payload + 4, g_xid);
    ip_put16(payload + 10, DHCP_BROADCAST_FLAG);
    if (ciaddr != NULL) {
        memcpy(payload + 12, ciaddr, 4);
    }
    memcpy(payload + 28, net_hw_mac(), 6);
    dhcp_put32_be(payload + 236, DHCP_MAGIC_COOKIE);

    opt = payload + 240;
    *opt++ = DHCP_OPT_MSG_TYPE;
    *opt++ = 1;
    *opt++ = msg_type;
    if (req_ip != NULL) {
        *opt++ = DHCP_OPT_REQ_IP;
        *opt++ = 4;
        memcpy(opt, req_ip, 4);
        opt += 4;
    }
    if (server_id != NULL) {
        *opt++ = DHCP_OPT_SERVER_ID;
        *opt++ = 4;
        memcpy(opt, server_id, 4);
        opt += 4;
    }
    *opt++ = DHCP_OPT_PARAM_REQ;
    *opt++ = 4;
    *opt++ = DHCP_OPT_SUBNET_MASK;
    *opt++ = DHCP_OPT_ROUTER;
    *opt++ = DHCP_OPT_DNS;
    *opt++ = DHCP_OPT_LEASE_TIME;
    *opt++ = DHCP_OPT_VENDOR;
    *opt++ = 6;
    memcpy(opt, "monios", 6);
    opt += 6;
    *opt++ = DHCP_OPT_END;
    return (uint16_t) (opt - payload);
}

static bool dhcp_send_discover(void)
{
    uint8_t payload[300];
    uint16_t len = dhcp_build_message(payload, DHCP_MSG_DISCOVER, NULL, NULL, NULL);

    strcpy(g_status, "dhcp: discover");
    log_write(g_status);
    return net_dhcp_broadcast(NULL, payload, len);
}

static bool dhcp_send_request(const uint8_t ciaddr[4],
                              const uint8_t req_ip[4],
                              const uint8_t server_id[4],
                              bool broadcast)
{
    uint8_t payload[300];
    uint16_t len = dhcp_build_message(payload, DHCP_MSG_REQUEST,
                                      ciaddr, req_ip, server_id);

    if (broadcast) {
        strcpy(g_status, "dhcp: request(bcast)");
        log_write(g_status);
        return net_dhcp_broadcast(ciaddr, payload, len);
    }
    strcpy(g_status, "dhcp: request(ucast)");
    log_write(g_status);
    /* Unicast renew: src port 68 -> server port 67. */
    return net_udp_send_to(server_id, DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
                           payload, len);
}

/* Parse the option area of a reply into a lease structure. */
static void dhcp_parse_options(const uint8_t *udp, uint16_t len,
                              dhcp_lease_t *lease)
{
    const uint8_t *opt = udp + 240;
    const uint8_t *end = udp + len;
    uint8_t mask[4] = { 255, 255, 255, 0 };

    lease->dns_count = 0;
    lease->lease_time = 0;
    lease->renew_time = 0;
    lease->rebind_time = 0;

    while (opt < end && *opt != DHCP_OPT_END) {
        uint8_t code = *opt++;
        uint8_t olen;

        if (code == DHCP_OPT_PAD) {
            continue;
        }
        if (opt >= end) {
            break;
        }
        olen = *opt++;
        if ((uint32_t) (end - opt) < olen) {
            break;
        }
        switch (code) {
        case DHCP_OPT_SUBNET_MASK:
            if (olen >= 4) {
                memcpy(mask, opt, 4);
            }
            break;
        case DHCP_OPT_ROUTER:
            if (olen >= 4 && dhcp_ip_zero(lease->gateway)) {
                memcpy(lease->gateway, opt, 4);
            }
            break;
        case DHCP_OPT_DNS:
            for (uint8_t i = 0; i + 4 <= olen &&
                                lease->dns_count < DHCP_DNS_MAX_SERVERS; i += 4) {
                memcpy(lease->dns[lease->dns_count], opt + i, 4);
                lease->dns_count++;
            }
            break;
        case DHCP_OPT_SERVER_ID:
            if (olen >= 4) {
                memcpy(lease->server_id, opt, 4);
            }
            break;
        case DHCP_OPT_LEASE_TIME:
            if (olen >= 4) {
                lease->lease_time = dhcp_read32_be(opt);
            }
            break;
        case DHCP_OPT_RENEW_T1:
            if (olen >= 4) {
                lease->renew_time = dhcp_read32_be(opt);
            }
            break;
        case DHCP_OPT_REBIND_T2:
            if (olen >= 4) {
                lease->rebind_time = dhcp_read32_be(opt);
            }
            break;
        default:
            break;
        }
        opt += olen;
    }
    memcpy(lease->netmask, mask, 4);
    if (lease->lease_time == 0) {
        lease->lease_time = 3600;      /* RFC default */
    }
    if (lease->renew_time == 0) {
        lease->renew_time = lease->lease_time / 2;
    }
    if (lease->rebind_time == 0) {
        lease->rebind_time = (uint32_t) ((uint64_t) lease->lease_time * 7u / 8u);
    }
}

void dhcp_handle_packet(const uint8_t *udp, uint16_t len,
                        const uint8_t src_mac[6])
{
    uint8_t msg_type = 0;
    const uint8_t *opt;
    const uint8_t *end;

    (void) src_mac;
    if (udp == NULL || len < DHCP_MIN_PACKET) {
        return;
    }
    if (udp[0] != DHCP_OP_BOOTREPLY ||
        udp[1] != DHCP_HTYPE_ETHER ||
        udp[2] != DHCP_HLEN_ETHER) {
        return;
    }
    if (dhcp_read32_be(udp + 4) != g_xid) {
        return;
    }
    if (memcmp(udp + 28, net_hw_mac(), 6) != 0 ||
        dhcp_read32_be(udp + 236) != DHCP_MAGIC_COOKIE) {
        return;
    }

    /* Locate message type option. */
    opt = udp + 240;
    end = udp + len;
    while (opt < end && *opt != DHCP_OPT_END) {
        uint8_t code = *opt++;
        uint8_t olen;

        if (code == DHCP_OPT_PAD || opt >= end) {
            continue;
        }
        olen = *opt++;
        if ((uint32_t) (end - opt) < olen) {
            break;
        }
        if (code == DHCP_OPT_MSG_TYPE && olen >= 1) {
            msg_type = opt[0];
        }
        opt += olen;
    }

    if (msg_type == DHCP_MSG_OFFER && g_state == DHCP_STATE_SELECTING) {
        memset(&g_offer, 0, sizeof(g_offer));
        memcpy(g_offer.ip, udp + 16, 4);           /* yiaddr */
        if (dhcp_ip_zero(g_offer.ip)) {
            return;
        }
        dhcp_parse_options(udp, len, &g_offer);
        if (dhcp_ip_zero(g_offer.server_id)) {
            memcpy(g_offer.server_id, udp + 20, 4); /* siaddr */
        }
        g_offer.valid = true;
        g_got_reply = true;
        g_reply_msgtype = DHCP_MSG_OFFER;
        strcpy(g_status, "dhcp: offer ");
        ip_to_text(g_offer.ip, g_status + strlen(g_status));
        log_write(g_status);
        return;
    }

    if ((msg_type == DHCP_MSG_ACK || msg_type == DHCP_MSG_NAK) &&
        (g_state == DHCP_STATE_REQUESTING ||
         g_state == DHCP_STATE_RENEWING ||
         g_state == DHCP_STATE_REBINDING)) {
        if (msg_type == DHCP_MSG_NAK) {
            g_got_reply = true;
            g_reply_msgtype = DHCP_MSG_NAK;
            strcpy(g_status, "dhcp: nak");
            log_write(g_status);
            return;
        }
        /* ACK: adopt yiaddr + options. */
        memset(&g_lease, 0, sizeof(g_lease));
        memcpy(g_lease.ip, udp + 16, 4);
        dhcp_parse_options(udp, len, &g_lease);
        if (dhcp_ip_zero(g_lease.server_id)) {
            memcpy(g_lease.server_id, udp + 20, 4);
        }
        if (dhcp_ip_zero(g_lease.gateway) && !dhcp_ip_zero(g_offer.gateway)) {
            memcpy(g_lease.gateway, g_offer.gateway, 4);
        }
        g_lease.valid = true;
        g_lease_start = timer_ticks();
        g_last_renew_tx = g_lease_start;
        net_dhcp_apply_lease(&g_lease);
        g_state = DHCP_STATE_BOUND;
        g_got_reply = true;
        g_reply_msgtype = DHCP_MSG_ACK;
        strcpy(g_status, "dhcp: ack ip ");
        ip_to_text(g_lease.ip, g_status + strlen(g_status));
        log_write(g_status);
    }
}

/* Busy-wait for an expected reply, pumping the NIC. */
static bool dhcp_wait_reply(uint32_t loops)
{
    for (uint32_t i = 0; i < loops && !g_got_reply; i++) {
        net_update();
        io_wait();
    }
    return g_got_reply;
}

bool dhcp_start_discovery(void)
{
    uint32_t attempt;

    g_state = DHCP_STATE_INIT;
    g_offer.valid = false;

    /* ---- SELECTING: DISCOVER -> OFFER ---- */
    g_state = DHCP_STATE_SELECTING;
    g_got_reply = false;
    g_reply_msgtype = 0;
    for (attempt = 0; attempt < DHCP_RETRY_ATTEMPTS && !g_offer.valid; attempt++) {
        g_xid = dhcp_make_xid();
        g_got_reply = false;
        if (!dhcp_send_discover()) {
            strcpy(g_status, "dhcp: discover tx failed");
            log_write(g_status);
            break;
        }
        dhcp_wait_reply(DHCP_WAIT_LOOPS);
    }
    if (!g_offer.valid) {
        g_state = DHCP_STATE_INIT;
        strcpy(g_status, "dhcp: no offer");
        log_write(g_status);
        return false;
    }

    /* ---- REQUESTING: REQUEST -> ACK ---- */
    g_state = DHCP_STATE_REQUESTING;
    for (attempt = 0; attempt < DHCP_RETRY_ATTEMPTS; attempt++) {
        g_got_reply = false;
        g_reply_msgtype = 0;
        if (!dhcp_send_request(NULL, g_offer.ip, g_offer.server_id, true)) {
            strcpy(g_status, "dhcp: request tx failed");
            log_write(g_status);
            break;
        }
        dhcp_wait_reply(DHCP_WAIT_LOOPS);
        if (g_reply_msgtype == DHCP_MSG_ACK) {
            return true;                 /* handler already applied lease */
        }
        if (g_reply_msgtype == DHCP_MSG_NAK) {
            strcpy(g_status, "dhcp: nak on request");
            log_write(g_status);
            break;
        }
    }
    g_state = DHCP_STATE_INIT;
    strcpy(g_status, "dhcp: no ack");
    log_write(g_status);
    return false;
}

void dhcp_tick(void)
{
    uint32_t hz;
    uint64_t now;
    uint32_t elapsed;

    if (g_state != DHCP_STATE_BOUND || !g_lease.valid) {
        return;
    }
    hz = timer_hz();
    if (hz == 0) {
        return;
    }
    now = timer_ticks();
    elapsed = (uint32_t) ((now - g_lease_start) / hz);

    /* Lease fully expired: drop back to discovery. */
    if (elapsed >= g_lease.lease_time) {
        strcpy(g_status, "dhcp: lease expired, rediscover");
        log_write(g_status);
        dhcp_start_discovery();
        return;
    }

    /* Rate-limit renew/rebind retransmissions to once per ~5 s. */
    if (now - g_last_renew_tx < (uint64_t) hz * 5u) {
        return;
    }

    if (elapsed >= g_lease.rebind_time) {
        /* T2: broadcast REQUEST to any server (REBINDING). */
        g_state = DHCP_STATE_REBINDING;
        g_last_renew_tx = now;
        dhcp_send_request(g_lease.ip, NULL, NULL, true);
    } else if (elapsed >= g_lease.renew_time) {
        /* T1: unicast REQUEST to the owning server (RENEWING). */
        g_state = DHCP_STATE_RENEWING;
        g_last_renew_tx = now;
        dhcp_send_request(g_lease.ip, NULL, g_lease.server_id, false);
    }
}

bool dhcp_is_waiting(void)
{
    return g_state == DHCP_STATE_SELECTING ||
           g_state == DHCP_STATE_REQUESTING ||
           g_state == DHCP_STATE_RENEWING ||
           g_state == DHCP_STATE_REBINDING;
}

void dhcp_init(void)
{
    g_state = DHCP_STATE_INIT;
    g_got_reply = false;
    g_reply_msgtype = 0;
    memset(&g_offer, 0, sizeof(g_offer));
    memset(&g_lease, 0, sizeof(g_lease));
    g_lease_start = 0;
    g_last_renew_tx = 0;
    g_xid = dhcp_make_xid();
    strcpy(g_status, "dhcp: ready");
}

const dhcp_lease_t *dhcp_lease(void)
{
    return &g_lease;
}

dhcp_state_t dhcp_state(void)
{
    return g_state;
}

const char *dhcp_status(void)
{
    return g_status;
}
