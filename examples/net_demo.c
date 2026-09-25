/*
 * net_demo.c - MoniOS network example (feature 32).
 *
 * Demonstrates:
 *   - fetching a URL over HTTP via app_http_get_url()
 *   - UDP socket send/receive (DNS-style datagram echo)
 *
 * Build:
 *   x86_64-w64-mingw32-gcc examples/net_demo.c \
 *       -I user/lib -I include -o C:\\Monios\\Apps\\net_demo.exe
 */
#include "appsys.h"
#include "console_dll.h"
#include <stdio.h>
#include <string.h>

static void show_network_status(void)
{
    app_system_status_t st;
    if (app_get_system_status(&st) != 0) {
        printf("net: status unavailable\n");
        return;
    }
    printf("net present=%d connected=%d dhcp=%d\n",
           st.net_present, st.net_connected, st.net_dhcp_configured);
    printf("ip=%s gw=%s dns=%s mac=%s\n",
           st.net_ip, st.net_gateway, st.net_dns, st.net_mac);
}

static void http_demo(const char *url)
{
    char buf[2048];
    printf("HTTP GET %s ...\n", url);
    int n = app_http_get_url(url, buf, sizeof(buf) - 1);
    if (n < 0) {
        printf("HTTP GET failed (%d)\n", n);
        return;
    }
    buf[n] = '\0';
    printf("HTTP %d bytes:\n%.200s%s\n", n, buf, n > 200 ? "..." : "");
}

static void udp_demo(const char *echo_host, uint16_t echo_port)
{
    int s = app_socket_udp_open(0);
    if (s < 0) {
        printf("udp: open failed\n");
        return;
    }
    const char *msg = "monios-udp-ping";
    int w = app_socket_sendto(s, echo_host, echo_port, msg, (uint16_t)strlen(msg));
    printf("udp: sent %d bytes to %s:%u\n", w, echo_host, echo_port);

    char src_ip[24]; uint16_t src_port = 0; char rbuf[128];
    int r = app_socket_recvfrom(s, src_ip, &src_port, rbuf, sizeof(rbuf) - 1);
    if (r > 0) {
        rbuf[r] = '\0';
        printf("udp: %d bytes from %s:%u -> %s\n", r, src_ip, src_port, rbuf);
    } else {
        printf("udp: no reply\n");
    }
    app_socket_close(s);
}

int main(int argc, char **argv)
{
    const char *url = "http://monios.local/index.html";
    const char *echo = "8.8.8.8";
    uint16_t echo_port = 53;   /* DNS: send a tiny query, expect reply */

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "url=", 4) == 0) url = argv[i] + 4;
    }

    console_set_title("MoniOS Net Demo");
    show_network_status();
    http_demo(url);
    udp_demo(echo, echo_port);
    return 0;
}
