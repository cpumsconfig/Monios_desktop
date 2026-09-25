#include "common.h"
#include "dma.h"
#include "kernel.h"
#include "pcnet.h"

/*
 * AMD PCnet/PCI-II (Am79C970A) ethernet driver.
 *
 * The chip is driven entirely through its 16-bit IO window:
 *   +0x00  RDP   - data port (contents selected by RAP)
 *   +0x02  RAP   - register address port (selects CSR / BCR)
 *   +0x04  RESET - read to pulse the chip reset
 *   +0x10  BDP   - bus-configuration data port (BCR access)
 *
 * Rings and the initialization block live in DMA-able memory.  This
 * driver uses the 32-bit (SWSTYLE-1) descriptor layout that the
 * Am79C970A always uses on PCI.
 */

#define PCI_VENDOR_AMD            0x1022
#define PCI_VENDOR_AMD_ALT        0x1023
#define PCI_DEVICE_PCNET          0x2000
#define PCI_COMMAND_OFFSET        0x04
#define PCI_COMMAND_IO            0x0001
#define PCI_COMMAND_BUS_MASTER    0x0004

#define PCNET_RDP                 0x00
#define PCNET_RAP                 0x02
#define PCNET_RESET_PORT          0x04
#define PCNET_BDP                 0x10

/* CSR0 - main status / command register */
#define CSR0_INIT                 0x0001
#define CSR0_STOP                 0x0002
#define CSR0_STRT                 0x0004
#define CSR0_IDON                 0x0400
#define CSR0_RINT                 0x0100
#define CSR0_TINT                 0x0200
#define CSR0_INTR                 0x8000

/* CSR3 - interrupt mask (all masked: we poll) */
#define CSR3_DEFAULT              0x0000

/* Descriptor status bits */
#define PCNET_OWN                 0x80
#define PCNET_DESC_STP            0x04
#define PCNET_DESC_ENP            0x02

/* Initialization block (32-bit style).  36 useful bytes, padded to 64. */
typedef struct {
    uint16_t mode;          /* 0x00 */
    uint16_t rlen_tlen;     /* 0x02 */
    uint32_t rx_ring_phys;  /* 0x04 */
    uint32_t tx_ring_phys;  /* 0x08 */
    uint8_t  phys_addr[8];  /* 0x0C */
    uint32_t logical[2];    /* 0x14 */
    uint32_t rx_ring_high;  /* 0x1C */
    uint32_t tx_ring_high;  /* 0x20 */
} __attribute__((packed)) pcnet_init_block_t;

/* Ring descriptor (16 bytes, 32-bit style). */
typedef struct {
    uint32_t base;          /* 0x00 buffer physical address */
    uint16_t bcnt;          /* 0x04 negative buffer size (rx) / length (tx) */
    uint8_t  status;        /* 0x06 OWN | STP | ENP | ERR */
    uint8_t  unused0;        /* 0x07 */
    uint16_t msg_len;       /* 0x08 received byte count */
    uint16_t errors;        /* 0x0A */
    uint32_t unused1;        /* 0x0C */
} __attribute__((packed)) pcnet_desc_t;

static pcnet_info_t g_pcnet_info;

static dma_buffer_t g_init_block_dma;
static dma_buffer_t g_rx_ring_dma;
static dma_buffer_t g_tx_ring_dma;
static dma_buffer_t g_rx_buf_dma[PCNET_RING_SIZE];
static dma_buffer_t g_tx_buf_dma[PCNET_RING_SIZE];

static volatile pcnet_init_block_t *g_init_block;
static volatile pcnet_desc_t *g_rx_ring;
static volatile pcnet_desc_t *g_tx_ring;

static uint32_t g_rx_ptr;
static uint32_t g_tx_ptr;
static bool g_started;

static uint16_t pcnet_read_csr(uint16_t reg)
{
    outw((uint16_t) (g_pcnet_info.io_base + PCNET_RAP), reg);
    return inw((uint16_t) (g_pcnet_info.io_base + PCNET_RDP));
}

static void pcnet_write_csr(uint16_t reg, uint16_t value)
{
    outw((uint16_t) (g_pcnet_info.io_base + PCNET_RAP), reg);
    outw((uint16_t) (g_pcnet_info.io_base + PCNET_RDP), value);
}

static void pcnet_set_mac_text(const uint8_t mac[6])
{
    static const char hex[] = "0123456789ABCDEF";

    for (uint32_t i = 0; i < 6; i++) {
        g_pcnet_info.mac_text[i * 3] = hex[(mac[i] >> 4) & 0xF];
        g_pcnet_info.mac_text[i * 3 + 1] = hex[mac[i] & 0xF];
        if (i < 5) {
            g_pcnet_info.mac_text[i * 3 + 2] = ':';
        }
    }
    g_pcnet_info.mac_text[17] = '\0';
}

bool pcnet_supported(const pci_device_info_t *info)
{
    if (info == NULL) {
        return false;
    }
    if ((info->vendor_id != PCI_VENDOR_AMD) &&
        (info->vendor_id != PCI_VENDOR_AMD_ALT)) {
        return false;
    }
    return info->device_id == PCI_DEVICE_PCNET;
}

static bool pcnet_probe_collect(const pci_device_info_t *info, void *ctx)
{
    pci_device_info_t *found = (pci_device_info_t *) ctx;

    if (pcnet_supported(info)) {
        *found = *info;
        return false;
    }
    return true;
}

bool pcnet_probe(pci_device_info_t *out_info)
{
    pci_device_info_t found;

    memset(&found, 0, sizeof(found));
    if (out_info == NULL) {
        out_info = &found;
    }
    pci_enumerate(pcnet_probe_collect, out_info);
    if (!pcnet_supported(out_info)) {
        log_write("pcnet: not found");
        return false;
    }
    log_write("pcnet: device found");
    return true;
}

static void pcnet_reset_hw(void)
{
    /* Reading the RESET port pulses the chip's internal reset. */
    (void) inb((uint16_t) (g_pcnet_info.io_base + PCNET_RESET_PORT));
    for (uint32_t i = 0; i < 10000; i++) {
        io_wait();
    }
}

static void pcnet_stop(void)
{
    pcnet_write_csr(0, CSR0_STOP);
    /* wait until the chip leaves RUN state */
    for (uint32_t i = 0; i < 100000; i++) {
        if ((pcnet_read_csr(0) & (CSR0_RINT | CSR0_TINT)) == 0) {
            break;
        }
        io_wait();
    }
}

static bool pcnet_read_hw_mac(uint8_t mac[6])
{
    /* After STOP, the low six IO bytes alias the station PROM. */
    for (uint32_t i = 0; i < 6; i++) {
        mac[i] = inb((uint16_t) (g_pcnet_info.io_base + i));
    }
    bool all_zero = true;
    bool all_ff = true;
    for (uint32_t i = 0; i < 6; i++) {
        all_zero = all_zero && mac[i] == 0;
        all_ff = all_ff && mac[i] == 0xFF;
    }
    return !(all_zero || all_ff);
}

static bool pcnet_program_rings(void)
{
    uint64_t dma_mark = dma_checkpoint();

    if (!dma_alloc(sizeof(pcnet_init_block_t), 4096, 0xFFFFFFFFu, &g_init_block_dma) ||
        !dma_alloc(sizeof(pcnet_desc_t) * PCNET_RING_SIZE, 4096, 0xFFFFFFFFu, &g_rx_ring_dma) ||
        !dma_alloc(sizeof(pcnet_desc_t) * PCNET_RING_SIZE, 4096, 0xFFFFFFFFu, &g_tx_ring_dma)) {
        log_write("pcnet: ring dma alloc failed");
        goto fail;
    }
    g_init_block = (volatile pcnet_init_block_t *) g_init_block_dma.virtual_address;
    g_rx_ring = (volatile pcnet_desc_t *) g_rx_ring_dma.virtual_address;
    g_tx_ring = (volatile pcnet_desc_t *) g_tx_ring_dma.virtual_address;

    for (uint32_t i = 0; i < PCNET_RING_SIZE; i++) {
        if (!dma_alloc(PCNET_BUFFER_SIZE, PCNET_BUFFER_SIZE, 0xFFFFFFFFu, &g_rx_buf_dma[i]) ||
            !dma_alloc(PCNET_BUFFER_SIZE, PCNET_BUFFER_SIZE, 0xFFFFFFFFu, &g_tx_buf_dma[i])) {
            log_write("pcnet: buffer dma alloc failed");
            goto fail;
        }
        memset((void *) &g_rx_ring[i], 0, sizeof(g_rx_ring[i]));
        memset((void *) &g_tx_ring[i], 0, sizeof(g_tx_ring[i]));
        g_rx_ring[i].base = (uint32_t) g_rx_buf_dma[i].physical_address;
        g_rx_ring[i].bcnt = (uint16_t) (0 - PCNET_BUFFER_SIZE);
        g_rx_ring[i].status = PCNET_OWN; /* chip owns rx buffer */
        g_tx_ring[i].base = (uint32_t) g_tx_buf_dma[i].physical_address;
        g_tx_ring[i].status = 0;          /* cpu owns tx buffer */
    }
    g_rx_ptr = 0;
    g_tx_ptr = 0;
    return true;

fail:
    dma_rewind(dma_mark);
    g_init_block = NULL;
    g_rx_ring = NULL;
    g_tx_ring = NULL;
    return false;
}

static void pcnet_load_init_block(const uint8_t mac[6])
{
    memset((void *) g_init_block, 0, sizeof(*g_init_block));
    g_init_block->mode = 0x0000;
    /* ring length code: 4 == 16 descriptors */
    g_init_block->rlen_tlen = (uint16_t) ((4u << 12) | (4u << 8));
    g_init_block->rx_ring_phys = (uint32_t) g_rx_ring_dma.physical_address;
    g_init_block->tx_ring_phys = (uint32_t) g_tx_ring_dma.physical_address;
    for (uint32_t i = 0; i < 6; i++) {
        g_init_block->phys_addr[i] = mac[i];
    }
    g_init_block->logical[0] = 0;
    g_init_block->logical[1] = 0;
    g_init_block->rx_ring_high = 0;
    g_init_block->tx_ring_high = 0;
}

bool pcnet_init(const pci_device_info_t *info, net_info_t *net, uint8_t mac[6])
{
    uint16_t command;
    uint16_t csr0;
    uint8_t hw_mac[6];
    uint64_t dma_mark;

    memset(&g_pcnet_info, 0, sizeof(g_pcnet_info));
    strcpy(g_pcnet_info.status, "pcnet: not found");
    if (!pcnet_supported(info) || net == NULL || mac == NULL) {
        return false;
    }

    g_pcnet_info.present = true;
    g_pcnet_info.vendor_id = info->vendor_id;
    g_pcnet_info.device_id = info->device_id;
    g_pcnet_info.bus = info->bus;
    g_pcnet_info.slot = info->slot;
    g_pcnet_info.func = info->func;
    g_pcnet_info.irq = info->interrupt_line;
    g_pcnet_info.io_base = info->bar0 & 0xFFFFFFFCu;
    if ((info->bar0 & 1u) == 0 || g_pcnet_info.io_base == 0 ||
        g_pcnet_info.io_base > 0xFFF0u) {
        strcpy(g_pcnet_info.status, "pcnet: io bar invalid");
        return false;
    }

    command = pci_config_read16(info->bus, info->slot, info->func, PCI_COMMAND_OFFSET);
    command |= PCI_COMMAND_IO | PCI_COMMAND_BUS_MASTER;
    pci_config_write16(info->bus, info->slot, info->func, PCI_COMMAND_OFFSET, command);

    pcnet_reset_hw();
    pcnet_stop();

    if (!pcnet_read_hw_mac(hw_mac)) {
        memcpy(hw_mac, (const uint8_t[]) { 0x54, 0x52, 0x00, 0x12, 0x34, 0x56 }, 6);
        log_write("pcnet: invalid hw mac, using fallback");
    }
    memcpy(mac, hw_mac, 6);
    pcnet_set_mac_text(mac);

    dma_mark = dma_checkpoint();
    if (!pcnet_program_rings()) {
        strcpy(g_pcnet_info.status, "pcnet: ring alloc failed");
        dma_rewind(dma_mark);
        return false;
    }
    pcnet_load_init_block(mac);

    /* Point CSR1/CSR2 at the init block, then trigger INIT. */
    pcnet_write_csr(1, (uint16_t) (uint32_t) g_init_block_dma.physical_address);
    pcnet_write_csr(2, (uint16_t) ((uint32_t) g_init_block_dma.physical_address >> 16));
    pcnet_write_csr(3, CSR3_DEFAULT);
    pcnet_write_csr(0, CSR0_INIT);

    /* wait for IDON (initialization done) */
    csr0 = 0;
    for (uint32_t i = 0; i < 100000; i++) {
        csr0 = pcnet_read_csr(0);
        if (csr0 & CSR0_IDON) {
            break;
        }
        io_wait();
    }
    if ((csr0 & CSR0_IDON) == 0) {
        log_write("pcnet: init timed out");
        strcpy(g_pcnet_info.status, "pcnet: init timeout");
        dma_rewind(dma_mark);
        return false;
    }

    /* Start the chip: enable TX/RX. */
    pcnet_write_csr(0, CSR0_STRT);
    g_started = true;
    g_pcnet_info.io_ready = true;
    net->onboard = true;
    net->io_base = g_pcnet_info.io_base;
    net->mmio_base = 0;
    strcpy(net->driver, "pcnet");
    net->connected = false;
    strcpy(g_pcnet_info.status, "pcnet: running");
    log_write("pcnet: initialized");
    return true;
}

bool pcnet_ready(void)
{
    return g_pcnet_info.present && g_pcnet_info.io_ready && g_started;
}

bool pcnet_link_up(void)
{
    uint16_t csr0;

    if (!pcnet_ready()) {
        return false;
    }
    csr0 = pcnet_read_csr(0);
    return (csr0 & CSR0_TINT) != 0 || g_pcnet_info.link_up;
}

bool pcnet_send(const uint8_t *packet, uint32_t length)
{
    uint32_t tail;

    if (!pcnet_ready() || packet == NULL) {
        return false;
    }
    if (length == 0 || length > PCNET_BUFFER_SIZE) {
        return false;
    }
    tail = g_tx_ptr;
    if (g_tx_ring[tail].status & PCNET_OWN) {
        return false; /* ring slot still owned by chip */
    }
    memcpy(g_tx_buf_dma[tail].virtual_address, packet, length);
    g_tx_ring[tail].base = (uint32_t) g_tx_buf_dma[tail].physical_address;
    g_tx_ring[tail].bcnt = (uint16_t) length;
    g_tx_ring[tail].errors = 0;
    g_tx_ring[tail].msg_len = 0;
    g_tx_ring[tail].status = (uint8_t) (PCNET_OWN | PCNET_DESC_STP | PCNET_DESC_ENP);
    g_tx_ptr = (tail + 1) % PCNET_RING_SIZE;
    g_pcnet_info.tx_packets++;
    return true;
}

bool pcnet_write(const uint8_t *packet, uint32_t length)
{
    return pcnet_send(packet, length);
}

int pcnet_recv(uint8_t *buffer, uint32_t max_len)
{
    uint32_t head;
    uint16_t len;

    if (!pcnet_ready() || buffer == NULL || max_len == 0) {
        return 0;
    }
    head = g_rx_ptr;
    if (g_rx_ring[head].status & PCNET_OWN) {
        return 0; /* chip still owns this slot */
    }
    if ((g_rx_ring[head].status & (PCNET_DESC_STP | PCNET_DESC_ENP)) !=
        (PCNET_DESC_STP | PCNET_DESC_ENP)) {
        /* chained / runt packet: recycle */
        g_rx_ring[head].bcnt = (uint16_t) (0 - PCNET_BUFFER_SIZE);
        g_rx_ring[head].errors = 0;
        g_rx_ring[head].msg_len = 0;
        g_rx_ring[head].status = PCNET_OWN;
        g_rx_ptr = (head + 1) % PCNET_RING_SIZE;
        g_pcnet_info.rx_dropped++;
        return 0;
    }
    len = g_rx_ring[head].msg_len;
    if (len > PCNET_BUFFER_SIZE) {
        len = PCNET_BUFFER_SIZE;
    }
    if (len > max_len) {
        len = (uint16_t) max_len;
    }
    memcpy(buffer, g_rx_buf_dma[head].virtual_address, len);
    g_rx_ring[head].bcnt = (uint16_t) (0 - PCNET_BUFFER_SIZE);
    g_rx_ring[head].errors = 0;
    g_rx_ring[head].msg_len = 0;
    g_rx_ring[head].status = PCNET_OWN;
    g_rx_ptr = (head + 1) % PCNET_RING_SIZE;
    g_pcnet_info.rx_packets++;
    return (int) len;
}

int pcnet_read(uint8_t *buffer, uint32_t max_len)
{
    return pcnet_recv(buffer, max_len);
}

void pcnet_rx_start(void)
{
    if (!pcnet_ready()) {
        return;
    }
    pcnet_write_csr(0, pcnet_read_csr(0) | CSR0_STRT);
}

void pcnet_poll(void)
{
    /* drain tx completions: mark completed descriptors as cpu-owned */
    for (uint32_t i = 0; i < PCNET_RING_SIZE; i++) {
        uint32_t slot = (g_tx_ptr + i) % PCNET_RING_SIZE;
        if ((g_tx_ring[slot].status & PCNET_OWN) == 0) {
            break;
        }
    }
}

uint32_t pcnet_interrupt_handler(void)
{
    uint16_t csr0;

    if (!pcnet_ready()) {
        return 0;
    }
    csr0 = pcnet_read_csr(0); /* read-to-clear on PCnet */
    if (csr0 & CSR0_INTR) {
        /* writing back the read bits clears the asserted interrupts */
        pcnet_write_csr(0, (uint16_t) (csr0 & (CSR0_RINT | CSR0_TINT | CSR0_IDON)));
    }
    return (uint32_t) csr0;
}

void pcnet_shutdown(void)
{
    if (g_pcnet_info.present) {
        if (g_pcnet_info.io_ready) {
            pcnet_stop();
        }
        g_pcnet_info.io_ready = false;
        g_started = false;
        strcpy(g_pcnet_info.status, "pcnet: shutdown");
        log_write(g_pcnet_info.status);
    }
}

const pcnet_info_t *pcnet_info(void)
{
    return &g_pcnet_info;
}

const char *pcnet_status(void)
{
    if (g_pcnet_info.status[0] == '\0') {
        return "pcnet: not initialized";
    }
    return g_pcnet_info.status;
}
