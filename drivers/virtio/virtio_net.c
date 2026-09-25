#include "common.h"
#include "dma.h"
#include "kernel.h"
#include "mmu.h"
#include "pci.h"
#include "virtio.h"

#define VIRTIO_PCI_VENDOR                 0x1AF4U
#define VIRTIO_LEGACY_NETWORK             0x1000U
#define VIRTIO_MODERN_NETWORK             0x1041U

#define PCI_COMMAND_OFFSET                0x04U
#define PCI_COMMAND_IO                    0x0001U
#define PCI_COMMAND_MEMORY                0x0002U
#define PCI_COMMAND_BUS_MASTER            0x0004U

#define PCI_CAPABILITY_VENDOR             0x09U
#define VIRTIO_PCI_CAP_COMMON_CFG         1U
#define VIRTIO_PCI_CAP_NOTIFY_CFG         2U
#define VIRTIO_PCI_CAP_ISR_CFG            3U
#define VIRTIO_PCI_CAP_DEVICE_CFG         4U

#define VIRTIO_LEGACY_DEVICE_FEATURES     0x00U
#define VIRTIO_LEGACY_DRIVER_FEATURES     0x04U
#define VIRTIO_LEGACY_QUEUE_ADDRESS       0x08U
#define VIRTIO_LEGACY_QUEUE_SIZE          0x0CU
#define VIRTIO_LEGACY_QUEUE_SELECT         0x0EU
#define VIRTIO_LEGACY_QUEUE_NOTIFY         0x10U
#define VIRTIO_LEGACY_STATUS               0x12U
#define VIRTIO_LEGACY_ISR                  0x13U
#define VIRTIO_LEGACY_DEVICE_CONFIG        0x14U

#define VIRTIO_COMMON_DEVICE_FEATURE_SELECT 0x00U
#define VIRTIO_COMMON_DEVICE_FEATURE        0x04U
#define VIRTIO_COMMON_DRIVER_FEATURE_SELECT 0x08U
#define VIRTIO_COMMON_DRIVER_FEATURE        0x0CU
#define VIRTIO_COMMON_NUM_QUEUES            0x12U
#define VIRTIO_COMMON_DEVICE_STATUS         0x14U
#define VIRTIO_COMMON_QUEUE_SELECT          0x16U
#define VIRTIO_COMMON_QUEUE_SIZE            0x18U
#define VIRTIO_COMMON_QUEUE_ENABLE          0x1CU
#define VIRTIO_COMMON_QUEUE_NOTIFY_OFFSET    0x1EU
#define VIRTIO_COMMON_QUEUE_DESC            0x20U
#define VIRTIO_COMMON_QUEUE_DRIVER          0x28U
#define VIRTIO_COMMON_QUEUE_DEVICE          0x30U

#define VIRTIO_STATUS_ACKNOWLEDGE           0x01U
#define VIRTIO_STATUS_DRIVER                0x02U
#define VIRTIO_STATUS_DRIVER_OK             0x04U
#define VIRTIO_STATUS_FEATURES_OK           0x08U
#define VIRTIO_STATUS_FAILED                0x80U

#define VIRTIO_NET_F_MAC                    (1ULL << 5)
#define VIRTIO_F_VERSION_1                  (1ULL << 32)

#define VIRTQ_DESC_F_WRITE                  0x0002U
#define VIRTIO_RING_ALIGN                   4096U
#define VIRTIO_NET_HEADER_SIZE_LEGACY       10U
#define VIRTIO_NET_HEADER_SIZE_MODERN       12U
#define VIRTIO_NET_BUFFER_SIZE              2048U
#define VIRTIO_NET_QUEUE_COUNT              2U
#define VIRTIO_NET_BUFFER_COUNT             32U
#define VIRTIO_NET_QUEUE_SIZE_MAX           256U
#define VIRTIO_NET_MODERN_BAR_BASE          0xFEA00000ULL
#define VIRTIO_NET_MODERN_BAR_STRIDE        0x00010000ULL

typedef struct __attribute__((packed)) {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
} virtio_net_desc_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t length;
} virtio_net_used_elem_t;

typedef struct {
    dma_buffer_t ring_dma;
    volatile virtio_net_desc_t *desc;
    volatile uint16_t *avail_flags;
    volatile uint16_t *avail_idx;
    volatile uint16_t *avail_ring;
    volatile uint16_t *used_flags;
    volatile uint16_t *used_idx;
    volatile virtio_net_used_elem_t *used_ring;
    uint16_t size;
    uint16_t next_avail;
    uint16_t next_used;
    uint16_t queue_index;
    uint16_t notify_offset;
    uint32_t notify_multiplier;
    uint64_t notify_address;
    bool enabled;
} virtio_net_queue_t;

typedef struct {
    uint8_t cfg_type;
    uint8_t bar;
    uint32_t offset;
    uint32_t length;
    uint32_t notify_multiplier;
} virtio_net_cap_t;

typedef struct {
    bool present;
    bool modern;
    bool legacy;
    bool ready;
    bool rx_started;
    bool link_up;
    uint16_t io_base;
    uint64_t common_base;
    uint64_t notify_base;
    uint64_t device_base;
    uint32_t notify_multiplier;
    uint64_t device_features;
    uint64_t driver_features;
    uint16_t header_size;
    pci_device_info_t pci;
    virtio_net_queue_t rx;
    virtio_net_queue_t tx;
    dma_buffer_t rx_buffer[VIRTIO_NET_BUFFER_COUNT];
    dma_buffer_t tx_buffer[VIRTIO_NET_BUFFER_COUNT];
    bool tx_inflight[VIRTIO_NET_BUFFER_COUNT];
    uint32_t tx_packets;
    uint32_t rx_packets;
    net_info_t *net;
    uint8_t mac[6];
    char status[64];
} virtio_net_device_t;

static virtio_net_device_t g_virtio_net;

static bool virtio_net_identity_mapped(uint64_t address)
{
    return address < 0x40000000ULL ||
           (address >= 0xC0000000ULL && address < 0x200000000ULL);
}

static uint32_t *virtio_net_raw_bar_ptr(uint8_t bar)
{
    switch (bar) {
    case 0:
        return &g_virtio_net.pci.bar0;
    case 1:
        return &g_virtio_net.pci.bar1;
    case 2:
        return &g_virtio_net.pci.bar2;
    case 3:
        return &g_virtio_net.pci.bar3;
    case 4:
        return &g_virtio_net.pci.bar4;
    case 5:
        return &g_virtio_net.pci.bar5;
    default:
        return NULL;
    }
}

static bool virtio_net_assign_modern_bar(uint8_t bar)
{
    uint32_t *raw_bar;
    uint32_t raw;
    uint32_t flags;
    uint64_t base;

    if (bar >= 6U) {
        return false;
    }
    raw_bar = virtio_net_raw_bar_ptr(bar);
    if (raw_bar == NULL) {
        return false;
    }
    raw = *raw_bar;
    if ((raw & 1U) != 0) {
        return false;
    }
    flags = raw & 0x0FU;
    base = VIRTIO_NET_MODERN_BAR_BASE +
           (uint64_t) g_virtio_net.pci.slot * VIRTIO_NET_MODERN_BAR_STRIDE +
           (uint64_t) g_virtio_net.pci.func * 0x1000ULL;
    pci_config_write32(g_virtio_net.pci.bus,
                       g_virtio_net.pci.slot,
                       g_virtio_net.pci.func,
                       (uint8_t) (0x10U + bar * 4U),
                       (uint32_t) base | flags);
    *raw_bar = (uint32_t) base | flags;
    g_virtio_net.pci.bar_address[bar] = base;
    if ((flags & 0x06U) == 0x04U && bar < 5U) {
        uint32_t *high_bar = virtio_net_raw_bar_ptr((uint8_t) (bar + 1U));

        pci_config_write32(g_virtio_net.pci.bus,
                           g_virtio_net.pci.slot,
                           g_virtio_net.pci.func,
                           (uint8_t) (0x10U + (bar + 1U) * 4U),
                           0);
        if (high_bar != NULL) {
            *high_bar = 0;
        }
    }
    kernel_log_hex_u32("virtio-net: assigned modern BAR=", bar);
    kernel_log_hex_u32("virtio-net: assigned modern base=", (uint32_t) base);
    return true;
}

static uint64_t virtio_net_align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1ULL) & ~(align - 1ULL);
}

static bool virtio_net_supported_id(uint16_t device_id)
{
    return device_id == VIRTIO_LEGACY_NETWORK ||
           device_id == VIRTIO_MODERN_NETWORK;
}

static uint8_t virtio_net_mmio_read8(uint64_t address)
{
    return *(volatile uint8_t *) (uintptr_t) address;
}

static uint16_t virtio_net_mmio_read16(uint64_t address)
{
    return *(volatile uint16_t *) (uintptr_t) address;
}

static uint32_t virtio_net_mmio_read32(uint64_t address)
{
    return *(volatile uint32_t *) (uintptr_t) address;
}

__attribute__((unused))
static uint64_t virtio_net_mmio_read64(uint64_t address)
{
    uint64_t low = virtio_net_mmio_read32(address);
    uint64_t high = virtio_net_mmio_read32(address + 4);

    return low | (high << 32);
}

static void virtio_net_mmio_write8(uint64_t address, uint8_t value)
{
    *(volatile uint8_t *) (uintptr_t) address = value;
}

static void virtio_net_mmio_write16(uint64_t address, uint16_t value)
{
    *(volatile uint16_t *) (uintptr_t) address = value;
}

static void virtio_net_mmio_write32(uint64_t address, uint32_t value)
{
    *(volatile uint32_t *) (uintptr_t) address = value;
}

static void virtio_net_mmio_write64(uint64_t address, uint64_t value)
{
    virtio_net_mmio_write32(address, (uint32_t) value);
    virtio_net_mmio_write32(address + 4, (uint32_t) (value >> 32));
}

static void virtio_net_barrier(void)
{
    asm volatile ("mfence" ::: "memory");
}

static uint16_t virtio_net_header_size(void)
{
    return g_virtio_net.header_size != 0 ?
           g_virtio_net.header_size :
           VIRTIO_NET_HEADER_SIZE_LEGACY;
}

static void virtio_net_set_status(uint8_t status)
{
    if (g_virtio_net.legacy) {
        outb((uint16_t) (g_virtio_net.io_base + VIRTIO_LEGACY_STATUS), status);
    } else if (g_virtio_net.common_base != 0) {
        virtio_net_mmio_write8(g_virtio_net.common_base + VIRTIO_COMMON_DEVICE_STATUS, status);
    }
}

static uint8_t virtio_net_get_status(void)
{
    if (g_virtio_net.legacy) {
        return inb((uint16_t) (g_virtio_net.io_base + VIRTIO_LEGACY_STATUS));
    }
    if (g_virtio_net.common_base != 0) {
        return virtio_net_mmio_read8(g_virtio_net.common_base + VIRTIO_COMMON_DEVICE_STATUS);
    }
    return 0;
}

static void virtio_net_reset(void)
{
    if (g_virtio_net.legacy) {
        outb((uint16_t) (g_virtio_net.io_base + VIRTIO_LEGACY_STATUS), 0);
    } else if (g_virtio_net.common_base != 0) {
        virtio_net_mmio_write8(g_virtio_net.common_base + VIRTIO_COMMON_DEVICE_STATUS, 0);
    }
}

static void virtio_net_append_cap(const pci_device_info_t *info,
                                  uint8_t pointer,
                                  virtio_net_cap_t *out_cap)
{
    uint8_t cap_length;
    uint8_t cfg_type;

    if (info == NULL || out_cap == NULL || pointer < 0x40U) {
        return;
    }
    cap_length = pci_config_read8(info->bus, info->slot, info->func, (uint8_t) (pointer + 2U));
    cfg_type = pci_config_read8(info->bus, info->slot, info->func, (uint8_t) (pointer + 3U));
    if (pci_config_read8(info->bus, info->slot, info->func, pointer) != PCI_CAPABILITY_VENDOR ||
        cap_length < 16U) {
        return;
    }
    out_cap->cfg_type = cfg_type;
    out_cap->bar = pci_config_read8(info->bus, info->slot, info->func, (uint8_t) (pointer + 4U));
    out_cap->offset = pci_config_read32(info->bus, info->slot, info->func, (uint8_t) (pointer + 8U));
    out_cap->length = pci_config_read32(info->bus, info->slot, info->func, (uint8_t) (pointer + 12U));
    if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG && cap_length >= 20U) {
        out_cap->notify_multiplier =
            pci_config_read32(info->bus, info->slot, info->func, (uint8_t) (pointer + 16U));
    }
}

static bool virtio_net_find_caps(const pci_device_info_t *info,
                                 virtio_net_cap_t *common,
                                 virtio_net_cap_t *notify,
                                 virtio_net_cap_t *device)
{
    uint8_t pointer;
    bool common_found = false;
    bool notify_found = false;
    bool device_found = false;

    if (info == NULL || common == NULL || notify == NULL || device == NULL) {
        return false;
    }
    memset(common, 0, sizeof(*common));
    memset(notify, 0, sizeof(*notify));
    memset(device, 0, sizeof(*device));
    pointer = info->capability_pointer;
    for (uint8_t i = 0; i < 48U && pointer >= 0x40U; i++) {
        virtio_net_cap_t cap;
        uint8_t next;

        memset(&cap, 0, sizeof(cap));
        virtio_net_append_cap(info, pointer, &cap);
        if (cap.cfg_type == VIRTIO_PCI_CAP_COMMON_CFG) {
            *common = cap;
            common_found = true;
        } else if (cap.cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
            *notify = cap;
            notify_found = true;
        } else if (cap.cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG) {
            *device = cap;
            device_found = true;
        }
        next = pci_config_read8(info->bus, info->slot, info->func, (uint8_t) (pointer + 1U));
        if (next == pointer) {
            break;
        }
        pointer = (uint8_t) (next & 0xFCU);
    }
    return common_found && notify_found && device_found;
}

static bool virtio_net_setup_queue_layout(virtio_net_queue_t *queue, uint16_t size)
{
    uint64_t desc_bytes;
    uint64_t avail_bytes;
    uint64_t used_offset;
    uint64_t used_bytes;
    uint64_t total_size;
    uint8_t *base;

    if (queue == NULL || size == 0 || size > VIRTIO_NET_QUEUE_SIZE_MAX) {
        return false;
    }
    desc_bytes = (uint64_t) sizeof(virtio_net_desc_t) * size;
    avail_bytes = 4ULL + (uint64_t) sizeof(uint16_t) * size;
    used_offset = virtio_net_align_up(desc_bytes + avail_bytes, VIRTIO_RING_ALIGN);
    used_bytes = 4ULL + (uint64_t) sizeof(virtio_net_used_elem_t) * size;
    total_size = used_offset + used_bytes;
    memset(queue, 0, sizeof(*queue));
    if (!dma_alloc(total_size, VIRTIO_RING_ALIGN, 0xFFFFFFFFULL, &queue->ring_dma)) {
        return false;
    }

    base = (uint8_t *) queue->ring_dma.virtual_address;
    queue->desc = (volatile virtio_net_desc_t *) base;
    queue->avail_flags = (volatile uint16_t *) (base + desc_bytes);
    queue->avail_idx = queue->avail_flags + 1;
    queue->avail_ring = queue->avail_idx + 1;
    queue->used_flags = (volatile uint16_t *) (base + used_offset);
    queue->used_idx = queue->used_flags + 1;
    queue->used_ring = (volatile virtio_net_used_elem_t *) (queue->used_idx + 1);
    queue->size = size;
    queue->next_avail = 0;
    queue->next_used = 0;
    *queue->avail_flags = 0;
    *queue->avail_idx = 0;
    *queue->used_flags = 0;
    *queue->used_idx = 0;
    memset((void *) queue->desc, 0, (uint32_t) desc_bytes);
    memset((void *) queue->avail_ring, 0, (uint32_t) ((uint8_t *) queue->used_flags -
                                                       (uint8_t *) queue->avail_ring));
    memset((void *) queue->used_ring, 0, (uint32_t) (used_bytes - 4U));
    return true;
}

static void virtio_net_prepare_rx_queue(void)
{
    uint16_t buffer_count = g_virtio_net.rx.size < VIRTIO_NET_BUFFER_COUNT ?
                            g_virtio_net.rx.size : VIRTIO_NET_BUFFER_COUNT;

    for (uint16_t i = 0; i < buffer_count; i++) {
        g_virtio_net.rx.desc[i].address = g_virtio_net.rx_buffer[i].physical_address;
        g_virtio_net.rx.desc[i].length = VIRTIO_NET_BUFFER_SIZE;
        g_virtio_net.rx.desc[i].flags = VIRTQ_DESC_F_WRITE;
        g_virtio_net.rx.desc[i].next = 0;
        g_virtio_net.rx.avail_ring[i] = i;
    }
    g_virtio_net.rx.next_avail = buffer_count;
    *g_virtio_net.rx.avail_idx = buffer_count;
}

static uint16_t virtio_net_modern_queue_size(uint16_t device_size)
{
    if (device_size <= VIRTIO_NET_QUEUE_SIZE_MAX) {
        return device_size;
    }
    return VIRTIO_NET_QUEUE_SIZE_MAX;
}

static bool virtio_net_alloc_buffers(void)
{
    uint16_t rx_count = g_virtio_net.rx.size < VIRTIO_NET_BUFFER_COUNT ?
                        g_virtio_net.rx.size : VIRTIO_NET_BUFFER_COUNT;
    uint16_t tx_count = g_virtio_net.tx.size < VIRTIO_NET_BUFFER_COUNT ?
                        g_virtio_net.tx.size : VIRTIO_NET_BUFFER_COUNT;

    for (uint16_t i = 0; i < rx_count; i++) {
        if (!dma_alloc(VIRTIO_NET_BUFFER_SIZE, 16, 0xFFFFFFFFULL, &g_virtio_net.rx_buffer[i])) {
            return false;
        }
    }
    for (uint16_t i = 0; i < tx_count; i++) {
        if (!dma_alloc(VIRTIO_NET_BUFFER_SIZE, 16, 0xFFFFFFFFULL, &g_virtio_net.tx_buffer[i])) {
            return false;
        }
    }
    memset(g_virtio_net.tx_inflight, 0, sizeof(g_virtio_net.tx_inflight));
    return true;
}

static bool virtio_net_legacy_select_queue(uint16_t queue_index, uint16_t *out_size)
{
    uint16_t size;

    if (out_size == NULL || g_virtio_net.io_base == 0) {
        return false;
    }
    outw((uint16_t) (g_virtio_net.io_base + VIRTIO_LEGACY_QUEUE_SELECT), queue_index);
    size = inw((uint16_t) (g_virtio_net.io_base + VIRTIO_LEGACY_QUEUE_SIZE));
    *out_size = size;
    return *out_size >= 2U && *out_size <= VIRTIO_NET_QUEUE_SIZE_MAX;
}

static bool virtio_net_legacy_setup_queue(virtio_net_queue_t *queue, uint16_t queue_index)
{
    uint16_t device_size;

    if (!virtio_net_legacy_select_queue(queue_index, &device_size)) {
        kernel_log_hex_u32("virtio-net: legacy queue unavailable=", queue_index);
        return false;
    }
    if (!virtio_net_setup_queue_layout(queue, device_size)) {
        kernel_log_hex_u32("virtio-net: legacy queue layout failed=", device_size);
        return false;
    }
    queue->queue_index = queue_index;
    if ((queue->ring_dma.physical_address & (VIRTIO_RING_ALIGN - 1ULL)) != 0 ||
        (queue->ring_dma.physical_address >> 32) != 0) {
        kernel_log_hex_u32("virtio-net: legacy queue address invalid=",
                           (uint32_t) queue->ring_dma.physical_address);
        return false;
    }
    outw((uint16_t) (g_virtio_net.io_base + VIRTIO_LEGACY_QUEUE_SELECT), queue_index);
    outl((uint16_t) (g_virtio_net.io_base + VIRTIO_LEGACY_QUEUE_ADDRESS),
         (uint32_t) (queue->ring_dma.physical_address >> 12));
    queue->enabled = true;
    return true;
}

static bool virtio_net_modern_setup_queue(virtio_net_queue_t *queue, uint16_t queue_index)
{
    uint16_t device_size;
    uint64_t desc_address;
    uint64_t avail_address;
    uint64_t used_address;
    uint64_t avail_offset;
    uint64_t used_offset;

    if (g_virtio_net.common_base == 0 || g_virtio_net.notify_base == 0) {
        log_write("virtio-net: modern common/notify base missing");
        return false;
    }
    virtio_net_mmio_write16(g_virtio_net.common_base + VIRTIO_COMMON_QUEUE_SELECT, queue_index);
    device_size = virtio_net_mmio_read16(g_virtio_net.common_base + VIRTIO_COMMON_QUEUE_SIZE);
    device_size = virtio_net_modern_queue_size(device_size);
    if (device_size < 2U) {
        kernel_log_hex_u32("virtio-net: modern queue size invalid=", device_size);
        return false;
    }
    virtio_net_mmio_write16(g_virtio_net.common_base + VIRTIO_COMMON_QUEUE_SIZE, device_size);
    if (!virtio_net_setup_queue_layout(queue, device_size)) {
        kernel_log_hex_u32("virtio-net: modern queue layout failed=", device_size);
        return false;
    }

    avail_offset = (uint64_t) sizeof(virtio_net_desc_t) * device_size;
    used_offset = virtio_net_align_up(avail_offset + 4ULL +
                                      (uint64_t) sizeof(uint16_t) * device_size,
                                      VIRTIO_RING_ALIGN);
    desc_address = queue->ring_dma.physical_address;
    avail_address = queue->ring_dma.physical_address + avail_offset;
    used_address = queue->ring_dma.physical_address + used_offset;
    queue->queue_index = queue_index;
    queue->notify_offset =
        virtio_net_mmio_read16(g_virtio_net.common_base + VIRTIO_COMMON_QUEUE_NOTIFY_OFFSET);
    queue->notify_multiplier = g_virtio_net.notify_multiplier;
    queue->notify_address = g_virtio_net.notify_base +
                            (uint64_t) queue->notify_offset * queue->notify_multiplier;
    virtio_net_mmio_write64(g_virtio_net.common_base + VIRTIO_COMMON_QUEUE_DESC, desc_address);
    virtio_net_mmio_write64(g_virtio_net.common_base + VIRTIO_COMMON_QUEUE_DRIVER, avail_address);
    virtio_net_mmio_write64(g_virtio_net.common_base + VIRTIO_COMMON_QUEUE_DEVICE, used_address);
    virtio_net_mmio_write16(g_virtio_net.common_base + VIRTIO_COMMON_QUEUE_ENABLE, 1);
    queue->enabled = virtio_net_mmio_read16(g_virtio_net.common_base +
                                             VIRTIO_COMMON_QUEUE_ENABLE) != 0;
    if (!queue->enabled) {
        kernel_log_hex_u32("virtio-net: modern queue enable failed=", queue_index);
    }
    return queue->enabled;
}

static void virtio_net_notify(virtio_net_queue_t *queue)
{
    if (queue == NULL || !queue->enabled) {
        return;
    }
    if (g_virtio_net.legacy) {
        outw((uint16_t) (g_virtio_net.io_base + VIRTIO_LEGACY_QUEUE_NOTIFY),
             queue->queue_index);
    } else {
        virtio_net_mmio_write16(queue->notify_address, queue->queue_index);
    }
}

static bool virtio_net_negotiate_legacy(void)
{
    uint32_t device_features;
    uint32_t driver_features;
    uint8_t status;

    device_features = inl((uint16_t) (g_virtio_net.io_base + VIRTIO_LEGACY_DEVICE_FEATURES));
    driver_features = device_features & (uint32_t) VIRTIO_NET_F_MAC;
    g_virtio_net.device_features = device_features;
    g_virtio_net.driver_features = driver_features;
    virtio_net_reset();
    virtio_net_set_status(VIRTIO_STATUS_ACKNOWLEDGE);
    virtio_net_set_status(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    outl((uint16_t) (g_virtio_net.io_base + VIRTIO_LEGACY_DRIVER_FEATURES), driver_features);
    status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
    virtio_net_set_status(status);
    if ((virtio_net_get_status() & VIRTIO_STATUS_FAILED) != 0) {
        return false;
    }
    return true;
}

static bool virtio_net_negotiate_modern(void)
{
    uint64_t device_features;
    uint64_t driver_features;
    uint8_t status;

    virtio_net_mmio_write32(g_virtio_net.common_base + VIRTIO_COMMON_DEVICE_FEATURE_SELECT, 0);
    device_features = virtio_net_mmio_read32(g_virtio_net.common_base + VIRTIO_COMMON_DEVICE_FEATURE);
    virtio_net_mmio_write32(g_virtio_net.common_base + VIRTIO_COMMON_DEVICE_FEATURE_SELECT, 1);
    device_features |= (uint64_t) virtio_net_mmio_read32(g_virtio_net.common_base +
                                                          VIRTIO_COMMON_DEVICE_FEATURE) << 32;
    driver_features = device_features & (VIRTIO_NET_F_MAC | VIRTIO_F_VERSION_1);
    g_virtio_net.device_features = device_features;
    g_virtio_net.driver_features = driver_features;
    virtio_net_reset();
    virtio_net_set_status(VIRTIO_STATUS_ACKNOWLEDGE);
    virtio_net_set_status(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    virtio_net_mmio_write32(g_virtio_net.common_base + VIRTIO_COMMON_DRIVER_FEATURE_SELECT, 0);
    virtio_net_mmio_write32(g_virtio_net.common_base + VIRTIO_COMMON_DRIVER_FEATURE,
                             (uint32_t) driver_features);
    virtio_net_mmio_write32(g_virtio_net.common_base + VIRTIO_COMMON_DRIVER_FEATURE_SELECT, 1);
    virtio_net_mmio_write32(g_virtio_net.common_base + VIRTIO_COMMON_DRIVER_FEATURE,
                             (uint32_t) (driver_features >> 32));
    status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
    virtio_net_set_status(status);
    if ((virtio_net_get_status() & VIRTIO_STATUS_FEATURES_OK) == 0) {
        return false;
    }
    return true;
}

static void virtio_net_read_mac(void)
{
    if (g_virtio_net.legacy) {
        for (uint8_t i = 0; i < 6; i++) {
            g_virtio_net.mac[i] =
                inb((uint16_t) (g_virtio_net.io_base + VIRTIO_LEGACY_DEVICE_CONFIG + i));
        }
    } else {
        for (uint8_t i = 0; i < 6; i++) {
            g_virtio_net.mac[i] =
                virtio_net_mmio_read8(g_virtio_net.device_base + i);
        }
    }
}

static bool virtio_net_find_device_callback(const pci_device_info_t *info, void *ctx)
{
    if (info != NULL && info->vendor_id == VIRTIO_PCI_VENDOR &&
        virtio_net_supported_id(info->device_id)) {
        g_virtio_net.pci = *info;
        *(bool *) ctx = true;
        return false;
    }
    return true;
}

static bool virtio_net_prepare_modern(void)
{
    virtio_net_cap_t common;
    virtio_net_cap_t notify;
    virtio_net_cap_t device;
    uint64_t common_bar;
    uint64_t notify_bar;
    uint64_t device_bar;
    uint16_t command;

    if (!virtio_net_find_caps(&g_virtio_net.pci, &common, &notify, &device) ||
        common.bar >= 6U || notify.bar >= 6U || device.bar >= 6U) {
        log_write("virtio-net: modern capabilities unavailable");
        return false;
    }
    command = pci_config_read16(g_virtio_net.pci.bus,
                                g_virtio_net.pci.slot,
                                g_virtio_net.pci.func,
                                PCI_COMMAND_OFFSET);
    command |= PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;
    pci_config_write16(g_virtio_net.pci.bus,
                       g_virtio_net.pci.slot,
                       g_virtio_net.pci.func,
                       PCI_COMMAND_OFFSET,
                       command);
    common_bar = g_virtio_net.pci.bar_address[common.bar];
    notify_bar = g_virtio_net.pci.bar_address[notify.bar];
    device_bar = g_virtio_net.pci.bar_address[device.bar];
    if (common_bar == 0 && !virtio_net_assign_modern_bar(common.bar)) {
        log_write("virtio-net: common BAR assignment failed");
        return false;
    }
    if (notify_bar == 0 && notify.bar != common.bar &&
        !virtio_net_assign_modern_bar(notify.bar)) {
        log_write("virtio-net: notify BAR assignment failed");
        return false;
    }
    if (device_bar == 0 && device.bar != common.bar &&
        device.bar != notify.bar &&
        !virtio_net_assign_modern_bar(device.bar)) {
        log_write("virtio-net: device BAR assignment failed");
        return false;
    }
    common_bar = g_virtio_net.pci.bar_address[common.bar];
    notify_bar = g_virtio_net.pci.bar_address[notify.bar];
    device_bar = g_virtio_net.pci.bar_address[device.bar];
    if (!virtio_net_identity_mapped(common_bar) ||
        !virtio_net_identity_mapped(notify_bar) ||
        !virtio_net_identity_mapped(device_bar)) {
        log_write("virtio-net: modern MMIO BAR unavailable");
        return false;
    }
    g_virtio_net.common_base = common_bar + common.offset;
    g_virtio_net.notify_base = notify_bar + notify.offset;
    g_virtio_net.device_base = device_bar + device.offset;
    g_virtio_net.notify_multiplier = notify.notify_multiplier;
    if (g_virtio_net.notify_multiplier == 0) {
        g_virtio_net.notify_multiplier = 2;
    }
    if (mmu_is_active()) {
        mmu_map_device_identity(common_bar, 0x00100000ULL);
        if (notify_bar != common_bar) {
            mmu_map_device_identity(notify_bar, 0x00100000ULL);
        }
        if (device_bar != common_bar && device_bar != notify_bar) {
            mmu_map_device_identity(device_bar, 0x00100000ULL);
        }
    }
    return virtio_net_negotiate_modern();
}

static bool virtio_net_prepare_legacy(void)
{
    uint16_t io_base = (uint16_t) (g_virtio_net.pci.bar0 & 0xFFFCU);
    uint16_t command;

    if ((g_virtio_net.pci.bar0 & 1U) == 0 ||
        io_base < 0x100U || io_base > 0xFFF0U) {
        return false;
    }
    command = pci_config_read16(g_virtio_net.pci.bus,
                                g_virtio_net.pci.slot,
                                g_virtio_net.pci.func,
                                PCI_COMMAND_OFFSET);
    command |= PCI_COMMAND_IO | PCI_COMMAND_BUS_MASTER;
    pci_config_write16(g_virtio_net.pci.bus,
                       g_virtio_net.pci.slot,
                       g_virtio_net.pci.func,
                       PCI_COMMAND_OFFSET,
                       command);
    g_virtio_net.io_base = io_base;
    return virtio_net_negotiate_legacy();
}

static void virtio_net_mark_failed(const char *status)
{
    virtio_net_set_status(VIRTIO_STATUS_FAILED);
    g_virtio_net.ready = false;
    g_virtio_net.link_up = false;
    strlcpy(g_virtio_net.status, status, sizeof(g_virtio_net.status));
    log_write(g_virtio_net.status);
}

bool virtio_net_init(net_info_t *net, uint8_t mac[6])
{
    bool found = false;
    uint64_t dma_mark;

    memset(&g_virtio_net, 0, sizeof(g_virtio_net));
    strcpy(g_virtio_net.status, "virtio-net: not found");
    if (net == NULL || mac == NULL) {
        return false;
    }
    g_virtio_net.net = net;
    pci_enumerate(virtio_net_find_device_callback, &found);
    if (!found) {
        return false;
    }
    g_virtio_net.present = true;
    g_virtio_net.legacy = g_virtio_net.pci.device_id == VIRTIO_LEGACY_NETWORK;
    g_virtio_net.modern = g_virtio_net.pci.device_id == VIRTIO_MODERN_NETWORK;
    g_virtio_net.header_size = g_virtio_net.modern ?
                               VIRTIO_NET_HEADER_SIZE_MODERN :
                               VIRTIO_NET_HEADER_SIZE_LEGACY;
    if (g_virtio_net.modern) {
        if (!virtio_net_prepare_modern()) {
            virtio_net_mark_failed("virtio-net: modern transport unavailable");
            return false;
        }
    } else if (!virtio_net_prepare_legacy()) {
        virtio_net_mark_failed("virtio-net: legacy transport unavailable");
        return false;
    }
    dma_mark = dma_checkpoint();
    if (g_virtio_net.legacy) {
        if (!virtio_net_legacy_setup_queue(&g_virtio_net.rx, 0) ||
            !virtio_net_legacy_setup_queue(&g_virtio_net.tx, 1)) {
            dma_rewind(dma_mark);
            virtio_net_mark_failed("virtio-net: queue setup failed");
            return false;
        }
    } else {
        if (!virtio_net_modern_setup_queue(&g_virtio_net.rx, 0) ||
            !virtio_net_modern_setup_queue(&g_virtio_net.tx, 1)) {
            dma_rewind(dma_mark);
            virtio_net_mark_failed("virtio-net: modern queue setup failed");
            return false;
        }
    }
    if (!virtio_net_alloc_buffers()) {
        dma_rewind(dma_mark);
        virtio_net_mark_failed("virtio-net: buffer dma allocation failed");
        return false;
    }
    virtio_net_prepare_rx_queue();
    virtio_net_barrier();
    virtio_net_read_mac();
    memcpy(mac, g_virtio_net.mac, sizeof(g_virtio_net.mac));
    strcpy(net->driver, "virtio-net");
    net->io_base = g_virtio_net.io_base;
    net->mmio_base = (uint32_t) g_virtio_net.common_base;
    net->vendor_id = g_virtio_net.pci.vendor_id;
    net->device_id = g_virtio_net.pci.device_id;
    net->bus = g_virtio_net.pci.bus;
    net->slot = g_virtio_net.pci.slot;
    net->func = g_virtio_net.pci.func;
    net->irq = g_virtio_net.pci.interrupt_line;
    net->onboard = true;
    net->connected = true;
    g_virtio_net.link_up = true;
    g_virtio_net.ready = true;
    virtio_net_set_status(VIRTIO_STATUS_ACKNOWLEDGE |
                          VIRTIO_STATUS_DRIVER |
                          VIRTIO_STATUS_FEATURES_OK |
                          VIRTIO_STATUS_DRIVER_OK);
    strcpy(g_virtio_net.status, g_virtio_net.modern ?
           "virtio-net: modern split ring ready" :
           "virtio-net: legacy split ring ready");
    log_write(g_virtio_net.status);
    return true;
}

bool virtio_net_ready(void)
{
    return g_virtio_net.ready;
}

bool virtio_net_link_up(void)
{
    return g_virtio_net.ready && g_virtio_net.link_up;
}

void virtio_net_rx_start(void)
{
    if (!g_virtio_net.ready || g_virtio_net.rx_started) {
        return;
    }
    g_virtio_net.rx_started = true;
    virtio_net_notify(&g_virtio_net.rx);
}

static void virtio_net_reclaim_tx(void)
{
    while (g_virtio_net.tx.next_used != *g_virtio_net.tx.used_idx) {
        uint16_t used_slot = (uint16_t) (g_virtio_net.tx.next_used % g_virtio_net.tx.size);
        uint32_t id = g_virtio_net.tx.used_ring[used_slot].id;

        if (id < VIRTIO_NET_BUFFER_COUNT) {
            g_virtio_net.tx_inflight[id] = false;
        }
        g_virtio_net.tx.next_used++;
    }
}

bool virtio_net_send_frame(const uint8_t *packet, uint16_t length)
{
    uint16_t slot = VIRTIO_NET_BUFFER_COUNT;
    uint16_t wire_length;
    uint16_t avail_slot;
    uint16_t header_size = virtio_net_header_size();

    if (!g_virtio_net.ready || packet == NULL ||
        length > VIRTIO_NET_BUFFER_SIZE - header_size) {
        return false;
    }
    virtio_net_reclaim_tx();
    for (uint16_t i = 0; i < g_virtio_net.tx.size && i < VIRTIO_NET_BUFFER_COUNT; i++) {
        if (!g_virtio_net.tx_inflight[i]) {
            slot = i;
            break;
        }
    }
    if (slot >= g_virtio_net.tx.size || slot >= VIRTIO_NET_BUFFER_COUNT) {
        return false;
    }
    wire_length = length < 60U ? 60U : length;
    memset(g_virtio_net.tx_buffer[slot].virtual_address, 0, VIRTIO_NET_BUFFER_SIZE);
    memcpy((uint8_t *) g_virtio_net.tx_buffer[slot].virtual_address +
           header_size, packet, length);
    g_virtio_net.tx.desc[slot].address = g_virtio_net.tx_buffer[slot].physical_address;
    g_virtio_net.tx.desc[slot].length = header_size + wire_length;
    g_virtio_net.tx.desc[slot].flags = 0;
    g_virtio_net.tx.desc[slot].next = 0;
    avail_slot = (uint16_t) (*g_virtio_net.tx.avail_idx % g_virtio_net.tx.size);
    g_virtio_net.tx.avail_ring[avail_slot] = slot;
    virtio_net_barrier();
    (*g_virtio_net.tx.avail_idx)++;
    virtio_net_barrier();
    g_virtio_net.tx_inflight[slot] = true;
    virtio_net_notify(&g_virtio_net.tx);
    g_virtio_net.tx_packets++;
    if (g_virtio_net.net != NULL) {
        g_virtio_net.net->tx_packets++;
    }
    return true;
}

void virtio_net_poll(void (*handler)(const uint8_t *packet, uint16_t length))
{
    uint16_t header_size = virtio_net_header_size();

    if (!g_virtio_net.ready || !g_virtio_net.rx_started) {
        return;
    }
    virtio_net_reclaim_tx();
    while (g_virtio_net.rx.next_used != *g_virtio_net.rx.used_idx) {
        uint16_t used_slot = (uint16_t) (g_virtio_net.rx.next_used % g_virtio_net.rx.size);
        uint32_t id = g_virtio_net.rx.used_ring[used_slot].id;
        uint32_t used_length = g_virtio_net.rx.used_ring[used_slot].length;
        uint16_t packet_length;

        if (id >= g_virtio_net.rx.size ||
            id >= VIRTIO_NET_BUFFER_COUNT ||
            used_length <= header_size) {
            g_virtio_net.rx.next_used++;
            continue;
        }
        packet_length = (uint16_t) (used_length - header_size);
        if (packet_length > VIRTIO_NET_BUFFER_SIZE - header_size) {
            packet_length = VIRTIO_NET_BUFFER_SIZE - header_size;
        }
        g_virtio_net.rx_packets++;
        if (g_virtio_net.net != NULL) {
            g_virtio_net.net->rx_packets++;
        }
        if (handler != NULL) {
            handler((const uint8_t *) g_virtio_net.rx_buffer[id].virtual_address +
                    header_size,
                    packet_length);
        }
        g_virtio_net.rx.desc[id].address = g_virtio_net.rx_buffer[id].physical_address;
        g_virtio_net.rx.desc[id].length = VIRTIO_NET_BUFFER_SIZE;
        g_virtio_net.rx.desc[id].flags = VIRTQ_DESC_F_WRITE;
        g_virtio_net.rx.avail_ring[*g_virtio_net.rx.avail_idx % g_virtio_net.rx.size] = (uint16_t) id;
        virtio_net_barrier();
        (*g_virtio_net.rx.avail_idx)++;
        g_virtio_net.rx.next_used++;
    }
    virtio_net_barrier();
    virtio_net_notify(&g_virtio_net.rx);
}

void virtio_net_shutdown(void)
{
    if (!g_virtio_net.present) {
        return;
    }
    virtio_net_reset();
    g_virtio_net.ready = false;
    g_virtio_net.rx_started = false;
    g_virtio_net.link_up = false;
    strcpy(g_virtio_net.status, "virtio-net: shutdown");
    log_write(g_virtio_net.status);
}

const char *virtio_net_status(void)
{
    return g_virtio_net.status;
}
