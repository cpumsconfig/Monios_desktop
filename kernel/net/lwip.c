#include "common.h"
#include "lwip.h"
#include "net.h"
#include "socket.h"

static lwip_info_t g_lwip_info;

static void lwip_copy(char *dst, uint32_t size, const char *src)
{
    uint32_t i = 0;

    if (size == 0) {
        return;
    }
    while (src != NULL && src[i] != '\0' && i + 1 < size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void lwip_update(void)
{
    const net_info_t *net = net_info();

    g_lwip_info.netif_up = net->present && net->connected;
    g_lwip_info.dhcp_configured = net->dhcp_configured;
    g_lwip_info.dns_configured = net->dns_text[0] != '\0' && strcmp(net->dns_text, "0.0.0.0") != 0;
    g_lwip_info.tx_packets = net->tx_packets;
    g_lwip_info.rx_packets = net->rx_packets;
    g_lwip_info.udp_sockets = socket_count();
    g_lwip_info.tcp_pcbs = g_lwip_info.netif_up ? 1u : 0u;
    lwip_copy(g_lwip_info.ip, sizeof(g_lwip_info.ip), net->ip_text);
    lwip_copy(g_lwip_info.gateway, sizeof(g_lwip_info.gateway), net->gateway_text);
    lwip_copy(g_lwip_info.dns, sizeof(g_lwip_info.dns), net->dns_text);
    lwip_copy(g_lwip_info.driver, sizeof(g_lwip_info.driver), net->driver);
    if (!g_lwip_info.initialized) {
        strcpy(g_lwip_info.status, "lwip: not initialized");
    } else {
        strcpy(g_lwip_info.status, g_lwip_info.netif_up ? "lwip: netif up (compat shim)" : "lwip: netif down");
    }
}

void lwip_init(void)
{
    memset(&g_lwip_info, 0, sizeof(g_lwip_info));
    g_lwip_info.initialized = true;
    lwip_update();
}

bool lwip_udp_send(const char *dst_host, uint16_t dst_port, const uint8_t *payload, uint16_t payload_len)
{
    bool ok;

    if (!g_lwip_info.initialized || dst_host == NULL || payload == NULL || payload_len == 0) {
        lwip_update();
        return false;
    }
    ok = net_udp_send(dst_host, dst_port, payload, payload_len);

    lwip_update();
    return ok;
}

const lwip_info_t *lwip_info(void)
{
    lwip_update();
    return &g_lwip_info;
}

const char *lwip_status(void)
{
    lwip_update();
    return g_lwip_info.status;
}
