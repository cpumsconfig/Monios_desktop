#ifndef _DHCP_H_
#define _DHCP_H_

/* ============================================================
 *  MoniOS DHCP client (RFC 2131 / RFC 2132).
 *
 *  This module owns the DHCP protocol state machine:
 *
 *     INIT -> SELECTING (DISCOVER) -> REQUESTING (REQUEST)
 *          -> BOUND -> RENEWING (T1, unicast REQUEST)
 *                  -> REBINDING (T2, broadcast REQUEST)
 *                  -> (lease expiry) back to INIT
 *
 *  The wire layer (Ethernet/IP/UDP broadcast and the NIC) lives in
 *  net.c; dhcp.c builds/parses DHCP messages and drives the timers.
 * ============================================================ */

#include "stdbool.h"
#include "stdint.h"

#define DHCP_DNS_MAX_SERVERS   4

typedef enum {
    DHCP_STATE_INIT = 0,
    DHCP_STATE_SELECTING,    /* sent DISCOVER, awaiting OFFER          */
    DHCP_STATE_REQUESTING,   /* sent REQUEST, awaiting ACK             */
    DHCP_STATE_BOUND,        /* lease obtained                          */
    DHCP_STATE_RENEWING,     /* T1: unicast REQUEST to server           */
    DHCP_STATE_REBINDING,     /* T2: broadcast REQUEST to any server     */
    DHCP_STATE_STOPPED
} dhcp_state_t;

typedef struct {
    uint8_t  ip[4];                       /* offered / bound client IP   */
    uint8_t  netmask[4];                  /* subnet mask                 */
    uint8_t  gateway[4];                  /* router (0.0.0.0 = none)     */
    uint8_t  dns[DHCP_DNS_MAX_SERVERS][4];/* DNS server list             */
    uint32_t dns_count;
    uint8_t  server_id[4];                /* DHCP server identifier      */
    uint32_t lease_time;                   /* option 51, seconds          */
    uint32_t renew_time;                  /* T1 option 58, seconds      */
    uint32_t rebind_time;                 /* T2 option 59, seconds       */
    bool     valid;
} dhcp_lease_t;

/* One-time init (called from net driver init). */
void dhcp_init(void);

/* Blocking initial configuration: DISCOVER -> OFFER -> REQUEST -> ACK.
 * Returns true once a lease is BOUND (config already pushed to net.c). */
bool dhcp_start_discovery(void);

/* RX hook: net.c feeds every UDP payload received from a DHCP server
 * (src port 67 -> dst port 68) here, along with the peer MAC. */
void dhcp_handle_packet(const uint8_t *payload, uint16_t len,
                       const uint8_t src_mac[6]);

/* Periodic hook (called from net_update). Drives T1/T1 renewal. */
void dhcp_tick(void);

/* True while the client is mid-negotiation; net.c uses this to accept
 * incoming DHCP frames addressed to our not-yet-configured IP. */
bool dhcp_is_waiting(void);

const dhcp_lease_t *dhcp_lease(void);
dhcp_state_t        dhcp_state(void);
const char         *dhcp_status(void);

/* ============================================================
 *  Provided by net.c (the wire layer):
 * ============================================================ */

/* Broadcast a ready-to-send DHCP UDP payload out port 68 to the
 * limited broadcast address (255.255.255.255). If src_ip is NULL the
 * IPv4 source address is 0.0.0.0 (initial discovery); otherwise the
 * given leased IP is used (rebinding). */
bool net_dhcp_broadcast(const uint8_t src_ip[4],
                        const uint8_t *payload, uint16_t len);

/* Apply a freshly-ACKed lease into the network interface globals. */
void net_dhcp_apply_lease(const dhcp_lease_t *lease);

/* The interface MAC (6 bytes) used for chaddr and xid mixing. */
const uint8_t *net_hw_mac(void);

#endif /* _DHCP_H_ */
