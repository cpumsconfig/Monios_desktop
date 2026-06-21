#include "common.h"
#include "dma.h"
#include "e1000.h"
#include "kernel.h"
#include "pci.h"

#define PCI_VENDOR_INTEL              0x8086
#define PCI_DEVICE_E1000_82540EM      0x100E
#define PCI_DEVICE_E1000_82545EM      0x100F

#define PCI_COMMAND_OFFSET            0x04
#define PCI_COMMAND_IO                0x0001
#define PCI_COMMAND_MEMORY            0x0002
#define PCI_COMMAND_BUS_MASTER        0x0004

#define E1000_REG_STATUS              0x00008
#define E1000_REG_IMC                 0x000D8
#define E1000_REG_CTRL                0x00000
#define E1000_REG_RCTL                0x00100
#define E1000_REG_TCTL                0x00400
#define E1000_REG_TIPG                0x00410
#define E1000_REG_RAL0                0x05400
#define E1000_REG_RAH0                0x05404
#define E1000_REG_RDBAL               0x02800
#define E1000_REG_RDBAH               0x02804
#define E1000_REG_RDLEN               0x02808
#define E1000_REG_RDH                 0x02810
#define E1000_REG_RDT                 0x02818
#define E1000_REG_TDBAL               0x03800
#define E1000_REG_TDBAH               0x03804
#define E1000_REG_TDLEN               0x03808
#define E1000_REG_TDH                 0x03810
#define E1000_REG_TDT                 0x03818

#define E1000_STATUS_LU               0x00000002
#define E1000_CTRL_SLU                0x00000040
#define E1000_CTRL_ASDE               0x00000020
#define E1000_RCTL_EN                 0x00000002
#define E1000_RCTL_SBP                0x00000004
#define E1000_RCTL_UPE                0x00000008
#define E1000_RCTL_MPE                0x00000010
#define E1000_RCTL_BAM                0x00008000
#define E1000_RCTL_SECRC              0x04000000
#define E1000_TCTL_EN                 0x00000002
#define E1000_TCTL_PSP                0x00000008
#define E1000_TCTL_CT_SHIFT           4
#define E1000_TCTL_COLD_SHIFT         12
#define E1000_TX_CMD_EOP              0x01
#define E1000_TX_CMD_IFCS             0x02
#define E1000_TX_CMD_RS               0x08
#define E1000_DESC_STATUS_DD          0x01

#define E1000_RING_SIZE               32
#define E1000_BUFFER_SIZE             2048

typedef struct {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

typedef struct {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

static net_info_t *g_net;
static dma_buffer_t g_rx_desc_dma;
static dma_buffer_t g_tx_desc_dma;
static dma_buffer_t g_rx_buffer_dma[E1000_RING_SIZE];
static dma_buffer_t g_tx_buffer_dma[E1000_RING_SIZE];
static volatile e1000_rx_desc_t *g_rx_desc;
static volatile e1000_tx_desc_t *g_tx_desc;
static uint32_t g_rx_tail;
static uint32_t g_tx_tail;
static bool g_ready;
static bool g_rx_enabled;

static void e1000_append_dec(char *out, uint32_t value)
{
    char temp[11];
    uint32_t pos = 0;

    if (value == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (value > 0 && pos < sizeof(temp)) {
        temp[pos++] = (char) ('0' + (value % 10));
        value /= 10;
    }
    for (uint32_t i = 0; i < pos; i++) {
        out[i] = temp[pos - 1 - i];
    }
    out[pos] = '\0';
}

static void e1000_append_hex32(char *out, uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    for (uint32_t i = 0; i < 8; i++) {
        out[i] = hex[(value >> ((7 - i) * 4)) & 0xF];
    }
    out[8] = '\0';
}

static void e1000_append_bool(char *out, bool value)
{
    strcpy(out, value ? "yes" : "no");
}

static uint32_t e1000_read32(uint32_t reg);

void e1000_debug_state(const char *reason)
{
    char line[160] = "e1000: ";
    char value[16];
    uint32_t tail = g_tx_tail;

    if (reason != NULL) {
        strcpy(line + strlen(line), reason);
    } else {
        strcpy(line + strlen(line), "state");
    }
    strcpy(line + strlen(line), " ready=");
    e1000_append_bool(value, g_ready);
    strcpy(line + strlen(line), value);
    strcpy(line + strlen(line), " rx=");
    e1000_append_bool(value, g_rx_enabled);
    strcpy(line + strlen(line), value);
    strcpy(line + strlen(line), " link=");
    e1000_append_bool(value, e1000_link_up());
    strcpy(line + strlen(line), value);
    strcpy(line + strlen(line), " tail=");
    e1000_append_dec(value, tail);
    strcpy(line + strlen(line), value);

    if (g_ready && g_tx_desc != NULL && tail < E1000_RING_SIZE) {
        strcpy(line + strlen(line), " desc=");
        e1000_append_hex32(value, g_tx_desc[tail].status);
        strcpy(line + strlen(line), value);
        strcpy(line + strlen(line), " tdh=");
        e1000_append_dec(value, e1000_read32(E1000_REG_TDH));
        strcpy(line + strlen(line), value);
        strcpy(line + strlen(line), " tdt=");
        e1000_append_dec(value, e1000_read32(E1000_REG_TDT));
        strcpy(line + strlen(line), value);
    }
    log_write(line);
}

bool e1000_supported(const pci_device_info_t *info)
{
    return info != NULL && info->vendor_id == PCI_VENDOR_INTEL &&
           (info->device_id == PCI_DEVICE_E1000_82540EM || info->device_id == PCI_DEVICE_E1000_82545EM);
}

static uint32_t e1000_bar_base(uint32_t bar)
{
    if ((bar & 1u) != 0) {
        return bar & 0xFFFFFFFCu;
    }
    return bar & 0xFFFFFFF0u;
}

static uint32_t e1000_read32(uint32_t reg)
{
    volatile uint32_t *ptr = (volatile uint32_t *) (uint64_t) (g_net->mmio_base + reg);

    return *ptr;
}

static void e1000_write32(uint32_t reg, uint32_t value)
{
    volatile uint32_t *ptr = (volatile uint32_t *) (uint64_t) (g_net->mmio_base + reg);

    *ptr = value;
}

static void e1000_enable_pci(const pci_device_info_t *info)
{
    uint16_t command = pci_config_read16(info->bus, info->slot, info->func, PCI_COMMAND_OFFSET);

    command |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;
    pci_config_write16(info->bus, info->slot, info->func, PCI_COMMAND_OFFSET, command);
}

bool e1000_link_up(void)
{
    return g_ready && (e1000_read32(E1000_REG_STATUS) & E1000_STATUS_LU) != 0;
}

static bool e1000_init_rings(void)
{
    if (!dma_alloc(sizeof(e1000_rx_desc_t) * E1000_RING_SIZE, 4096, 0xFFFFFFFFu, &g_rx_desc_dma) ||
        !dma_alloc(sizeof(e1000_tx_desc_t) * E1000_RING_SIZE, 4096, 0xFFFFFFFFu, &g_tx_desc_dma)) {
        log_write("e1000: desc dma alloc failed");
        return false;
    }
    g_rx_desc = (volatile e1000_rx_desc_t *) g_rx_desc_dma.virtual_address;
    g_tx_desc = (volatile e1000_tx_desc_t *) g_tx_desc_dma.virtual_address;

    e1000_write32(E1000_REG_RCTL, 0);
    e1000_write32(E1000_REG_TCTL, 0);
    e1000_write32(E1000_REG_IMC, 0xFFFFFFFFu);

    for (uint32_t i = 0; i < E1000_RING_SIZE; i++) {
        if (!dma_alloc(E1000_BUFFER_SIZE, E1000_BUFFER_SIZE, 0xFFFFFFFFu, &g_rx_buffer_dma[i]) ||
            !dma_alloc(E1000_BUFFER_SIZE, E1000_BUFFER_SIZE, 0xFFFFFFFFu, &g_tx_buffer_dma[i])) {
            log_write("e1000: buffer dma alloc failed");
            return false;
        }
        memset((void *) &g_rx_desc[i], 0, sizeof(g_rx_desc[i]));
        memset((void *) &g_tx_desc[i], 0, sizeof(g_tx_desc[i]));
        g_rx_desc[i].address = g_rx_buffer_dma[i].physical_address;
        g_rx_desc[i].status = 0;
        g_tx_desc[i].address = g_tx_buffer_dma[i].physical_address;
        g_tx_desc[i].status = E1000_DESC_STATUS_DD;
    }

    g_rx_tail = 0;
    g_tx_tail = 0;
    g_rx_enabled = false;
    e1000_write32(E1000_REG_CTRL, e1000_read32(E1000_REG_CTRL) | E1000_CTRL_SLU | E1000_CTRL_ASDE);
    e1000_write32(E1000_REG_RDBAL, (uint32_t) g_rx_desc_dma.physical_address);
    e1000_write32(E1000_REG_RDBAH, (uint32_t) (g_rx_desc_dma.physical_address >> 32));
    e1000_write32(E1000_REG_RDLEN, sizeof(e1000_rx_desc_t) * E1000_RING_SIZE);
    e1000_write32(E1000_REG_RDH, 0);
    e1000_write32(E1000_REG_RDT, E1000_RING_SIZE - 1);
    e1000_write32(E1000_REG_TDBAL, (uint32_t) g_tx_desc_dma.physical_address);
    e1000_write32(E1000_REG_TDBAH, (uint32_t) (g_tx_desc_dma.physical_address >> 32));
    e1000_write32(E1000_REG_TDLEN, sizeof(e1000_tx_desc_t) * E1000_RING_SIZE);
    e1000_write32(E1000_REG_TDH, 0);
    e1000_write32(E1000_REG_TDT, 0);
    e1000_write32(E1000_REG_RCTL, 0);
    e1000_write32(E1000_REG_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP |
                                  (0x10u << E1000_TCTL_CT_SHIFT) |
                                  (0x40u << E1000_TCTL_COLD_SHIFT));
    e1000_write32(E1000_REG_TIPG, 0x0060200Au);
    e1000_debug_state("ring initialized");
    return true;
}

bool e1000_init(const pci_device_info_t *info, net_info_t *net, uint8_t mac[6])
{
    uint32_t ral = 0;
    uint32_t rah = 0;
    uint8_t fallback_mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };

    if (!e1000_supported(info) || net == NULL || mac == NULL) {
        return false;
    }
    g_net = net;
    g_ready = false;
    e1000_enable_pci(info);
    strcpy(net->driver, "onboard-e1000");
    net->mmio_base = e1000_bar_base(info->bar0);
    net->io_base = e1000_bar_base(info->bar1);
    memcpy(mac, fallback_mac, 6);
    if (net->mmio_base == 0) {
        net->connected = true;
        log_write("e1000: mmio bar missing");
        return false;
    }

    net->connected = (e1000_read32(E1000_REG_STATUS) & E1000_STATUS_LU) != 0;
    ral = e1000_read32(E1000_REG_RAL0);
    rah = e1000_read32(E1000_REG_RAH0);
    if (ral != 0 || rah != 0) {
        mac[0] = (uint8_t) (ral & 0xFF);
        mac[1] = (uint8_t) ((ral >> 8) & 0xFF);
        mac[2] = (uint8_t) ((ral >> 16) & 0xFF);
        mac[3] = (uint8_t) ((ral >> 24) & 0xFF);
        mac[4] = (uint8_t) (rah & 0xFF);
        mac[5] = (uint8_t) ((rah >> 8) & 0xFF);
    }
    g_ready = e1000_init_rings();
    if (!g_ready) {
        log_write("e1000: init rings failed");
    }
    return g_ready;
}

bool e1000_ready(void)
{
    return g_ready;
}

void e1000_rx_start(void)
{
    if (!g_ready || g_rx_enabled) {
        e1000_debug_state("rx start skipped");
        return;
    }
    for (uint32_t i = 0; i < E1000_RING_SIZE; i++) {
        g_rx_desc[i].address = g_rx_buffer_dma[i].physical_address;
        g_rx_desc[i].length = 0;
        g_rx_desc[i].checksum = 0;
        g_rx_desc[i].status = 0;
        g_rx_desc[i].errors = 0;
        g_rx_desc[i].special = 0;
    }
    g_rx_tail = 0;
    e1000_write32(E1000_REG_RDH, 0);
    e1000_write32(E1000_REG_RDT, E1000_RING_SIZE - 1);
    e1000_write32(E1000_REG_RCTL, E1000_RCTL_EN | E1000_RCTL_UPE | E1000_RCTL_BAM | E1000_RCTL_SECRC);
    g_rx_enabled = true;
    e1000_debug_state("rx started");
}

void e1000_rx_stop(void)
{
    if (!g_ready || !g_rx_enabled) {
        e1000_debug_state("rx stop skipped");
        return;
    }
    e1000_write32(E1000_REG_RCTL, 0);
    g_rx_enabled = false;
    e1000_debug_state("rx stopped");
}

bool e1000_send_frame(const uint8_t *packet, uint16_t length)
{
    uint32_t tail;
    uint32_t next_tail;
    uint16_t wire_length = length < 60 ? 60 : length;

    if (!g_ready) {
        log_write("e1000: tx rejected not ready");
        e1000_debug_state("tx reject");
        return false;
    }
    if (packet == NULL) {
        log_write("e1000: tx rejected null packet");
        e1000_debug_state("tx reject");
        return false;
    }
    if (length > E1000_BUFFER_SIZE || wire_length > E1000_BUFFER_SIZE) {
        log_write("e1000: tx rejected oversized frame");
        e1000_debug_state("tx reject");
        return false;
    }

    tail = g_tx_tail;
    next_tail = (tail + 1) % E1000_RING_SIZE;
    if ((g_tx_desc[tail].status & E1000_DESC_STATUS_DD) == 0) {
        log_write("e1000: tx rejected descriptor busy");
        e1000_debug_state("tx busy");
        return false;
    }

    memset(g_tx_buffer_dma[tail].virtual_address, 0, E1000_BUFFER_SIZE);
    memcpy(g_tx_buffer_dma[tail].virtual_address, packet, length);
    g_tx_desc[tail].address = g_tx_buffer_dma[tail].physical_address;
    g_tx_desc[tail].length = wire_length;
    g_tx_desc[tail].checksum_offset = 0;
    g_tx_desc[tail].command = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    g_tx_desc[tail].status = 0;
    g_tx_desc[tail].checksum_start = 0;
    g_tx_desc[tail].special = 0;
    g_tx_tail = next_tail;
    e1000_write32(E1000_REG_TDT, g_tx_tail);
    g_net->tx_packets++;
    e1000_debug_state("tx submitted");
    return true;
}

void e1000_poll(void (*handler)(const uint8_t *packet, uint16_t length))
{
    while (g_ready && g_rx_enabled && (g_rx_desc[g_rx_tail].status & E1000_DESC_STATUS_DD) != 0) {
        uint16_t length = g_rx_desc[g_rx_tail].length;
        uint8_t *packet = (uint8_t *) g_rx_buffer_dma[g_rx_tail].virtual_address;
        uint32_t done = g_rx_tail;

        g_net->rx_packets++;
        /* Log brief packet summary: ethertype and first bytes (up to 16) */
        if (length >= 14) {
            uint16_t ether_type = (uint16_t) ((packet[12] << 8) | packet[13]);
            char line[128];
            char tmp[4];
            strcpy(line, "e1000: rx eth=0x");
            e1000_append_hex32(tmp, ether_type);
            strcat(line, tmp + 4); /* use last 4 hex chars */
            strcat(line, " bytes=");
            e1000_append_dec(tmp, length);
            strcat(line, tmp);
            strcat(line, " data=");
            for (uint32_t i = 0; i < 16 && i < (uint32_t) length; i++) {
                char hex[3];
                const char hexchars[] = "0123456789ABCDEF";
                hex[0] = hexchars[(packet[i] >> 4) & 0xF];
                hex[1] = hexchars[packet[i] & 0xF];
                hex[2] = '\0';
                strcat(line, hex);
                if (i < 15 && i < (uint32_t) length - 1) strcat(line, " ");
            }
            log_write(line);
        }
        if (handler != NULL) {
            handler(packet, length);
        }
        g_rx_desc[done].address = g_rx_buffer_dma[done].physical_address;
        g_rx_desc[done].length = 0;
        g_rx_desc[done].checksum = 0;
        g_rx_desc[done].status = 0;
        g_rx_desc[done].errors = 0;
        g_rx_desc[done].special = 0;
        g_rx_tail = (g_rx_tail + 1) % E1000_RING_SIZE;
        e1000_write32(E1000_REG_RDT, done);
    }
}

void e1000_shutdown(void)
{
    if (!g_ready) {
        return;
    }
    e1000_write32(E1000_REG_RCTL, 0);
    e1000_write32(E1000_REG_TCTL, 0);
    g_rx_enabled = false;
    g_ready = false;
}
