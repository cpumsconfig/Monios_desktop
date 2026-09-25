#include "common.h"
#include "dma.h"
#include "kernel.h"
#include "mmu.h"
#include "pci.h"
#include "virtio.h"

#define VIRTIO_PCI_VENDOR                   0x1AF4U
#define VIRTIO_LEGACY_BLOCK                 0x1001U
#define VIRTIO_MODERN_BLOCK                 0x1042U

#define PCI_COMMAND_OFFSET                  0x04U
#define PCI_COMMAND_IO                      0x0001U
#define PCI_COMMAND_MEMORY                  0x0002U
#define PCI_COMMAND_BUS_MASTER              0x0004U

#define PCI_CAPABILITY_VENDOR               0x09U
#define VIRTIO_PCI_CAP_COMMON_CFG           1U
#define VIRTIO_PCI_CAP_NOTIFY_CFG           2U
#define VIRTIO_PCI_CAP_DEVICE_CFG           4U

#define VIRTIO_LEGACY_DEVICE_FEATURES       0x00U
#define VIRTIO_LEGACY_DRIVER_FEATURES       0x04U
#define VIRTIO_LEGACY_QUEUE_ADDRESS         0x08U
#define VIRTIO_LEGACY_QUEUE_SIZE            0x0CU
#define VIRTIO_LEGACY_QUEUE_SELECT          0x0EU
#define VIRTIO_LEGACY_QUEUE_NOTIFY          0x10U
#define VIRTIO_LEGACY_STATUS                0x12U
#define VIRTIO_LEGACY_DEVICE_CONFIG         0x14U

#define VIRTIO_COMMON_DEVICE_FEATURE_SELECT 0x00U
#define VIRTIO_COMMON_DEVICE_FEATURE        0x04U
#define VIRTIO_COMMON_DRIVER_FEATURE_SELECT 0x08U
#define VIRTIO_COMMON_DRIVER_FEATURE        0x0CU
#define VIRTIO_COMMON_DEVICE_STATUS         0x14U
#define VIRTIO_COMMON_QUEUE_SELECT          0x16U
#define VIRTIO_COMMON_QUEUE_SIZE            0x18U
#define VIRTIO_COMMON_QUEUE_ENABLE          0x1CU
#define VIRTIO_COMMON_QUEUE_NOTIFY_OFFSET   0x1EU
#define VIRTIO_COMMON_QUEUE_DESC            0x20U
#define VIRTIO_COMMON_QUEUE_DRIVER          0x28U
#define VIRTIO_COMMON_QUEUE_DEVICE          0x30U

#define VIRTIO_STATUS_ACKNOWLEDGE           0x01U
#define VIRTIO_STATUS_DRIVER                0x02U
#define VIRTIO_STATUS_DRIVER_OK             0x04U
#define VIRTIO_STATUS_FEATURES_OK           0x08U
#define VIRTIO_STATUS_FAILED                0x80U

#define VIRTIO_F_VERSION_1                  (1ULL << 32)

#define VIRTQ_DESC_F_NEXT                   0x0001U
#define VIRTQ_DESC_F_WRITE                  0x0002U
#define VIRTIO_RING_ALIGN                   4096U
#define VIRTIO_BLK_QUEUE_SIZE_MAX           256U
#define VIRTIO_BLK_MODERN_BAR_BASE          0xFEB00000ULL
#define VIRTIO_BLK_MODERN_BAR_STRIDE        0x00010000ULL

#define VIRTIO_BLK_T_IN                     0U
#define VIRTIO_BLK_T_OUT                    1U
#define VIRTIO_BLK_S_OK                     0U
#define VIRTIO_BLK_S_IOERR                  1U

typedef struct __attribute__((packed)) {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
} virtio_blk_desc_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t length;
} virtio_blk_used_elem_t;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} virtio_blk_request_t;

typedef struct {
    dma_buffer_t ring_dma;
    volatile virtio_blk_desc_t *desc;
    volatile uint16_t *avail_flags;
    volatile uint16_t *avail_idx;
    volatile uint16_t *avail_ring;
    volatile uint16_t *used_flags;
    volatile uint16_t *used_idx;
    volatile virtio_blk_used_elem_t *used_ring;
    uint16_t size;
    uint16_t next_used;
    uint16_t queue_index;
    uint16_t notify_offset;
    uint32_t notify_multiplier;
    uint64_t notify_address;
    bool enabled;
} virtio_blk_queue_t;

typedef struct {
    uint8_t cfg_type;
    uint8_t bar;
    uint32_t offset;
    uint32_t length;
    uint32_t notify_multiplier;
} virtio_blk_cap_t;

typedef struct {
    bool present;
    bool legacy;
    bool modern;
    bool ready;
    bool busy;
    uint16_t io_base;
    uint64_t common_base;
    uint64_t notify_base;
    uint64_t device_base;
    uint32_t notify_multiplier;
    uint64_t device_features;
    uint64_t driver_features;
    pci_device_info_t pci;
    virtio_blk_queue_t queue;
    dma_buffer_t request_dma;
    dma_buffer_t data_dma;
    virtio_blk_info_t info;
} virtio_blk_device_t;

static virtio_blk_device_t g_virtio_blk;

static bool virtio_blk_identity_mapped(uint64_t address)
{
    return address < 0x40000000ULL ||
           (address >= 0xC0000000ULL && address < 0x200000000ULL);
}

static uint32_t *virtio_blk_raw_bar_ptr(uint8_t bar)
{
    switch (bar) {
    case 0:
        return &g_virtio_blk.pci.bar0;
    case 1:
        return &g_virtio_blk.pci.bar1;
    case 2:
        return &g_virtio_blk.pci.bar2;
    case 3:
        return &g_virtio_blk.pci.bar3;
    case 4:
        return &g_virtio_blk.pci.bar4;
    case 5:
        return &g_virtio_blk.pci.bar5;
    default:
        return NULL;
    }
}

static bool virtio_blk_assign_modern_bar(uint8_t bar)
{
    uint32_t *raw_bar;
    uint32_t raw;
    uint32_t flags;
    uint64_t base;

    if (bar >= 6U) {
        return false;
    }
    raw_bar = virtio_blk_raw_bar_ptr(bar);
    if (raw_bar == NULL) {
        return false;
    }
    raw = *raw_bar;
    if ((raw & 1U) != 0) {
        return false;
    }
    flags = raw & 0x0FU;
    base = VIRTIO_BLK_MODERN_BAR_BASE +
           (uint64_t) g_virtio_blk.pci.slot * VIRTIO_BLK_MODERN_BAR_STRIDE +
           (uint64_t) g_virtio_blk.pci.func * 0x1000ULL;
    pci_config_write32(g_virtio_blk.pci.bus,
                       g_virtio_blk.pci.slot,
                       g_virtio_blk.pci.func,
                       (uint8_t) (0x10U + bar * 4U),
                       (uint32_t) base | flags);
    *raw_bar = (uint32_t) base | flags;
    g_virtio_blk.pci.bar_address[bar] = base;
    if ((flags & 0x06U) == 0x04U && bar < 5U) {
        uint32_t *high_bar = virtio_blk_raw_bar_ptr((uint8_t) (bar + 1U));

        pci_config_write32(g_virtio_blk.pci.bus,
                           g_virtio_blk.pci.slot,
                           g_virtio_blk.pci.func,
                           (uint8_t) (0x10U + (bar + 1U) * 4U),
                           0);
        if (high_bar != NULL) {
            *high_bar = 0;
        }
    }
    kernel_log_hex_u32("virtio-blk: assigned modern BAR=", bar);
    kernel_log_hex_u32("virtio-blk: assigned modern base=", (uint32_t) base);
    return true;
}

static uint64_t virtio_blk_align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1ULL) & ~(align - 1ULL);
}

static bool virtio_blk_supported_id(uint16_t device_id)
{
    return device_id == VIRTIO_LEGACY_BLOCK ||
           device_id == VIRTIO_MODERN_BLOCK;
}

static uint8_t virtio_blk_mmio_read8(uint64_t address)
{
    return *(volatile uint8_t *) (uintptr_t) address;
}

static uint16_t virtio_blk_mmio_read16(uint64_t address)
{
    return *(volatile uint16_t *) (uintptr_t) address;
}

static uint32_t virtio_blk_mmio_read32(uint64_t address)
{
    return *(volatile uint32_t *) (uintptr_t) address;
}

static void virtio_blk_mmio_write8(uint64_t address, uint8_t value)
{
    *(volatile uint8_t *) (uintptr_t) address = value;
}

static void virtio_blk_mmio_write16(uint64_t address, uint16_t value)
{
    *(volatile uint16_t *) (uintptr_t) address = value;
}

static void virtio_blk_mmio_write32(uint64_t address, uint32_t value)
{
    *(volatile uint32_t *) (uintptr_t) address = value;
}

static void virtio_blk_mmio_write64(uint64_t address, uint64_t value)
{
    virtio_blk_mmio_write32(address, (uint32_t) value);
    virtio_blk_mmio_write32(address + 4, (uint32_t) (value >> 32));
}

static void virtio_blk_barrier(void)
{
    asm volatile ("mfence" ::: "memory");
}

static void virtio_blk_set_status(uint8_t status)
{
    if (g_virtio_blk.legacy) {
        outb((uint16_t) (g_virtio_blk.io_base + VIRTIO_LEGACY_STATUS), status);
    } else if (g_virtio_blk.common_base != 0) {
        virtio_blk_mmio_write8(g_virtio_blk.common_base + VIRTIO_COMMON_DEVICE_STATUS, status);
    }
}

static uint8_t virtio_blk_get_status(void)
{
    if (g_virtio_blk.legacy) {
        return inb((uint16_t) (g_virtio_blk.io_base + VIRTIO_LEGACY_STATUS));
    }
    if (g_virtio_blk.common_base != 0) {
        return virtio_blk_mmio_read8(g_virtio_blk.common_base + VIRTIO_COMMON_DEVICE_STATUS);
    }
    return 0;
}

static void virtio_blk_reset(void)
{
    if (g_virtio_blk.legacy) {
        outb((uint16_t) (g_virtio_blk.io_base + VIRTIO_LEGACY_STATUS), 0);
    } else if (g_virtio_blk.common_base != 0) {
        virtio_blk_mmio_write8(g_virtio_blk.common_base + VIRTIO_COMMON_DEVICE_STATUS, 0);
    }
}

static void virtio_blk_append_cap(const pci_device_info_t *info,
                                  uint8_t pointer,
                                  virtio_blk_cap_t *out_cap)
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

static bool virtio_blk_find_caps(const pci_device_info_t *info,
                                 virtio_blk_cap_t *common,
                                 virtio_blk_cap_t *notify,
                                 virtio_blk_cap_t *device)
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
        virtio_blk_cap_t cap;
        uint8_t next;

        memset(&cap, 0, sizeof(cap));
        virtio_blk_append_cap(info, pointer, &cap);
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

static bool virtio_blk_setup_queue_layout(virtio_blk_queue_t *queue, uint16_t size)
{
    uint64_t desc_bytes;
    uint64_t avail_bytes;
    uint64_t used_offset;
    uint64_t used_bytes;
    uint64_t total_size;
    uint8_t *base;

    if (queue == NULL || size < 4U || size > VIRTIO_BLK_QUEUE_SIZE_MAX) {
        return false;
    }
    desc_bytes = (uint64_t) sizeof(virtio_blk_desc_t) * size;
    avail_bytes = 4ULL + (uint64_t) sizeof(uint16_t) * size;
    used_offset = virtio_blk_align_up(desc_bytes + avail_bytes, VIRTIO_RING_ALIGN);
    used_bytes = 4ULL + (uint64_t) sizeof(virtio_blk_used_elem_t) * size;
    total_size = used_offset + used_bytes;
    memset(queue, 0, sizeof(*queue));
    if (!dma_alloc(total_size, VIRTIO_RING_ALIGN, 0xFFFFFFFFULL, &queue->ring_dma)) {
        return false;
    }

    base = (uint8_t *) queue->ring_dma.virtual_address;
    queue->desc = (volatile virtio_blk_desc_t *) base;
    queue->avail_flags = (volatile uint16_t *) (base + desc_bytes);
    queue->avail_idx = queue->avail_flags + 1;
    queue->avail_ring = queue->avail_idx + 1;
    queue->used_flags = (volatile uint16_t *) (base + used_offset);
    queue->used_idx = queue->used_flags + 1;
    queue->used_ring = (volatile virtio_blk_used_elem_t *) (queue->used_idx + 1);
    queue->size = size;
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

static bool virtio_blk_legacy_select_queue(uint16_t queue_index, uint16_t *out_size)
{
    uint16_t size;

    if (out_size == NULL || g_virtio_blk.io_base == 0) {
        return false;
    }
    outw((uint16_t) (g_virtio_blk.io_base + VIRTIO_LEGACY_QUEUE_SELECT), queue_index);
    size = inw((uint16_t) (g_virtio_blk.io_base + VIRTIO_LEGACY_QUEUE_SIZE));
    *out_size = size;
    return size >= 4U && size <= VIRTIO_BLK_QUEUE_SIZE_MAX;
}

static bool virtio_blk_legacy_setup_queue(virtio_blk_queue_t *queue, uint16_t queue_index)
{
    uint16_t device_size;

    if (!virtio_blk_legacy_select_queue(queue_index, &device_size)) {
        kernel_log_hex_u32("virtio-blk: legacy queue unavailable=", queue_index);
        return false;
    }
    if (!virtio_blk_setup_queue_layout(queue, device_size)) {
        kernel_log_hex_u32("virtio-blk: legacy queue layout failed=", device_size);
        return false;
    }
    queue->queue_index = queue_index;
    if ((queue->ring_dma.physical_address & (VIRTIO_RING_ALIGN - 1ULL)) != 0 ||
        (queue->ring_dma.physical_address >> 32) != 0) {
        kernel_log_hex_u32("virtio-blk: legacy queue address invalid=",
                           (uint32_t) queue->ring_dma.physical_address);
        return false;
    }
    outw((uint16_t) (g_virtio_blk.io_base + VIRTIO_LEGACY_QUEUE_SELECT), queue_index);
    outl((uint16_t) (g_virtio_blk.io_base + VIRTIO_LEGACY_QUEUE_ADDRESS),
         (uint32_t) (queue->ring_dma.physical_address >> 12));
    queue->enabled = true;
    return true;
}

static uint16_t virtio_blk_modern_queue_size(uint16_t device_size)
{
    if (device_size <= VIRTIO_BLK_QUEUE_SIZE_MAX) {
        return device_size;
    }
    return VIRTIO_BLK_QUEUE_SIZE_MAX;
}

static bool virtio_blk_modern_setup_queue(virtio_blk_queue_t *queue, uint16_t queue_index)
{
    uint16_t device_size;
    uint64_t desc_address;
    uint64_t avail_address;
    uint64_t used_address;
    uint64_t avail_offset;
    uint64_t used_offset;

    if (g_virtio_blk.common_base == 0 || g_virtio_blk.notify_base == 0) {
        log_write("virtio-blk: modern common/notify base missing");
        return false;
    }
    virtio_blk_mmio_write16(g_virtio_blk.common_base + VIRTIO_COMMON_QUEUE_SELECT, queue_index);
    device_size = virtio_blk_mmio_read16(g_virtio_blk.common_base + VIRTIO_COMMON_QUEUE_SIZE);
    device_size = virtio_blk_modern_queue_size(device_size);
    if (device_size < 4U) {
        kernel_log_hex_u32("virtio-blk: modern queue size invalid=", device_size);
        return false;
    }
    virtio_blk_mmio_write16(g_virtio_blk.common_base + VIRTIO_COMMON_QUEUE_SIZE, device_size);
    if (!virtio_blk_setup_queue_layout(queue, device_size)) {
        kernel_log_hex_u32("virtio-blk: modern queue layout failed=", device_size);
        return false;
    }

    avail_offset = (uint64_t) sizeof(virtio_blk_desc_t) * device_size;
    used_offset = virtio_blk_align_up(avail_offset + 4ULL +
                                      (uint64_t) sizeof(uint16_t) * device_size,
                                      VIRTIO_RING_ALIGN);
    desc_address = queue->ring_dma.physical_address;
    avail_address = queue->ring_dma.physical_address + avail_offset;
    used_address = queue->ring_dma.physical_address + used_offset;
    queue->queue_index = queue_index;
    queue->notify_offset =
        virtio_blk_mmio_read16(g_virtio_blk.common_base + VIRTIO_COMMON_QUEUE_NOTIFY_OFFSET);
    queue->notify_multiplier = g_virtio_blk.notify_multiplier;
    queue->notify_address = g_virtio_blk.notify_base +
                            (uint64_t) queue->notify_offset * queue->notify_multiplier;
    virtio_blk_mmio_write64(g_virtio_blk.common_base + VIRTIO_COMMON_QUEUE_DESC, desc_address);
    virtio_blk_mmio_write64(g_virtio_blk.common_base + VIRTIO_COMMON_QUEUE_DRIVER, avail_address);
    virtio_blk_mmio_write64(g_virtio_blk.common_base + VIRTIO_COMMON_QUEUE_DEVICE, used_address);
    virtio_blk_mmio_write16(g_virtio_blk.common_base + VIRTIO_COMMON_QUEUE_ENABLE, 1);
    queue->enabled = virtio_blk_mmio_read16(g_virtio_blk.common_base +
                                            VIRTIO_COMMON_QUEUE_ENABLE) != 0;
    if (!queue->enabled) {
        kernel_log_hex_u32("virtio-blk: modern queue enable failed=", queue_index);
    }
    return queue->enabled;
}

static void virtio_blk_notify(virtio_blk_queue_t *queue)
{
    if (queue == NULL || !queue->enabled) {
        return;
    }
    if (g_virtio_blk.legacy) {
        outw((uint16_t) (g_virtio_blk.io_base + VIRTIO_LEGACY_QUEUE_NOTIFY),
             queue->queue_index);
    } else {
        virtio_blk_mmio_write16(queue->notify_address, queue->queue_index);
    }
}

static bool virtio_blk_negotiate_legacy(void)
{
    uint32_t device_features;
    uint32_t driver_features = 0;
    uint8_t status;

    device_features = inl((uint16_t) (g_virtio_blk.io_base + VIRTIO_LEGACY_DEVICE_FEATURES));
    g_virtio_blk.device_features = device_features;
    g_virtio_blk.driver_features = driver_features;
    virtio_blk_reset();
    virtio_blk_set_status(VIRTIO_STATUS_ACKNOWLEDGE);
    virtio_blk_set_status(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    outl((uint16_t) (g_virtio_blk.io_base + VIRTIO_LEGACY_DRIVER_FEATURES), driver_features);
    status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
    virtio_blk_set_status(status);
    return (virtio_blk_get_status() & VIRTIO_STATUS_FAILED) == 0;
}

static bool virtio_blk_negotiate_modern(void)
{
    uint64_t device_features;
    uint64_t driver_features;
    uint8_t status;

    virtio_blk_mmio_write32(g_virtio_blk.common_base + VIRTIO_COMMON_DEVICE_FEATURE_SELECT, 0);
    device_features = virtio_blk_mmio_read32(g_virtio_blk.common_base + VIRTIO_COMMON_DEVICE_FEATURE);
    virtio_blk_mmio_write32(g_virtio_blk.common_base + VIRTIO_COMMON_DEVICE_FEATURE_SELECT, 1);
    device_features |= (uint64_t) virtio_blk_mmio_read32(g_virtio_blk.common_base +
                                                          VIRTIO_COMMON_DEVICE_FEATURE) << 32;
    driver_features = device_features & VIRTIO_F_VERSION_1;
    if ((driver_features & VIRTIO_F_VERSION_1) == 0) {
        return false;
    }
    g_virtio_blk.device_features = device_features;
    g_virtio_blk.driver_features = driver_features;
    virtio_blk_reset();
    virtio_blk_set_status(VIRTIO_STATUS_ACKNOWLEDGE);
    virtio_blk_set_status(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    virtio_blk_mmio_write32(g_virtio_blk.common_base + VIRTIO_COMMON_DRIVER_FEATURE_SELECT, 0);
    virtio_blk_mmio_write32(g_virtio_blk.common_base + VIRTIO_COMMON_DRIVER_FEATURE,
                            (uint32_t) driver_features);
    virtio_blk_mmio_write32(g_virtio_blk.common_base + VIRTIO_COMMON_DRIVER_FEATURE_SELECT, 1);
    virtio_blk_mmio_write32(g_virtio_blk.common_base + VIRTIO_COMMON_DRIVER_FEATURE,
                            (uint32_t) (driver_features >> 32));
    status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
    virtio_blk_set_status(status);
    return (virtio_blk_get_status() & VIRTIO_STATUS_FEATURES_OK) != 0;
}

static uint64_t virtio_blk_read_capacity(void)
{
    uint32_t low;
    uint32_t high;

    if (g_virtio_blk.legacy) {
        low = inl((uint16_t) (g_virtio_blk.io_base + VIRTIO_LEGACY_DEVICE_CONFIG));
        high = inl((uint16_t) (g_virtio_blk.io_base + VIRTIO_LEGACY_DEVICE_CONFIG + 4U));
    } else {
        low = virtio_blk_mmio_read32(g_virtio_blk.device_base);
        high = virtio_blk_mmio_read32(g_virtio_blk.device_base + 4U);
    }
    return (uint64_t) low | ((uint64_t) high << 32);
}

static bool virtio_blk_find_device_callback(const pci_device_info_t *info, void *ctx)
{
    if (info != NULL && info->vendor_id == VIRTIO_PCI_VENDOR &&
        virtio_blk_supported_id(info->device_id)) {
        g_virtio_blk.pci = *info;
        *(bool *) ctx = true;
        return false;
    }
    return true;
}

static bool virtio_blk_prepare_modern(void)
{
    virtio_blk_cap_t common;
    virtio_blk_cap_t notify;
    virtio_blk_cap_t device;
    uint64_t common_bar;
    uint64_t notify_bar;
    uint64_t device_bar;
    uint16_t command;

    if (!virtio_blk_find_caps(&g_virtio_blk.pci, &common, &notify, &device) ||
        common.bar >= 6U || notify.bar >= 6U || device.bar >= 6U) {
        log_write("virtio-blk: modern capabilities unavailable");
        return false;
    }
    command = pci_config_read16(g_virtio_blk.pci.bus,
                                g_virtio_blk.pci.slot,
                                g_virtio_blk.pci.func,
                                PCI_COMMAND_OFFSET);
    command |= PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;
    pci_config_write16(g_virtio_blk.pci.bus,
                       g_virtio_blk.pci.slot,
                       g_virtio_blk.pci.func,
                       PCI_COMMAND_OFFSET,
                       command);
    common_bar = g_virtio_blk.pci.bar_address[common.bar];
    notify_bar = g_virtio_blk.pci.bar_address[notify.bar];
    device_bar = g_virtio_blk.pci.bar_address[device.bar];
    if (common_bar == 0 && !virtio_blk_assign_modern_bar(common.bar)) {
        log_write("virtio-blk: common BAR assignment failed");
        return false;
    }
    if (notify_bar == 0 && notify.bar != common.bar &&
        !virtio_blk_assign_modern_bar(notify.bar)) {
        log_write("virtio-blk: notify BAR assignment failed");
        return false;
    }
    if (device_bar == 0 && device.bar != common.bar &&
        device.bar != notify.bar &&
        !virtio_blk_assign_modern_bar(device.bar)) {
        log_write("virtio-blk: device BAR assignment failed");
        return false;
    }
    common_bar = g_virtio_blk.pci.bar_address[common.bar];
    notify_bar = g_virtio_blk.pci.bar_address[notify.bar];
    device_bar = g_virtio_blk.pci.bar_address[device.bar];
    if (!virtio_blk_identity_mapped(common_bar) ||
        !virtio_blk_identity_mapped(notify_bar) ||
        !virtio_blk_identity_mapped(device_bar)) {
        log_write("virtio-blk: modern MMIO BAR unavailable");
        return false;
    }
    g_virtio_blk.common_base = common_bar + common.offset;
    g_virtio_blk.notify_base = notify_bar + notify.offset;
    g_virtio_blk.device_base = device_bar + device.offset;
    g_virtio_blk.notify_multiplier = notify.notify_multiplier;
    if (g_virtio_blk.notify_multiplier == 0) {
        g_virtio_blk.notify_multiplier = 2;
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
    return virtio_blk_negotiate_modern();
}

static bool virtio_blk_prepare_legacy(void)
{
    uint16_t io_base = (uint16_t) (g_virtio_blk.pci.bar0 & 0xFFFCU);
    uint16_t command;

    if ((g_virtio_blk.pci.bar0 & 1U) == 0 ||
        io_base < 0x100U || io_base > 0xFFF0U) {
        return false;
    }
    command = pci_config_read16(g_virtio_blk.pci.bus,
                                g_virtio_blk.pci.slot,
                                g_virtio_blk.pci.func,
                                PCI_COMMAND_OFFSET);
    command |= PCI_COMMAND_IO | PCI_COMMAND_BUS_MASTER;
    pci_config_write16(g_virtio_blk.pci.bus,
                       g_virtio_blk.pci.slot,
                       g_virtio_blk.pci.func,
                       PCI_COMMAND_OFFSET,
                       command);
    g_virtio_blk.io_base = io_base;
    return virtio_blk_negotiate_legacy();
}

static void virtio_blk_mark_failed(const char *status)
{
    virtio_blk_set_status(VIRTIO_STATUS_FAILED);
    g_virtio_blk.ready = false;
    g_virtio_blk.info.ready = false;
    strlcpy(g_virtio_blk.info.status, status, sizeof(g_virtio_blk.info.status));
    log_write(g_virtio_blk.info.status);
}

static bool virtio_blk_alloc_buffers(void)
{
    if (!dma_alloc(sizeof(virtio_blk_request_t) + 1U,
                   16,
                   0xFFFFFFFFULL,
                   &g_virtio_blk.request_dma)) {
        return false;
    }
    return dma_alloc((uint64_t) VIRTIO_BLK_SECTOR_SIZE * VIRTIO_BLK_MAX_SECTORS_PER_IO,
                     16,
                     0xFFFFFFFFULL,
                     &g_virtio_blk.data_dma);
}

bool virtio_blk_read_sectors(uint64_t sector, uint32_t count, void *buffer)
{
    virtio_blk_queue_t *queue = &g_virtio_blk.queue;
    volatile virtio_blk_request_t *request;
    volatile uint8_t *status;
    uint16_t avail_slot;
    uint32_t bytes;
    uint16_t used_slot;

    if (!g_virtio_blk.ready || buffer == NULL ||
        count == 0 || count > VIRTIO_BLK_MAX_SECTORS_PER_IO ||
        sector >= g_virtio_blk.info.capacity_sectors ||
        count > g_virtio_blk.info.capacity_sectors - sector ||
        g_virtio_blk.busy) {
        return false;
    }
    g_virtio_blk.busy = true;
    bytes = count * VIRTIO_BLK_SECTOR_SIZE;
    request = (volatile virtio_blk_request_t *) g_virtio_blk.request_dma.virtual_address;
    status = (volatile uint8_t *) ((uint8_t *) g_virtio_blk.request_dma.virtual_address +
                                   sizeof(virtio_blk_request_t));
    memset((void *) request, 0, (uint32_t) g_virtio_blk.request_dma.size);
    memset(g_virtio_blk.data_dma.virtual_address, 0, bytes);
    request->type = VIRTIO_BLK_T_IN;
    request->reserved = 0;
    request->sector = sector;
    *status = 0xFFU;

    queue->desc[0].address = g_virtio_blk.request_dma.physical_address;
    queue->desc[0].length = sizeof(virtio_blk_request_t);
    queue->desc[0].flags = VIRTQ_DESC_F_NEXT;
    queue->desc[0].next = 1;
    queue->desc[1].address = g_virtio_blk.data_dma.physical_address;
    queue->desc[1].length = bytes;
    queue->desc[1].flags = VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT;
    queue->desc[1].next = 2;
    queue->desc[2].address = g_virtio_blk.request_dma.physical_address +
                             sizeof(virtio_blk_request_t);
    queue->desc[2].length = 1;
    queue->desc[2].flags = VIRTQ_DESC_F_WRITE;
    queue->desc[2].next = 0;

    avail_slot = (uint16_t) (*queue->avail_idx % queue->size);
    queue->avail_ring[avail_slot] = 0;
    virtio_blk_barrier();
    (*queue->avail_idx)++;
    virtio_blk_barrier();
    virtio_blk_notify(queue);

    for (uint32_t i = 0; i < 1000000U && queue->next_used == *queue->used_idx; i++) {
        io_wait();
    }
    if (queue->next_used == *queue->used_idx ||
        *status != VIRTIO_BLK_S_OK) {
        g_virtio_blk.info.last_status = *status;
        g_virtio_blk.busy = false;
        return false;
    }
    used_slot = (uint16_t) (queue->next_used % queue->size);
    if (queue->used_ring[used_slot].id != 0) {
        g_virtio_blk.info.last_status = *status;
        g_virtio_blk.busy = false;
        return false;
    }
    queue->next_used++;
    memcpy(buffer, g_virtio_blk.data_dma.virtual_address, bytes);
    g_virtio_blk.info.last_status = *status;
    g_virtio_blk.info.read_ops++;
    g_virtio_blk.busy = false;
    return true;
}

bool virtio_blk_read_sector(uint64_t sector, void *buffer)
{
    return virtio_blk_read_sectors(sector, 1, buffer);
}

bool virtio_blk_driver_init(void)
{
    bool found = false;
    uint64_t dma_mark;
    uint8_t sector0[VIRTIO_BLK_SECTOR_SIZE];

    memset(&g_virtio_blk, 0, sizeof(g_virtio_blk));
    strcpy(g_virtio_blk.info.status, "virtio-blk: not found");
    pci_enumerate(virtio_blk_find_device_callback, &found);
    if (!found) {
        log_write(g_virtio_blk.info.status);
        return false;
    }
    g_virtio_blk.present = true;
    g_virtio_blk.legacy = g_virtio_blk.pci.device_id == VIRTIO_LEGACY_BLOCK;
    g_virtio_blk.modern = g_virtio_blk.pci.device_id == VIRTIO_MODERN_BLOCK;
    g_virtio_blk.info.present = true;
    g_virtio_blk.info.legacy = g_virtio_blk.legacy;
    g_virtio_blk.info.modern = g_virtio_blk.modern;
    g_virtio_blk.info.vendor_id = g_virtio_blk.pci.vendor_id;
    g_virtio_blk.info.device_id = g_virtio_blk.pci.device_id;
    g_virtio_blk.info.bus = g_virtio_blk.pci.bus;
    g_virtio_blk.info.slot = g_virtio_blk.pci.slot;
    g_virtio_blk.info.func = g_virtio_blk.pci.func;
    g_virtio_blk.info.irq = g_virtio_blk.pci.interrupt_line;
    g_virtio_blk.info.sector_size = VIRTIO_BLK_SECTOR_SIZE;

    if (g_virtio_blk.modern) {
        if (!virtio_blk_prepare_modern()) {
            virtio_blk_mark_failed("virtio-blk: modern transport unavailable");
            return false;
        }
    } else if (!virtio_blk_prepare_legacy()) {
        virtio_blk_mark_failed("virtio-blk: legacy transport unavailable");
        return false;
    }
    g_virtio_blk.info.capacity_sectors = virtio_blk_read_capacity();
    g_virtio_blk.info.io_base = g_virtio_blk.io_base;
    g_virtio_blk.info.mmio_base = (uint32_t) g_virtio_blk.common_base;
    if (g_virtio_blk.info.capacity_sectors == 0) {
        virtio_blk_mark_failed("virtio-blk: zero capacity");
        return false;
    }

    dma_mark = dma_checkpoint();
    if (g_virtio_blk.legacy) {
        if (!virtio_blk_legacy_setup_queue(&g_virtio_blk.queue, 0)) {
            dma_rewind(dma_mark);
            virtio_blk_mark_failed("virtio-blk: queue setup failed");
            return false;
        }
    } else if (!virtio_blk_modern_setup_queue(&g_virtio_blk.queue, 0)) {
        dma_rewind(dma_mark);
        virtio_blk_mark_failed("virtio-blk: modern queue setup failed");
        return false;
    }
    if (!virtio_blk_alloc_buffers()) {
        dma_rewind(dma_mark);
        virtio_blk_mark_failed("virtio-blk: buffer dma allocation failed");
        return false;
    }

    g_virtio_blk.ready = true;
    g_virtio_blk.info.ready = true;
    virtio_blk_set_status(VIRTIO_STATUS_ACKNOWLEDGE |
                          VIRTIO_STATUS_DRIVER |
                          VIRTIO_STATUS_FEATURES_OK |
                          VIRTIO_STATUS_DRIVER_OK);
    strcpy(g_virtio_blk.info.status, g_virtio_blk.modern ?
           "virtio-blk: modern queue ready" :
           "virtio-blk: legacy queue ready");
    log_write(g_virtio_blk.info.status);
    if (virtio_blk_read_sector(0, sector0)) {
        g_virtio_blk.info.sector0_signature =
            ((uint16_t) sector0[511] << 8) | sector0[510];
        kernel_log_hex_u32("virtio-blk: sector0 signature=",
                           g_virtio_blk.info.sector0_signature);
    } else {
        log_write("virtio-blk: sector0 read failed");
    }
    return true;
}

void virtio_blk_shutdown(void)
{
    if (!g_virtio_blk.present) {
        return;
    }
    virtio_blk_reset();
    g_virtio_blk.ready = false;
    g_virtio_blk.info.ready = false;
    strcpy(g_virtio_blk.info.status, "virtio-blk: shutdown");
    log_write(g_virtio_blk.info.status);
}

bool virtio_blk_ready(void)
{
    return g_virtio_blk.ready;
}

const virtio_blk_info_t *virtio_blk_info(void)
{
    return &g_virtio_blk.info;
}

const char *virtio_blk_status(void)
{
    return g_virtio_blk.info.status;
}
