#include "common.h"
#include "dma.h"
#include "kernel.h"
#include "mmu.h"
#include "nvme.h"
#include "pci.h"

#define PCI_CLASS_STORAGE       0x01
#define PCI_SUBCLASS_NVM        0x08
#define PCI_COMMAND_OFFSET      0x04
#define PCI_COMMAND_MEMORY       0x0002
#define PCI_COMMAND_BUS_MASTER  0x0004

#define NVME_REG_CAP            0x00
#define NVME_REG_VS             0x08
#define NVME_REG_INTMS          0x0C
#define NVME_REG_INTMC          0x10
#define NVME_REG_CC             0x14
#define NVME_REG_CSTS           0x1C
#define NVME_REG_AQA            0x24
#define NVME_REG_ASQ            0x28
#define NVME_REG_ACQ            0x30
#define NVME_REG_DB_BASE        0x100

#define NVME_CC_EN              0x0001U
#define NVME_CC_CSS_NVM         0x0000U
#define NVME_CC_IOCQES          (4U << 16)
#define NVME_CC_IOSQES          (6U << 20)
#define NVME_CC_SHN_NORMAL      (1U << 10)

#define NVME_CSTS_RDY           0x0001U
#define NVME_CSTS_CFS           0x0002U
#define NVME_CSTS_SHST_MASK     (3U << 2)

#define NVME_ADM_DELETE_SQ      0x00U
#define NVME_ADM_CREATE_SQ      0x01U
#define NVME_ADM_DELETE_CQ      0x04U
#define NVME_ADM_CREATE_CQ      0x05U
#define NVME_ADM_IDENTIFY       0x06U

#define NVME_NVM_FLUSH          0x00U
#define NVME_NVM_WRITE          0x01U
#define NVME_NVM_READ           0x02U

#define NVME_ID_CNS_ID_CTRL     0x01U
#define NVME_ID_CNS_NS_ACTIVE   0x00U

#define NVME_SQ_ENTRY_SIZE      64U
#define NVME_CQ_ENTRY_SIZE      16U
#define NVME_ADM_QSIZE          4U
#define NVME_IO_QSIZE           4U
#define NVME_DEFAULT_NS          1U
#define NVME_BOUNCE_SECTORS     32U
#define NVME_SECTOR_SIZE        512U
#define NVME_WAIT_LONG          5000000U

typedef struct {
    uint32_t cdw0;
    uint32_t nsid;
    uint32_t cdw2;
    uint32_t cdw3;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) nvme_sq_entry_t;

typedef struct {
    uint32_t dw0;
    uint32_t dw1;
    uint32_t dw2;
    uint32_t dw3;
} __attribute__((packed)) nvme_cq_entry_t;

static nvme_info_t g_nvme_info;
static bool g_nvme_dma_ready;

static dma_buffer_t g_admin_sq_dma;
static dma_buffer_t g_admin_cq_dma;
static dma_buffer_t g_io_sq_dma;
static dma_buffer_t g_io_cq_dma;
static dma_buffer_t g_identify_dma;
static dma_buffer_t g_bounce_dma;

static nvme_sq_entry_t *g_admin_sq;
static nvme_cq_entry_t *g_admin_cq;
static nvme_sq_entry_t *g_io_sq;
static nvme_cq_entry_t *g_io_cq;
static uint8_t *g_identify_buf;
static uint8_t *g_bounce;

static uint32_t g_admin_sq_tail;
static uint32_t g_admin_cq_head;
static uint32_t g_admin_phase = 1;
static uint32_t g_io_sq_tail;
static uint32_t g_io_cq_head;
static uint32_t g_io_phase = 1;

static uint32_t nvme_bar_base(uint32_t bar)
{
    if ((bar & 1u) != 0) {
        return 0;
    }
    return bar & 0xFFFFFFF0u;
}

static uint32_t nvme_read32(uint32_t offset)
{
    volatile uint32_t *ptr = (volatile uint32_t *) (uint64_t) (g_nvme_info.mmio_base + offset);

    return *ptr;
}

static void nvme_write32(uint32_t offset, uint32_t value)
{
    volatile uint32_t *ptr = (volatile uint32_t *) (uint64_t) (g_nvme_info.mmio_base + offset);

    *ptr = value;
}

/* Doorbell access: per-queue SQ tail / CQ head register. */
static uint32_t nvme_db_stride_bytes(void)
{
    return (uint32_t) (4U << g_nvme_info.doorbell_stride);
}

static void nvme_doorbell_sq(uint32_t qid, uint32_t tail)
{
    uint32_t off = NVME_REG_DB_BASE + nvme_db_stride_bytes() * (qid * 2U + 0U);

    nvme_write32(off, tail & 0xFFFFU);
}

static void nvme_doorbell_cq(uint32_t qid, uint32_t head)
{
    uint32_t off = NVME_REG_DB_BASE + nvme_db_stride_bytes() * (qid * 2U + 1U);

    nvme_write32(off, head & 0xFFFFU);
}

static bool nvme_wait_ready(bool expected)
{
    uint32_t want = expected ? NVME_CSTS_RDY : 0U;

    for (uint32_t i = 0; i < NVME_WAIT_LONG; i++) {
        if ((nvme_read32(NVME_REG_CSTS) & NVME_CSTS_RDY) == want) {
            return true;
        }
        io_wait();
    }
    return false;
}

static void nvme_reset_controller(void)
{
    uint32_t cc = nvme_read32(NVME_REG_CC);

    cc &= ~NVME_CC_EN;
    nvme_write32(NVME_REG_CC, cc);
    (void) nvme_wait_ready(false);
}

static bool nvme_admin_submit(nvme_sq_entry_t *cmd, uint32_t *cid_out)
{
    static uint32_t next_cid = 1;
    nvme_sq_entry_t *slot = &g_admin_sq[g_admin_sq_tail];
    uint32_t cid = next_cid++;

    cmd->cdw0 = (cmd->cdw0 & 0x00FFFFFFU) | (cid << 16);
    *slot = *cmd;
    g_admin_sq_tail = (g_admin_sq_tail + 1U) % NVME_ADM_QSIZE;
    nvme_doorbell_sq(0, g_admin_sq_tail);
    *cid_out = cid;
    return true;
}

static bool nvme_admin_complete(uint32_t cid)
{
    for (uint32_t i = 0; i < NVME_WAIT_LONG; i++) {
        nvme_cq_entry_t *entry = &g_admin_cq[g_admin_cq_head];
        uint32_t phase = entry->dw3 & 1U;
        uint32_t done_cid = entry->dw3 >> 16;

        if (phase == g_admin_phase) {
            uint32_t status = (entry->dw3 >> 1) & 0x7FFU;

            g_admin_cq_head = (g_admin_cq_head + 1U) % NVME_ADM_QSIZE;
            nvme_doorbell_cq(0, g_admin_cq_head);
            if (g_admin_cq_head == 0) {
                g_admin_phase ^= 1U;
            }
            if (done_cid == cid && status == 0U) {
                return true;
            }
            return false;
        }
        io_wait();
    }
    return false;
}

static bool nvme_admin_command(nvme_sq_entry_t *cmd)
{
    uint32_t cid;

    nvme_admin_submit(cmd, &cid);
    return nvme_admin_complete(cid);
}

static bool nvme_identify(uint32_t cns, uint32_t nsid, void *buffer, uint64_t phys)
{
    nvme_sq_entry_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = NVME_ADM_IDENTIFY;
    cmd.nsid = nsid;
    cmd.prp1 = phys;
    cmd.cdw10 = cns;
    (void) buffer;
    return nvme_admin_command(&cmd);
}

static bool nvme_create_cq(uint32_t qid, uint64_t phys)
{
    nvme_sq_entry_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = NVME_ADM_CREATE_CQ;
    cmd.prp1 = phys;
    cmd.cdw10 = ((NVME_IO_QSIZE - 1U) << 0) | (qid << 16);
    cmd.cdw11 = 1U; /* PC: physically contiguous */
    return nvme_admin_command(&cmd);
}

static bool nvme_create_sq(uint32_t qid, uint64_t phys)
{
    nvme_sq_entry_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = NVME_ADM_CREATE_SQ;
    cmd.prp1 = phys;
    cmd.cdw10 = ((NVME_IO_QSIZE - 1U) << 0) | (qid << 16);
    cmd.cdw11 = qid << 16; /* CQID */
    return nvme_admin_command(&cmd);
}

static void nvme_decode_string(uint8_t *dst, const uint8_t *src, uint32_t len)
{
    uint32_t pos = 0;

    for (uint32_t i = 0; i + 1 < len; i += 2) {
        dst[pos++] = (char) src[i + 1];
        dst[pos++] = (char) src[i];
    }
    while (pos > 0 && (dst[pos - 1] == ' ' || dst[pos - 1] == '\0')) {
        pos--;
    }
    dst[pos] = '\0';
}

static bool nvme_init_queues(void)
{
    uint64_t checkpoint = dma_checkpoint();
    uint32_t aqa;

    if (!dma_alloc(NVME_ADM_QSIZE * NVME_SQ_ENTRY_SIZE, 128, 0xFFFFFFFFULL,
                   &g_admin_sq_dma) ||
        !dma_alloc(NVME_ADM_QSIZE * NVME_CQ_ENTRY_SIZE, 128, 0xFFFFFFFFULL,
                   &g_admin_cq_dma) ||
        !dma_alloc(NVME_IO_QSIZE * NVME_SQ_ENTRY_SIZE, 128, 0xFFFFFFFFULL,
                   &g_io_sq_dma) ||
        !dma_alloc(NVME_IO_QSIZE * NVME_CQ_ENTRY_SIZE, 128, 0xFFFFFFFFULL,
                   &g_io_cq_dma) ||
        !dma_alloc(4096, 4096, 0xFFFFFFFFULL, &g_identify_dma) ||
        !dma_alloc((uint64_t) NVME_BOUNCE_SECTORS * NVME_SECTOR_SIZE, 4096,
                   0xFFFFFFFFULL, &g_bounce_dma)) {
        dma_rewind(checkpoint);
        return false;
    }

    g_admin_sq = (nvme_sq_entry_t *) g_admin_sq_dma.virtual_address;
    g_admin_cq = (nvme_cq_entry_t *) g_admin_cq_dma.virtual_address;
    g_io_sq = (nvme_sq_entry_t *) g_io_sq_dma.virtual_address;
    g_io_cq = (nvme_cq_entry_t *) g_io_cq_dma.virtual_address;
    g_identify_buf = (uint8_t *) g_identify_dma.virtual_address;
    g_bounce = (uint8_t *) g_bounce_dma.virtual_address;

    nvme_reset_controller();

    /* disable interrupts */
    nvme_write32(NVME_REG_INTMS, 0xFFFFFFFFU);

    aqa = ((NVME_ADM_QSIZE - 1U) << 16) | (NVME_ADM_QSIZE - 1U);
    nvme_write32(NVME_REG_AQA, aqa);
    nvme_write32(NVME_REG_ASQ, (uint32_t) g_admin_sq_dma.physical_address);
    nvme_write32(NVME_REG_ASQ + 4, (uint32_t) (g_admin_sq_dma.physical_address >> 32));
    nvme_write32(NVME_REG_ACQ, (uint32_t) g_admin_cq_dma.physical_address);
    nvme_write32(NVME_REG_ACQ + 4, (uint32_t) (g_admin_cq_dma.physical_address >> 32));

    nvme_write32(NVME_REG_CC,
                 NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_IOCQES | NVME_CC_IOSQES);
    if (!nvme_wait_ready(true)) {
        return false;
    }
    g_nvme_dma_ready = true;
    return true;
}

static bool nvme_setup_io_queues(void)
{
    if (!nvme_create_cq(1, g_io_cq_dma.physical_address)) {
        return false;
    }
    if (!nvme_create_sq(1, g_io_sq_dma.physical_address)) {
        return false;
    }
    return true;
}

static bool nvme_identify_namespace(uint32_t nsid)
{
    memset(g_identify_buf, 0, 4096);
    if (!nvme_identify(NVME_ID_CNS_NS_ACTIVE, nsid, g_identify_buf,
                       g_identify_dma.physical_address)) {
        return false;
    }
    /* NSZE: bytes 0..7 little endian */
    uint64_t nsize = (uint64_t) g_identify_buf[0] |
                     ((uint64_t) g_identify_buf[1] << 8) |
                     ((uint64_t) g_identify_buf[2] << 16) |
                     ((uint64_t) g_identify_buf[3] << 24) |
                     ((uint64_t) g_identify_buf[4] << 32) |
                     ((uint64_t) g_identify_buf[5] << 40) |
                     ((uint64_t) g_identify_buf[6] << 48) |
                     ((uint64_t) g_identify_buf[7] << 56);
    g_nvme_info.capacity_sectors = nsize;
    /* LBAF0 at bytes 128..131; lba size exponent at bits 16-23 */
    uint32_t lbaf0 = (uint32_t) g_identify_buf[128] |
                     ((uint32_t) g_identify_buf[129] << 8) |
                     ((uint32_t) g_identify_buf[130] << 16) |
                     ((uint32_t) g_identify_buf[131] << 24);
    uint32_t lba_shift = (lbaf0 >> 16) & 0xFFU;
    g_nvme_info.lba_size = (lba_shift >= 32) ? 512U : (1U << lba_shift);
    return nsize != 0;
}

static bool nvme_identify_controller(void)
{
    memset(g_identify_buf, 0, 4096);
    if (!nvme_identify(NVME_ID_CNS_ID_CTRL, 0, g_identify_buf,
                       g_identify_dma.physical_address)) {
        return false;
    }
    /* model number: bytes 40..63 (ASCII, word-swapped) */
    nvme_decode_string(g_nvme_info.model, g_identify_buf + 40, 40);
    if (g_nvme_info.model[0] == '\0') {
        memcpy(g_nvme_info.model, "NVMe disk", 10);
    }
    return true;
}

static bool nvme_io_command(uint8_t opcode, uint32_t nsid,
                            uint64_t lba, uint32_t count)
{
    nvme_sq_entry_t *slot = &g_io_sq[g_io_sq_tail];
    uint32_t cid = (uint32_t) (g_io_sq_tail + 1);

    memset(slot, 0, sizeof(*slot));
    slot->cdw0 = opcode | (cid << 16);
    slot->nsid = nsid;
    slot->prp1 = g_bounce_dma.physical_address;
    slot->cdw10 = (uint32_t) lba;
    slot->cdw11 = (uint32_t) (lba >> 32);
    slot->cdw12 = (count - 1U) & 0xFFFFU;
    slot->cdw14 = 0U; /* 64-bit LBA only; bits 64+ are zero */

    g_io_sq_tail = (g_io_sq_tail + 1U) % NVME_IO_QSIZE;
    nvme_doorbell_sq(1, g_io_sq_tail);

    for (uint32_t i = 0; i < NVME_WAIT_LONG; i++) {
        nvme_cq_entry_t *entry = &g_io_cq[g_io_cq_head];

        if ((entry->dw3 & 1U) == g_io_phase) {
            uint32_t done_cid = entry->dw3 >> 16;
            uint32_t status = (entry->dw3 >> 1) & 0x7FFU;

            g_io_cq_head = (g_io_cq_head + 1U) % NVME_IO_QSIZE;
            nvme_doorbell_cq(1, g_io_cq_head);
            if (g_io_cq_head == 0) {
                g_io_phase ^= 1U;
            }
            if (done_cid == cid) {
                return status == 0U;
            }
            return false;
        }
        io_wait();
    }
    return false;
}

bool nvme_read(uint32_t namespace_id, uint64_t lba, uint32_t count, void *buffer)
{
    uint32_t bytes;

    if (!g_nvme_info.ready || buffer == NULL || count == 0 ||
        count > NVME_BOUNCE_SECTORS) {
        return false;
    }
    if (g_nvme_info.capacity_sectors != 0 &&
        (lba >= g_nvme_info.capacity_sectors ||
         count > g_nvme_info.capacity_sectors - lba)) {
        return false;
    }

    bytes = count * NVME_SECTOR_SIZE;
    memset(g_bounce, 0, bytes);
    if (!nvme_io_command(NVME_NVM_READ, namespace_id, lba, count)) {
        strcpy(g_nvme_info.status, "nvme: read error");
        return false;
    }
    memcpy(buffer, g_bounce, bytes);
    g_nvme_info.read_ops++;
    strcpy(g_nvme_info.status, "nvme: read ok");
    return true;
}

bool nvme_write(uint32_t namespace_id, uint64_t lba, uint32_t count, const void *buffer)
{
    uint32_t bytes;

    if (!g_nvme_info.ready || buffer == NULL || count == 0 ||
        count > NVME_BOUNCE_SECTORS) {
        return false;
    }
    if (g_nvme_info.capacity_sectors != 0 &&
        (lba >= g_nvme_info.capacity_sectors ||
         count > g_nvme_info.capacity_sectors - lba)) {
        return false;
    }

    bytes = count * NVME_SECTOR_SIZE;
    memcpy(g_bounce, buffer, bytes);
    if (!nvme_io_command(NVME_NVM_WRITE, namespace_id, lba, count)) {
        strcpy(g_nvme_info.status, "nvme: write error");
        return false;
    }
    g_nvme_info.write_ops++;
    strcpy(g_nvme_info.status, "nvme: write ok");
    return true;
}

uint32_t nvme_probe(void)
{
    if (!g_nvme_dma_ready) {
        return 0;
    }
    if (!nvme_identify_controller()) {
        return 0;
    }
    if (!nvme_setup_io_queues()) {
        return 0;
    }
    if (!nvme_identify_namespace(NVME_DEFAULT_NS)) {
        return 0;
    }
    g_nvme_info.ns_count = 1;
    g_nvme_info.ready = true;
    return 1;
}

bool nvme_ready(void)
{
    return g_nvme_info.ready;
}

bool nvme_driver_init(void)
{
    pci_device_info_t info;
    uint16_t command;
    uint64_t cap;

    memset(&g_nvme_info, 0, sizeof(g_nvme_info));
    g_nvme_dma_ready = false;
    strcpy(g_nvme_info.status, "nvme: not found");

    if (!pci_find_first(PCI_CLASS_STORAGE, PCI_SUBCLASS_NVM, &info)) {
        log_write(g_nvme_info.status);
        return false;
    }

    g_nvme_info.present = true;
    g_nvme_info.vendor_id = info.vendor_id;
    g_nvme_info.device_id = info.device_id;
    g_nvme_info.bus = info.bus;
    g_nvme_info.slot = info.slot;
    g_nvme_info.func = info.func;
    g_nvme_info.irq = info.interrupt_line;
    g_nvme_info.mmio_base = nvme_bar_base(info.bar0);
    if (g_nvme_info.mmio_base == 0) {
        strcpy(g_nvme_info.status, "nvme: mmio bar missing");
        log_write(g_nvme_info.status);
        return false;
    }

    /* Map the controller registers uncacheable, and refuse the device if that
     * is not possible. mmu_map_identity() would leave the range write-back
     * cached, so doorbell and CC writes could sit in the cache and CC.EN would
     * never be observed to take effect. NVMe BARs are also 64-bit, so the
     * address may legitimately lie above the range this kernel maps. */
    if (!mmu_map_device_identity(g_nvme_info.mmio_base, 0x4000)) {
        strcpy(g_nvme_info.status, "nvme: mmio bar outside mapped window");
        log_write(g_nvme_info.status);
        return false;
    }
    command = pci_config_read16(info.bus, info.slot, info.func, PCI_COMMAND_OFFSET);
    command |= PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;
    pci_config_write16(info.bus, info.slot, info.func, PCI_COMMAND_OFFSET, command);

    g_nvme_info.cap_lo = nvme_read32(NVME_REG_CAP);
    g_nvme_info.cap_hi = nvme_read32(NVME_REG_CAP + 4);
    g_nvme_info.version = nvme_read32(NVME_REG_VS);
    g_nvme_info.csts = nvme_read32(NVME_REG_CSTS);
    cap = (uint64_t) g_nvme_info.cap_lo | ((uint64_t) g_nvme_info.cap_hi << 32);
    g_nvme_info.max_queue_entries = (uint16_t) ((cap & 0xFFFFu) + 1u);
    g_nvme_info.doorbell_stride = (uint8_t) ((cap >> 32) & 0xFu);
    g_nvme_info.mmio_ready = true;

    if (!nvme_init_queues()) {
        strcpy(g_nvme_info.status, "nvme: queue init failed");
        log_write(g_nvme_info.status);
        return true;
    }

    if (nvme_probe() == 0) {
        strcpy(g_nvme_info.status, "nvme: controller ready; no namespace");
        log_write(g_nvme_info.status);
        return true;
    }

    strcpy(g_nvme_info.status, "nvme: namespace ready");
    log_write(g_nvme_info.status);
    return true;
}

void nvme_shutdown(void)
{
    if (g_nvme_info.present) {
        if (g_nvme_info.mmio_ready) {
            uint32_t cc = nvme_read32(NVME_REG_CC);

            cc &= ~NVME_CC_EN;
            cc |= NVME_CC_SHN_NORMAL;
            nvme_write32(NVME_REG_CC, cc);
        }
        g_nvme_info.ready = false;
        strcpy(g_nvme_info.status, "nvme: shutdown");
        log_write(g_nvme_info.status);
    }
}

const nvme_info_t *nvme_info(void)
{
    return &g_nvme_info;
}

const char *nvme_status(void)
{
    return g_nvme_info.status;
}
