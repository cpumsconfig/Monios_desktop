/*
 * nettest.c — Monios command-line network verification tool.
 *
 * Usage:
 *   nettest ping <host>      ICMP echo request/reply, RTT stats (4 probes)
 *   nettest http <url>       HTTP GET, print status line + first 512 bytes
 *   nettest dns <hostname>  DNS resolver -> dotted IPv4 text
 *   nettest ifconfig        network interface / address state
 *
 * Build: x86_64-w64-mingw32-gcc (see Makefile rule out/app_nettest.pe.o).
 *
 * The new kernel entry points used here are SYS_NET_PING (70) and
 * SYS_NET_RESOLVE (71); http/dns-via-http and interface status reuse the
 * existing SYS_HTTP_GET_URL (43) and SYS_SYSTEM_STATUS (18) paths.
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "monios_dll.h"
#include "string.h"
#include "syscall.h"

static void usage(void)
{
    fputs("Monios nettest — network verification tool\r\n");
    fputs("usage:\r\n");
    fputs("  nettest ping <host>\r\n");
    fputs("  nettest http <url>\r\n");
    fputs("  nettest dns <hostname>\r\n");
    fputs("  nettest ifconfig\r\n");
}

/* ------------------------------------------------------------------ */
/* ping                                                                */
/* ------------------------------------------------------------------ */
static int cmd_ping(const char *host)
{
    uint32_t sent = 0;
    uint32_t ok = 0;
    uint64_t total_ticks = 0;
    uint64_t min_ticks = 0;
    uint64_t max_ticks = 0;
    uint32_t i;

    fputs("Pinging ");
    fputs(host);
    fputs(" ...\r\n");

    for (i = 0; i < 4; i++) {
        uint64_t t0 = app_ticks();
        int32_t rc = (int32_t) monios_syscall1(SYS_NET_PING, (uint64_t) host);
        uint64_t dt = app_ticks() - t0;

        sent++;
        if (rc == 0) {
            ok++;
            fputs("  reply from ");
            fputs(host);
            fputs(": time=");
            print_uint((uint32_t) dt);
            fputs(" ticks\r\n");
            total_ticks += dt;
            if (min_ticks == 0 || dt < min_ticks) {
                min_ticks = dt;
            }
            if (dt > max_ticks) {
                max_ticks = dt;
            }
        } else {
            fputs("  request timed out / no reply\r\n");
        }
        app_sleep_ticks(1);
    }

    fputs("\r\n--- ping statistics ---\r\n");
    fputs("packets: sent=");
    print_uint(sent);
    fputs(" received=");
    print_uint(ok);
    fputs(" lost=");
    print_uint(sent - ok);
    fputs("\r\n");
    if (ok > 0) {
        fputs("round-trip ticks: min=");
        print_uint((uint32_t) min_ticks);
        fputs(" avg=");
        print_uint((uint32_t) (total_ticks / ok));
        fputs(" max=");
        print_uint((uint32_t) max_ticks);
        fputs("\r\n");
    }
    return ok > 0 ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* http                                                                */
/* ------------------------------------------------------------------ */
static int cmd_http(const char *url)
{
    static char response[MONIOS_HTTP_RESPONSE_MAX];
    int32_t n;
    uint32_t shown;
    uint32_t i;

    fputs("GET ");
    fputs(url);
    fputs("\r\n");

    n = monios_http_get_url(url, response, sizeof(response));
    if (n < 0) {
        fputs("http: request failed (offline / bad url / connect error)\r\n");
        return 1;
    }

    /* Print the status line (up to first CRLF) verbatim. */
    fputs("--- response status ---\r\n");
    for (i = 0; i < (uint32_t) n && response[i] != '\r' && response[i] != '\n'; i++) {
        putchar(response[i]);
    }
    fputs("\r\n");

    /* Skip headers up to the blank line, then dump up to 512 body bytes. */
    i = 0;
    while (i + 3 < (uint32_t) n) {
        if (response[i] == '\r' && response[i + 1] == '\n' &&
            response[i + 2] == '\r' && response[i + 3] == '\n') {
            i += 4;
            break;
        }
        i++;
    }
    if (i >= (uint32_t) n) {
        i = 0; /* no body separator found; just dump head of response */
    }

    shown = 0;
    fputs("--- first 512 bytes ---\r\n");
    while (i < (uint32_t) n && shown < 512) {
        putchar(response[i]);
        i++;
        shown++;
    }
    fputs("\r\n--- end (");
    print_uint(shown);
    fputs(" of ");
    print_uint((uint32_t) n);
    fputs(" bytes) ---\r\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* dns                                                                 */
/* ------------------------------------------------------------------ */
static int cmd_dns(const char *hostname)
{
    char ip_text[16];
    int32_t rc;

    ip_text[0] = '\0';
    rc = (int32_t) monios_syscall3(SYS_NET_RESOLVE,
                                   (uint64_t) hostname,
                                   (uint64_t) ip_text,
                                   (uint32_t) sizeof(ip_text));
    fputs("dns resolve ");
    fputs(hostname);
    fputs(" -> ");
    if (rc == 0 && ip_text[0] != '\0') {
        fputs(ip_text);
        fputs("\r\n");
        return 0;
    }
    fputs("FAILED\r\n");
    return 1;
}

/* ------------------------------------------------------------------ */
/* ifconfig                                                            */
/* ------------------------------------------------------------------ */
static void print_field(const char *label, const char *value)
{
    fputs("  ");
    fputs(label);
    fputs(value[0] == '\0' ? "none" : value);
    fputs("\r\n");
}

static int cmd_ifconfig(void)
{
    app_system_status_t st;

    if (app_get_system_status(&st) != 0) {
        fputs("ifconfig: unable to query system status\r\n");
        return 1;
    }

    fputs("Monios network interface\r\n");
    fputs("  adapter present : ");
    fputs(st.net_present ? "yes" : "no");
    fputs("\r\n");
    fputs("  link state      : ");
    fputs(st.net_connected ? "up" : "down");
    fputs("\r\n");
    fputs("  config source   : ");
    fputs(st.net_dhcp_configured ? "dhcp" : "static");
    fputs("\r\n");
    print_field("driver          : ", st.net_driver);
    print_field("mac address     : ", st.net_mac);
    print_field("ipv4 address    : ", st.net_ip);
    print_field("gateway         : ", st.net_gateway);
    print_field("dns server      : ", st.net_dns);
    fputs("  tx packets      : ");
    print_uint(st.net_tx_packets);
    fputs("\r\n");
    fputs("  rx packets      : ");
    print_uint(st.net_rx_packets);
    fputs("\r\n");
    fputs("  ping sent       : ");
    print_uint(st.net_ping_requests);
    fputs("  received: ");
    print_uint(st.net_ping_replies);
    fputs("\r\n");
    fputs("  status          : ");
    fputs(st.net_status);
    fputs("\r\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage();
        return 1;
    }

    if (strcmp(argv[1], "ping") == 0) {
        if (argc < 3) {
            fputs("ping: missing host\r\n");
            return 1;
        }
        return cmd_ping(argv[2]);
    }
    if (strcmp(argv[1], "http") == 0) {
        if (argc < 3) {
            fputs("http: missing url\r\n");
            return 1;
        }
        return cmd_http(argv[2]);
    }
    if (strcmp(argv[1], "dns") == 0) {
        if (argc < 3) {
            fputs("dns: missing hostname\r\n");
            return 1;
        }
        return cmd_dns(argv[2]);
    }
    if (strcmp(argv[1], "ifconfig") == 0) {
        return cmd_ifconfig();
    }

    usage();
    return 1;
}
