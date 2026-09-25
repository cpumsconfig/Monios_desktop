#include "ahci.h"
#include "common.h"
#include "dma.h"
#include "kernel.h"
#include "mmu.h"
#include "pci.h"

#define PCI_CLASS_STORAGE        0x01
#define PCI_SUBCLASS_SATA        0x06
#define PCI_COMMAND_OFFSET       0x04
#define PCI_COMMAND_MEMORY       0x0002
#define PCI_COMMAND_BUS_MASTER   0x0004

#define AHCI_REG_CAP             0x00
#define AHCI_REG_GHC             0x04
#define AHCI_REG_PI              0x0C
#define AHCI_REG_VS              0x10

#define AHCI_GHC_AE              0x80000000U

#define AHCI_PORT_BASE           0x100
#define AHCI_PORT_STRIDE         0x80
#define AHCI_PX_CLB              0x00
#define AHCI_PX_CLBU             0x04
#define AHCI_PX_FB               0x08
#define AHCI_PX_FBU              0x0C
#define AHCI_PX_IS               0x10
#define AHCI_PX_IE               0x14
#define AHCI_PX_CMD              0x18
#define AHCI_PX_TFD              0x20
#define AHCI_PX_SIG              0x24
#define AHCI_PX_SSTS             0x28
#define AHCI_PX_SERR             0x30
#define AHCI_PX_SACT             0x34
#define AHCI_PX_CI               0x38

#define AHCI_PX_CMD_ST           0x00000001U
#define AHCI_PX_CMD_FRE          0x00000010U
#define AHCI_PX_CMD_FR           0x00004000U
#define AHCI_PX_CMD_CR           0x00008000U

#define AHCI_PX_IS_TFES          0x40000000U
#define AHCI_PX_TFD_ERR          0x00000001U
#define AHCI_PX_TFD_DRQ          0x00000008U
#define AHCI_PX_TFD_BSY          0x00000080U

#define AHCI_SSTS_DET_MASK       0x0000000FU
#define AHCI_SSTS_IPM_MASK       0x00000F00U
#define AHCI_SSTS_DET_PRESENT    0x00000003U
#define AHCI_SSTS_IPM_ACTIVE     0x00000100U

#define AHCI_SIG_ATA             0x00000101U

#define AHCI_CMD_HEADER_COUNT    32U
#define AHCI_CMD_HEADER_SIZE     32U
#define AHCI_COMMAND_LIST_SIZE   (AHCI_CMD_HEADER_COUNT * AHCI_CMD_HEADER_SIZE)
#define AHCI_FIS_SIZE            256U
#define AHCI_COMMAND_TABLE_SIZE  256U

#define FIS_TYPE_REG_H2D         0x27U
#define FIS_REG_H2D_COMMAND      0x80U
#define ATA_CMD_IDENTIFY_DEVICE  0xECU
#define ATA_CMD_READ_DMA_EXT     0x25U
#define ATA_CMD_WRITE_DMA_EXT    0x35U
#define ATA_DEVICE_LBA           0x40U

#define AHCI_WAIT_SHORT          100000U
#define AHCI_WAIT_LONG           5000000U

typedef struct {
    uint16_t flags;
    uint16_t prdt_length;
    volatile uint32_t prd_byte_count;
    uint32_t command_table_base;
    uint32_t command_table_base_upper;
    uint32_t reserved[4];
} __attribute__((packed)) ahci_command_header_t;

typedef struct {
    uint32_t data_base;
    uint32_t data_base_upper;
    uint32_t reserved;
    uint32_t byte_count_ioc;
} __attribute__((packed)) ahci_prdt_entry_t;

typedef struct {
    uint8_t command_fis[64];
    uint8_t atapi_command[16];
    uint8_t reserved[48];
    ahci_prdt_entry_t prdt[1];
} __attribute__((packed)) ahci_command_table_t;

typedef struct {
    bool used;
    uint8_t hw_port;
    uint32_t ssts_last;
    uint32_t slot_bitmap; /* NCQ framework: bits = command slots in flight */
    dma_buffer_t cmd_list_dma;
    dma_buffer_t fis_dma;
    dma_buffer_t cmd_table_dma;
    ahci_command_header_t *headers;
    ahci_command_table_t *table;
} ahci_port_state_t;

static ahci_info_t g_ahci_info;
static ahci_port_state_t g_ahci_ports[AHCI_MAX_PORTS];
static dma_buffer_t g_ahci_data_dma;
static uint8_t *g_ahci_data;
static bool g_ahci_dma_ready;

static uint64_t ahci_bar_base(const pci_device_info_t *info)
{
    if (info == NULL) {
        return 0;
    }
    if (info->bar_address[5] != 0) {
        return info->bar_address[5];
    }
    if ((info->bar5 & 1U) != 0) {
        return 0;
    }
    return info->bar5 & 0xFFFFFFF0ULL;
}

static uint32_t ahci_port_offset(uint8_t port, uint32_t offset)
{
    return AHCI_PORT_BASE + (uint32_t) port * AHCI_PORT_STRIDE + offset;
}

static uint32_t ahci_read32(uint32_t offset)
{
    volatile uint32_t *ptr = (volatile uint32_t *) (uintptr_t) (g_ahci_info.abar + offset);

    return *ptr;
}

static void ahci_write32(uint32_t offset, uint32_t value)
{
    volatile uint32_t *ptr = (volatile uint32_t *) (uintptr_t) (g_ahci_info.abar + offset);

    *ptr = value;
}

static uint32_t ahci_port_read32(uint8_t port, uint32_t offset)
{
    return ahci_read32(ahci_port_offset(port, offset));
}

static void ahci_port_write32(uint8_t port, uint32_t offset, uint32_t value)
{
    ahci_write32(ahci_port_offset(port, offset), value);
}

static void ahci_memory_barrier(void)
{
    __asm__ __volatile__("mfence" ::: "memory");
}

static bool ahci_wait_clear(uint8_t port, uint32_t offset, uint32_t mask, uint32_t limit)
{
    for (uint32_t i = 0; i < limit; i++) {
        if ((ahci_port_read32(port, offset) & mask) == 0) {
            return true;
        }
        io_wait();
    }
    return false;
}

static bool ahci_wait_port_not_busy(uint8_t port)
{
    for (uint32_t i = 0; i < AHCI_WAIT_LONG; i++) {
        uint32_t tfd = ahci_port_read32(port, AHCI_PX_TFD);

        if ((tfd & (AHCI_PX_TFD_BSY | AHCI_PX_TFD_DRQ)) == 0) {
            return true;
        }
        if ((tfd & AHCI_PX_TFD_ERR) != 0) {
            g_ahci_info.last_error = tfd;
            return false;
        }
        io_wait();
    }
    g_ahci_info.last_error = ahci_port_read32(port, AHCI_PX_TFD);
    return false;
}

static bool ahci_stop_port(uint8_t port)
{
    uint32_t cmd = ahci_port_read32(port, AHCI_PX_CMD);

    cmd &= ~AHCI_PX_CMD_ST;
    ahci_port_write32(port, AHCI_PX_CMD, cmd);
    if (!ahci_wait_clear(port, AHCI_PX_CMD, AHCI_PX_CMD_CR, AHCI_WAIT_SHORT)) {
        g_ahci_info.last_error = ahci_port_read32(port, AHCI_PX_CMD);
        return false;
    }

    cmd &= ~AHCI_PX_CMD_FRE;
    ahci_port_write32(port, AHCI_PX_CMD, cmd);
    if (!ahci_wait_clear(port, AHCI_PX_CMD, AHCI_PX_CMD_FR, AHCI_WAIT_SHORT)) {
        g_ahci_info.last_error = ahci_port_read32(port, AHCI_PX_CMD);
        return false;
    }
    return true;
}

static void ahci_start_port(uint8_t port)
{
    uint32_t cmd = ahci_port_read32(port, AHCI_PX_CMD);

    cmd |= AHCI_PX_CMD_FRE;
    ahci_port_write32(port, AHCI_PX_CMD, cmd);
    cmd |= AHCI_PX_CMD_ST;
    ahci_port_write32(port, AHCI_PX_CMD, cmd);
}

static bool ahci_alloc_dma(void)
{
    uint64_t checkpoint = dma_checkpoint();

    for (uint32_t i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!dma_alloc(AHCI_COMMAND_LIST_SIZE, 1024, 0xFFFFFFFFULL,
                       &g_ahci_ports[i].cmd_list_dma) ||
            !dma_alloc(AHCI_FIS_SIZE, 256, 0xFFFFFFFFULL,
                       &g_ahci_ports[i].fis_dma) ||
            !dma_alloc(AHCI_COMMAND_TABLE_SIZE, 128, 0xFFFFFFFFULL,
                       &g_ahci_ports[i].cmd_table_dma)) {
            dma_rewind(checkpoint);
            return false;
        }
        g_ahci_ports[i].headers =
            (ahci_command_header_t *) g_ahci_ports[i].cmd_list_dma.virtual_address;
        g_ahci_ports[i].table =
            (ahci_command_table_t *) g_ahci_ports[i].cmd_table_dma.virtual_address;
    }
    if (!dma_alloc((uint64_t) AHCI_SECTOR_SIZE * AHCI_MAX_SECTORS_PER_IO,
                   16, 0xFFFFFFFFULL, &g_ahci_data_dma)) {
        dma_rewind(checkpoint);
        return false;
    }
    g_ahci_data = (uint8_t *) g_ahci_data_dma.virtual_address;
    g_ahci_dma_ready = true;
    return true;
}

static bool ahci_port_link_active(uint8_t port)
{
    uint32_t ssts = ahci_port_read32(port, AHCI_PX_SSTS);

    return (ssts & AHCI_SSTS_DET_MASK) == AHCI_SSTS_DET_PRESENT &&
           (ssts & AHCI_SSTS_IPM_MASK) == AHCI_SSTS_IPM_ACTIVE;
}

/* NCQ framework: allocate a command slot (simplified: non-NCQ, slot 0). */
static uint8_t ahci_alloc_slot(ahci_port_state_t *ps)
{
    if ((ps->slot_bitmap & 1U) != 0) {
        return 0xFFU;
    }
    ps->slot_bitmap |= 1U;
    return 0U;
}

static void ahci_free_slot(ahci_port_state_t *ps, uint8_t slot)
{
    if (slot < 32U) {
        ps->slot_bitmap &= ~(1U << slot);
    }
}

static bool ahci_configure_port(uint8_t slot)
{
    ahci_port_state_t *ps = &g_ahci_ports[slot];
    uint8_t port = ps->hw_port;

    if (!ahci_stop_port(port)) {
        strcpy(g_ahci_info.status, "ahci: port stop timeout");
        return false;
    }

    ahci_port_write32(port, AHCI_PX_CLB, (uint32_t) ps->cmd_list_dma.physical_address);
    ahci_port_write32(port, AHCI_PX_CLBU, (uint32_t) (ps->cmd_list_dma.physical_address >> 32));
    ahci_port_write32(port, AHCI_PX_FB, (uint32_t) ps->fis_dma.physical_address);
    ahci_port_write32(port, AHCI_PX_FBU, (uint32_t) (ps->fis_dma.physical_address >> 32));
    ahci_port_write32(port, AHCI_PX_IE, 0);
    ahci_port_write32(port, AHCI_PX_SERR, 0xFFFFFFFFU);
    ahci_port_write32(port, AHCI_PX_IS, 0xFFFFFFFFU);

    ahci_start_port(port);
    return true;
}

static void ahci_prepare_prdt(ahci_port_state_t *ps, uint32_t bytes)
{
    ps->table->prdt[0].data_base = (uint32_t) g_ahci_data_dma.physical_address;
    ps->table->prdt[0].data_base_upper =
        (uint32_t) (g_ahci_data_dma.physical_address >> 32);
    ps->table->prdt[0].reserved = 0;
    ps->table->prdt[0].byte_count_ioc = (bytes - 1U) | 0x80000000U;
}

static void ahci_build_fis(ahci_port_state_t *ps, uint8_t command,
                           uint64_t lba, uint16_t count)
{
    uint8_t *fis = ps->table->command_fis;

    memset(fis, 0, 64);
    fis[0] = FIS_TYPE_REG_H2D;
    fis[1] = FIS_REG_H2D_COMMAND;
    fis[2] = command;
    fis[4] = (uint8_t) lba;
    fis[5] = (uint8_t) (lba >> 8);
    fis[6] = (uint8_t) (lba >> 16);
    fis[7] = ATA_DEVICE_LBA;
    fis[8] = (uint8_t) (lba >> 24);
    fis[9] = (uint8_t) (lba >> 32);
    fis[10] = (uint8_t) (lba >> 40);
    fis[12] = (uint8_t) count;
    fis[13] = (uint8_t) (count >> 8);
}

static bool ahci_issue_data_command(uint8_t slot, uint8_t command,
                                    uint64_t lba, uint16_t count, uint32_t bytes)
{
    ahci_port_state_t *ps = &g_ahci_ports[slot];
    uint8_t port = ps->hw_port;
    ahci_command_header_t *header;
    uint8_t cmd_slot;
    uint32_t ci_mask;

    if (!g_ahci_dma_ready || bytes == 0 || bytes > g_ahci_data_dma.size) {
        g_ahci_info.last_error = 0xBAD00001U;
        return false;
    }
    cmd_slot = ahci_alloc_slot(ps);
    if (cmd_slot == 0xFFU) {
        g_ahci_info.last_error = 0xBAD00002U;
        return false;
    }
    ci_mask = 1U << cmd_slot;

    if (!ahci_wait_port_not_busy(port)) {
        ahci_free_slot(ps, cmd_slot);
        return false;
    }

    header = &ps->headers[cmd_slot];
    memset(header, 0, sizeof(*header));
    memset(ps->table, 0, sizeof(*ps->table));
    header->flags = 5U;
    header->prdt_length = 1U;
    header->command_table_base = (uint32_t) ps->cmd_table_dma.physical_address;
    header->command_table_base_upper =
        (uint32_t) (ps->cmd_table_dma.physical_address >> 32);
    ahci_prepare_prdt(ps, bytes);
    ahci_build_fis(ps, command, lba, count);
    ahci_memory_barrier();

    ahci_port_write32(port, AHCI_PX_SERR, 0xFFFFFFFFU);
    ahci_port_write32(port, AHCI_PX_IS, 0xFFFFFFFFU);
    ahci_port_write32(port, AHCI_PX_CI, ci_mask);

    for (uint32_t i = 0; i < AHCI_WAIT_LONG; i++) {
        uint32_t is = ahci_port_read32(port, AHCI_PX_IS);
        uint32_t ci = ahci_port_read32(port, AHCI_PX_CI);

        if ((is & AHCI_PX_IS_TFES) != 0) {
            g_ahci_info.last_error = ahci_port_read32(port, AHCI_PX_TFD);
            ahci_free_slot(ps, cmd_slot);
            return false;
        }
        if ((ci & ci_mask) == 0) {
            g_ahci_info.last_error = ahci_port_read32(port, AHCI_PX_TFD);
            bool ok = (g_ahci_info.last_error &
                       (AHCI_PX_TFD_ERR | AHCI_PX_TFD_BSY | AHCI_PX_TFD_DRQ)) == 0;
            ahci_free_slot(ps, cmd_slot);
            return ok;
        }
        io_wait();
    }

    g_ahci_info.last_error = ahci_port_read32(port, AHCI_PX_CI);
    ahci_free_slot(ps, cmd_slot);
    return false;
}

static void ahci_decode_model(char out[41], const uint16_t identify[256])
{
    uint32_t pos = 0;

    for (uint32_t word = 27; word <= 46 && pos + 1U < 41U; word++) {
        out[pos++] = (char) (identify[word] >> 8);
        out[pos++] = (char) identify[word];
    }
    while (pos > 0 && out[pos - 1U] == ' ') {
        pos--;
    }
    out[pos] = '\0';
    if (out[0] == '\0') {
        strcpy(out, "SATA disk");
    }
}

static uint64_t ahci_identify_capacity(const uint16_t identify[256])
{
    uint64_t capacity = 0;

    if ((identify[83] & (1U << 10)) != 0) {
        capacity = (uint64_t) identify[100] |
                   ((uint64_t) identify[101] << 16) |
                   ((uint64_t) identify[102] << 32) |
                   ((uint64_t) identify[103] << 48);
    }
    if (capacity == 0) {
        capacity = (uint64_t) identify[60] |
                   ((uint64_t) identify[61] << 16);
    }
    return capacity;
}

static bool ahci_identify_device(uint8_t slot)
{
    const uint16_t *identify;

    memset(g_ahci_data, 0, AHCI_SECTOR_SIZE);
    if (!ahci_issue_data_command(slot, ATA_CMD_IDENTIFY_DEVICE, 0, 0, AHCI_SECTOR_SIZE)) {
        strcpy(g_ahci_info.status, "ahci: identify failed");
        return false;
    }

    identify = (const uint16_t *) g_ahci_data;
    ahci_decode_model(g_ahci_info.ports[slot].model, identify);
    g_ahci_info.ports[slot].capacity_sectors = ahci_identify_capacity(identify);
    g_ahci_info.ports[slot].sector_size = AHCI_SECTOR_SIZE;
    g_ahci_info.ports[slot].ready = g_ahci_info.ports[slot].capacity_sectors != 0;
    return g_ahci_info.ports[slot].ready;
}

uint32_t ahci_probe(void)
{
    uint32_t count = 0;

    g_ahci_info.disk_count = 0;
    for (uint8_t slot = 0; slot < AHCI_MAX_PORTS; slot++) {
        g_ahci_ports[slot].used = false;
        g_ahci_ports[slot].slot_bitmap = 0;
        memset(&g_ahci_info.ports[slot], 0, sizeof(g_ahci_info.ports[slot]));
    }

    for (uint8_t hw = 0; hw < 32 && count < AHCI_MAX_PORTS; hw++) {
        uint32_t mask = 1U << hw;
        uint32_t signature;

        if ((g_ahci_info.ports_implemented & mask) == 0) {
            continue;
        }
        g_ahci_info.implemented_port_count++;
        if (!ahci_port_link_active(hw)) {
            continue;
        }
        g_ahci_info.active_port_count++;
        signature = ahci_port_read32(hw, AHCI_PX_SIG);
        if (signature != AHCI_SIG_ATA) {
            continue;
        }

        g_ahci_ports[count].hw_port = hw;
        g_ahci_ports[count].used = true;
        if (!ahci_configure_port(count)) {
            g_ahci_ports[count].used = false;
            continue;
        }
        g_ahci_info.ports[count].present = true;
        g_ahci_info.ports[count].port = hw;
        g_ahci_info.ports[count].ssts = ahci_port_read32(hw, AHCI_PX_SSTS);
        g_ahci_ports[count].ssts_last = g_ahci_info.ports[count].ssts;
        if (ahci_identify_device(count)) {
            if (count == 0) {
                g_ahci_info.ready = true;
                g_ahci_info.active = 0;
                g_ahci_info.capacity_sectors = g_ahci_info.ports[0].capacity_sectors;
                g_ahci_info.sector_size = g_ahci_info.ports[0].sector_size;
                strcpy(g_ahci_info.model, g_ahci_info.ports[0].model);
            }
            count++;
        } else {
            g_ahci_ports[count].used = false;
            g_ahci_info.ports[count].present = false;
        }
    }
    g_ahci_info.disk_count = count;
    return count;
}

bool ahci_read_sectors(uint64_t sector, uint32_t count, void *buffer)
{
    uint8_t slot = g_ahci_info.active;
    ahci_port_t *p = &g_ahci_info.ports[slot];
    uint32_t bytes;

    if (!g_ahci_info.ready || buffer == NULL || count == 0 ||
        count > AHCI_MAX_SECTORS_PER_IO ||
        p->sector_size != AHCI_SECTOR_SIZE) {
        return false;
    }
    if (p->capacity_sectors != 0 &&
        (sector >= p->capacity_sectors ||
         count > p->capacity_sectors - sector)) {
        return false;
    }

    bytes = count * AHCI_SECTOR_SIZE;
    memset(g_ahci_data, 0, bytes);
    if (!ahci_issue_data_command(slot, ATA_CMD_READ_DMA_EXT, sector, (uint16_t) count, bytes)) {
        strcpy(g_ahci_info.status, "ahci: read failed");
        return false;
    }

    memcpy(buffer, g_ahci_data, bytes);
    p->read_ops++;
    g_ahci_info.read_ops++;
    strcpy(g_ahci_info.status, "ahci: disk ready");
    return true;
}

bool ahci_read_sector(uint64_t sector, void *buffer)
{
    return ahci_read_sectors(sector, 1, buffer);
}

bool ahci_write_sectors(uint64_t sector, uint32_t count, const void *buffer)
{
    uint8_t slot = g_ahci_info.active;
    ahci_port_t *p = &g_ahci_info.ports[slot];
    uint32_t bytes;

    if (!g_ahci_info.ready || buffer == NULL || count == 0 ||
        count > AHCI_MAX_SECTORS_PER_IO ||
        p->sector_size != AHCI_SECTOR_SIZE) {
        return false;
    }
    if (p->capacity_sectors != 0 &&
        (sector >= p->capacity_sectors ||
         count > p->capacity_sectors - sector)) {
        return false;
    }

    bytes = count * AHCI_SECTOR_SIZE;
    memcpy(g_ahci_data, buffer, bytes);
    if (!ahci_issue_data_command(slot, ATA_CMD_WRITE_DMA_EXT, sector, (uint16_t) count, bytes)) {
        strcpy(g_ahci_info.status, "ahci: write failed");
        return false;
    }

    p->write_ops++;
    g_ahci_info.write_ops++;
    strcpy(g_ahci_info.status, "ahci: write ok");
    return true;
}

bool ahci_write_sector(uint64_t sector, const void *buffer)
{
    return ahci_write_sectors(sector, 1, buffer);
}

void ahci_check_hotplug(void)
{
    if (!g_ahci_info.mmio_ready) {
        return;
    }
    for (uint8_t slot = 0; slot < g_ahci_info.disk_count; slot++) {
        uint32_t ssts = ahci_port_read32(g_ahci_ports[slot].hw_port, AHCI_PX_SSTS);

        if (ssts != g_ahci_ports[slot].ssts_last) {
            g_ahci_ports[slot].ssts_last = ssts;
            g_ahci_info.ports[slot].ssts = ssts;
        }
    }
}

bool ahci_driver_init(void)
{
    pci_device_info_t info;
    uint16_t command;
    uint8_t sector0[AHCI_SECTOR_SIZE];

    memset(&g_ahci_info, 0, sizeof(g_ahci_info));
    g_ahci_dma_ready = false;
    strcpy(g_ahci_info.status, "ahci: not found");

    if (!pci_find_first(PCI_CLASS_STORAGE, PCI_SUBCLASS_SATA, &info)) {
        log_write(g_ahci_info.status);
        return false;
    }

    g_ahci_info.present = true;
    g_ahci_info.vendor_id = info.vendor_id;
    g_ahci_info.device_id = info.device_id;
    g_ahci_info.bus = info.bus;
    g_ahci_info.slot = info.slot;
    g_ahci_info.func = info.func;
    g_ahci_info.prog_if = info.prog_if;
    g_ahci_info.irq = info.interrupt_line;
    g_ahci_info.abar = ahci_bar_base(&info);
    if (g_ahci_info.abar == 0) {
        strcpy(g_ahci_info.status, "ahci: abar missing");
        log_write(g_ahci_info.status);
        return true;
    }

    mmu_map_device_identity(g_ahci_info.abar, 0x2000);
    command = pci_config_read16(info.bus, info.slot, info.func, PCI_COMMAND_OFFSET);
    command |= PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;
    pci_config_write16(info.bus, info.slot, info.func, PCI_COMMAND_OFFSET, command);

    ahci_write32(AHCI_REG_GHC, ahci_read32(AHCI_REG_GHC) | AHCI_GHC_AE);
    g_ahci_info.cap = ahci_read32(AHCI_REG_CAP);
    g_ahci_info.version = ahci_read32(AHCI_REG_VS);
    g_ahci_info.ports_implemented = ahci_read32(AHCI_REG_PI);
    g_ahci_info.command_slots = (uint8_t) (((g_ahci_info.cap >> 8) & 0x1FU) + 1U);
    if (g_ahci_info.command_slots > AHCI_CMD_HEADER_COUNT) {
        g_ahci_info.command_slots = AHCI_CMD_HEADER_COUNT;
    }
    g_ahci_info.mmio_ready = true;

    if (!ahci_alloc_dma()) {
        strcpy(g_ahci_info.status, "ahci: dma alloc failed");
        log_write(g_ahci_info.status);
        return true;
    }

    uint32_t disks = ahci_probe();
    if (disks == 0) {
        strcpy(g_ahci_info.status, "ahci: controller ready; no sata disk");
        log_write(g_ahci_info.status);
        return true;
    }

    if (ahci_read_sector(0, sector0)) {
        g_ahci_info.sector0_signature =
            (uint16_t) sector0[510] | ((uint16_t) sector0[511] << 8);
        kernel_log_hex_u32("ahci: sector0 signature=", g_ahci_info.sector0_signature);
        strcpy(g_ahci_info.status, "ahci: disk ready");
    } else {
        g_ahci_info.ready = false;
        strcpy(g_ahci_info.status, "ahci: identify ready; sector read failed");
    }
    log_write(g_ahci_info.status);
    return true;
}

void ahci_shutdown(void)
{
    if (g_ahci_info.present) {
        for (uint8_t slot = 0; slot < g_ahci_info.disk_count; slot++) {
            (void) ahci_stop_port(g_ahci_ports[slot].hw_port);
        }
        g_ahci_info.ready = false;
        strcpy(g_ahci_info.status, "ahci: shutdown");
        log_write(g_ahci_info.status);
    }
}

bool ahci_ready(void)
{
    return g_ahci_info.ready;
}

const ahci_info_t *ahci_info(void)
{
    return &g_ahci_info;
}

const char *ahci_status(void)
{
    return g_ahci_info.status;
}
