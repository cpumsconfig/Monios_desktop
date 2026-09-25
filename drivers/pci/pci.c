#include "common.h"
#include "kernel.h"
#include "pci.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t address =
        (1u << 31) |
        ((uint32_t) bus << 16) |
        ((uint32_t) slot << 11) |
        ((uint32_t) func << 8) |
        (offset & 0xFCu);

    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t value = pci_config_read32(bus, slot, func, offset);
    return (uint16_t) ((value >> ((offset & 2u) * 8u)) & 0xFFFFu);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t value = pci_config_read32(bus, slot, func, offset);
    return (uint8_t) ((value >> ((offset & 3u) * 8u)) & 0xFFu);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value)
{
    uint32_t address =
        (1u << 31) |
        ((uint32_t) bus << 16) |
        ((uint32_t) slot << 11) |
        ((uint32_t) func << 8) |
        (offset & 0xFCu);

    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value)
{
    uint32_t current = pci_config_read32(bus, slot, func, offset);
    uint32_t shift = (offset & 2u) * 8u;
    uint32_t mask = 0xFFFFu << shift;

    current = (current & ~mask) | ((uint32_t) value << shift);
    pci_config_write32(bus, slot, func, offset, current);
}

uint8_t pci_find_capability(uint8_t bus,
                            uint8_t slot,
                            uint8_t func,
                            uint8_t capability_id)
{
    uint8_t pointer = pci_config_read8(bus, slot, func, 0x34);

    for (uint8_t i = 0; i < 48 && pointer >= 0x40; i++) {
        uint8_t next;

        pointer &= 0xFCU;
        if (pointer == 0) {
            return 0;
        }
        if (pci_config_read8(bus, slot, func, pointer) == capability_id) {
            return pointer;
        }
        next = pci_config_read8(bus, slot, func, (uint8_t) (pointer + 1U));
        if (next == pointer) {
            return 0;
        }
        pointer = next;
    }
    return 0;
}

static void pci_fill_device_info(uint8_t bus, uint8_t slot, uint8_t func, pci_device_info_t *info)
{
    uint32_t *raw_bars;

    memset(info, 0, sizeof(*info));
    info->bus = bus;
    info->slot = slot;
    info->func = func;
    info->vendor_id = pci_config_read16(bus, slot, func, 0x00);
    info->device_id = pci_config_read16(bus, slot, func, 0x02);
    info->revision = pci_config_read8(bus, slot, func, 0x08);
    info->prog_if = pci_config_read8(bus, slot, func, 0x09);
    info->subclass = pci_config_read8(bus, slot, func, 0x0A);
    info->class_code = pci_config_read8(bus, slot, func, 0x0B);
    info->command = pci_config_read16(bus, slot, func, 0x04);
    info->header_type = pci_config_read8(bus, slot, func, 0x0E);
    raw_bars = &info->bar0;
    for (uint8_t i = 0; i < 6; i++) {
        raw_bars[i] = pci_config_read32(bus, slot, func, (uint8_t) (0x10U + i * 4U));
    }
    for (uint8_t i = 0; i < 6; i++) {
        uint32_t raw = raw_bars[i];
        uint64_t address;
        uint8_t bar_index = i;

        if (raw == 0) {
            continue;
        }
        if ((raw & 1U) != 0) {
            address = raw & 0xFFFFFFFCULL;
        } else {
            address = raw & 0xFFFFFFF0ULL;
            if ((raw & 0x06U) == 0x04U && i < 5) {
                address |= (uint64_t) raw_bars[i + 1] << 32;
                i++;
            }
        }
        info->bar_address[bar_index] = address;
    }
    info->interrupt_line = pci_config_read8(bus, slot, func, 0x3C);
    info->interrupt_pin = pci_config_read8(bus, slot, func, 0x3D);
    info->capability_pointer = pci_config_read8(bus, slot, func, 0x34);
    info->power_management_capable =
        pci_find_capability(bus, slot, func, 0x01U) != 0;
    info->msi_capable =
        pci_find_capability(bus, slot, func, 0x05U) != 0;
    info->msix_capable =
        pci_find_capability(bus, slot, func, 0x11U) != 0;
    if (info->power_management_capable) {
        uint8_t pm = pci_find_capability(bus, slot, func, 0x01U);

        info->power_state = pci_config_read16(bus, slot, func, (uint8_t) (pm + 4U)) & 0x03U;
    }
}

void pci_enumerate(pci_enum_callback_t callback, void *ctx)
{
    pci_device_info_t info;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint16_t vendor_id = pci_config_read16((uint8_t) bus, slot, func, 0x00);

                if (vendor_id == 0xFFFFu) {
                    if (func == 0) {
                        break;
                    }
                    continue;
                }

                pci_fill_device_info((uint8_t) bus, slot, func, &info);
                if (callback != NULL && !callback(&info, ctx)) {
                    return;
                }
            }
        }
    }
}

typedef struct {
    uint8_t class_code;
    uint8_t subclass;
    pci_device_info_t *out_info;
    bool found;
} pci_find_ctx_t;

static bool pci_find_first_callback(const pci_device_info_t *info, void *ctx_ptr)
{
    pci_find_ctx_t *ctx = (pci_find_ctx_t *) ctx_ptr;

    if (info->class_code == ctx->class_code && info->subclass == ctx->subclass) {
        *ctx->out_info = *info;
        ctx->found = true;
        return false;
    }
    return true;
}

bool pci_find_first(uint8_t class_code, uint8_t subclass, pci_device_info_t *out_info)
{
    pci_find_ctx_t ctx;

    if (out_info == NULL) {
        return false;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.class_code = class_code;
    ctx.subclass = subclass;
    ctx.out_info = out_info;
    pci_enumerate(pci_find_first_callback, &ctx);
    return ctx.found;
}

static void pci_append_hex8(char *dst, uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    for (uint8_t i = 0; i < 8; i++) {
        dst[i] = hex[(value >> ((7 - i) * 4)) & 0xF];
    }
    dst[8] = '\0';
}

static bool pci_log_callback(const pci_device_info_t *info, void *ctx)
{
    char line[96] = "pci: ";
    char hex[9];
    (void) ctx;

    pci_append_hex8(hex, ((uint32_t) info->vendor_id << 16) | info->device_id);
    strcpy(line + 5, hex);
    strcpy(line + 13, " class=");
    pci_append_hex8(hex, ((uint32_t) info->class_code << 24) | ((uint32_t) info->subclass << 16) | ((uint32_t) info->prog_if << 8) | info->revision);
    strcpy(line + 20, hex);
    log_write(line);
    return true;
}

void pci_log_devices(void)
{
    pci_enumerate(pci_log_callback, NULL);
}

/* =========================================================================
 * New unified PCI driver interface.
 * ========================================================================= */

static uint32_t g_pci_device_count;
static char g_pci_status[64];

typedef struct {
    uint16_t vendor;
    uint16_t device;
    pci_device_info_t *out;
    bool found;
} pci_vd_ctx_t;

static bool pci_vd_callback(const pci_device_info_t *info, void *ctx)
{
    pci_vd_ctx_t *c = (pci_vd_ctx_t *) ctx;
    if (info->vendor_id == c->vendor && info->device_id == c->device) {
        *c->out = *info;
        c->found = true;
        return false;
    }
    return true;
}

bool pci_find_by_vendor_device(uint16_t vendor, uint16_t device,
                               pci_device_info_t *out_info)
{
    pci_vd_ctx_t ctx;
    if (out_info == NULL) return false;
    ctx.vendor = vendor;
    ctx.device = device;
    ctx.out = out_info;
    ctx.found = false;
    pci_enumerate(pci_vd_callback, &ctx);
    return ctx.found;
}

typedef struct {
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    pci_device_info_t *out;
    bool found;
} pci_class_ctx_t;

static bool pci_class_callback(const pci_device_info_t *info, void *ctx)
{
    pci_class_ctx_t *c = (pci_class_ctx_t *) ctx;
    if (info->class_code == c->class_code && info->subclass == c->subclass) {
        if (c->prog_if == 0xFFu || info->prog_if == c->prog_if) {
            *c->out = *info;
            c->found = true;
            return false;
        }
    }
    return true;
}

bool pci_find_by_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                      pci_device_info_t *out_info)
{
    pci_class_ctx_t ctx;
    if (out_info == NULL) return false;
    ctx.class_code = class_code;
    ctx.subclass = subclass;
    ctx.prog_if = prog_if;
    ctx.out = out_info;
    ctx.found = false;
    pci_enumerate(pci_class_callback, &ctx);
    return ctx.found;
}

/* Command register bits */
#define PCI_CMD_IO_SPACE      0x0001u
#define PCI_CMD_MEM_SPACE     0x0002u
#define PCI_CMD_BUS_MASTER    0x0004u

void pci_enable_bus_master(uint8_t bus, uint8_t slot, uint8_t func)
{
    uint16_t cmd = pci_config_read16(bus, slot, func, 0x04);
    cmd |= PCI_CMD_BUS_MASTER;
    pci_config_write16(bus, slot, func, 0x04, cmd);
}

void pci_enable_memory(uint8_t bus, uint8_t slot, uint8_t func)
{
    uint16_t cmd = pci_config_read16(bus, slot, func, 0x04);
    cmd |= PCI_CMD_MEM_SPACE;
    pci_config_write16(bus, slot, func, 0x04, cmd);
}

void pci_enable_io(uint8_t bus, uint8_t slot, uint8_t func)
{
    uint16_t cmd = pci_config_read16(bus, slot, func, 0x04);
    cmd |= PCI_CMD_IO_SPACE;
    pci_config_write16(bus, slot, func, 0x04, cmd);
}

uint64_t pci_get_bar(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index,
                     bool *out_is_io, bool *out_is_64bit)
{
    uint32_t raw;
    bool is_io = false;
    bool is_64 = false;
    uint64_t addr;

    if (bar_index > 5u) {
        return 0u;
    }
    raw = pci_config_read32(bus, slot, func, (uint8_t) (0x10u + bar_index * 4u));
    if (raw == 0u) {
        return 0u;
    }
    is_io = (raw & 1u) != 0u;
    if (is_io) {
        addr = (uint64_t) (raw & 0xFFFFFFFCu);
    } else {
        addr = (uint64_t) (raw & 0xFFFFFFF0u);
        if ((raw & 0x6u) == 0x4u) {
            is_64 = true;
            if (bar_index < 5u) {
                uint32_t high = pci_config_read32(bus, slot, func,
                                                   (uint8_t) (0x10u + (bar_index + 1u) * 4u));
                addr |= (uint64_t) high << 32;
            }
        }
    }
    if (out_is_io) *out_is_io = is_io;
    if (out_is_64bit) *out_is_64bit = is_64;
    return addr;
}

void pci_set_power_state(uint8_t bus, uint8_t slot, uint8_t func, uint8_t state)
{
    uint8_t pm = pci_find_capability(bus, slot, func, 0x01u);
    if (pm == 0u) {
        return;
    }
    uint16_t ctrl = pci_config_read16(bus, slot, func, (uint8_t) (pm + 4u));
    ctrl &= (uint16_t) ~0x0003u;
    ctrl |= (uint16_t) (state & 0x03u);
    pci_config_write16(bus, slot, func, (uint8_t) (pm + 4u), ctrl);
}

uint8_t pci_msi_cap_offset(uint8_t bus, uint8_t slot, uint8_t func)
{
    return pci_find_capability(bus, slot, func, 0x05u);
}

uint8_t pci_msix_cap_offset(uint8_t bus, uint8_t slot, uint8_t func)
{
    return pci_find_capability(bus, slot, func, 0x11u);
}

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    return pci_config_read32(bus, slot, func, offset);
}

void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value)
{
    pci_config_write32(bus, slot, func, offset, value);
}

/* Count devices during enumeration (with bridge recursion). */
static bool pci_count_callback(const pci_device_info_t *info, void *ctx)
{
    uint32_t *count = (uint32_t *) ctx;
    (*count)++;
    /* PCI-to-PCI bridge (class 0x06, subclass 0x04): recurse into secondary bus. */
    if (info->class_code == 0x06u && info->subclass == 0x04u) {
        uint8_t secondary = pci_config_read8(info->bus, info->slot, info->func, 0x19);
        for (uint8_t s = 0; s < 32; s++) {
            uint16_t vid = pci_config_read16(secondary, s, 0u, 0x00);
            if (vid == 0xFFFFu) continue;
            (*count)++;
        }
    }
    return true;
}

uint32_t pci_probe(void)
{
    g_pci_device_count = 0u;
    pci_enumerate(pci_count_callback, &g_pci_device_count);
    if (g_pci_device_count == 0u) {
        strcpy(g_pci_status, "pci: not found");
    } else {
        strcpy(g_pci_status, "pci: enumerated");
    }
    log_write(g_pci_status);
    return g_pci_device_count;
}

uint32_t pci_device_count(void)
{
    if (g_pci_device_count == 0u) {
        pci_probe();
    }
    return g_pci_device_count;
}

void pci_shutdown(void)
{
    strcpy(g_pci_status, "pci: shutdown");
}

const char *pci_status(void)
{
    if (g_pci_status[0] == '\0') {
        strcpy(g_pci_status, "pci: not found");
    }
    return g_pci_status;
}
