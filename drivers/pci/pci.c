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

static void pci_fill_device_info(uint8_t bus, uint8_t slot, uint8_t func, pci_device_info_t *info)
{
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
    info->bar0 = pci_config_read32(bus, slot, func, 0x10);
    info->bar1 = pci_config_read32(bus, slot, func, 0x14);
    info->bar2 = pci_config_read32(bus, slot, func, 0x18);
    info->bar3 = pci_config_read32(bus, slot, func, 0x1C);
    info->bar4 = pci_config_read32(bus, slot, func, 0x20);
    info->bar5 = pci_config_read32(bus, slot, func, 0x24);
    info->interrupt_line = pci_config_read8(bus, slot, func, 0x3C);
    info->interrupt_pin = pci_config_read8(bus, slot, func, 0x3D);
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
