#include "acpi.h"
#include "common.h"
#include "interrupt.h"
#include "kernel.h"

#define ACPI_RSDP_LO_START 0x000E0000U
#define ACPI_RSDP_LO_END   0x00100000U
#define ACPI_RSDP_EBDA_PTR 0x0000040EU
#define ACPI_SIG_RSDP_0    0x2052545020445352ULL
#define ACPI_SIG_FACP      0x50434146U
#define ACPI_SIG_DSDT      0x54445344U
#define ACPI_SIG_RSDT      0x54445352U
#define ACPI_SIG_XSDT      0x54445358U
#define ACPI_PM1_SLP_EN    0x2000U
#define ACPI_PM1_PWRBTN_STS 0x0100U
#define ACPI_PM1_PWRBTN_EN  0x0100U
#define ACPI_PM1_WAKE_CLEAR 0x8D00U

typedef struct {
    uint64_t signature;
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t ext_checksum;
    uint8_t reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

typedef struct {
    uint32_t signature;
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_sdt_header_t;

typedef struct {
    acpi_sdt_header_t header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved0;
    uint8_t preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
} __attribute__((packed)) acpi_fadt_min_t;

static uint16_t g_pm1a_cnt;
static uint16_t g_pm1b_cnt;
static uint16_t g_pm1a_evt;
static uint16_t g_pm1b_evt;
static uint16_t g_sci_irq;
static uint16_t g_slp_typa;
static uint16_t g_slp_typb;
static bool g_acpi_ready;
static bool g_acpi_power_button_ready;
static bool g_acpi_power_button_hooked;

static uint8_t acpi_checksum(const void *ptr, uint32_t length)
{
    const uint8_t *bytes = (const uint8_t *) ptr;
    uint8_t sum = 0;

    for (uint32_t i = 0; i < length; i++) {
        sum = (uint8_t) (sum + bytes[i]);
    }
    return sum;
}

static bool acpi_signature_matches(const void *ptr, uint64_t signature)
{
    return *(const uint64_t *) ptr == signature;
}

static const acpi_rsdp_t *acpi_find_rsdp_range(uint32_t start, uint32_t end)
{
    for (uint32_t addr = start; addr + sizeof(acpi_rsdp_t) <= end; addr += 16) {
        const acpi_rsdp_t *rsdp = (const acpi_rsdp_t *) (uint64_t) addr;

        if (acpi_signature_matches(rsdp, ACPI_SIG_RSDP_0) && acpi_checksum(rsdp, 20) == 0) {
            return rsdp;
        }
    }
    return NULL;
}

static const acpi_rsdp_t *acpi_find_rsdp(void)
{
    uint16_t ebda_segment = *(const uint16_t *) (uint64_t) ACPI_RSDP_EBDA_PTR;
    uint32_t ebda_addr = (uint32_t) ebda_segment << 4;
    const acpi_rsdp_t *rsdp = NULL;

    if (ebda_addr >= 0x80000 && ebda_addr < 0xA0000) {
        rsdp = acpi_find_rsdp_range(ebda_addr, ebda_addr + 1024);
        if (rsdp != NULL) {
            return rsdp;
        }
    }
    return acpi_find_rsdp_range(ACPI_RSDP_LO_START, ACPI_RSDP_LO_END);
}

static bool acpi_table_valid(const acpi_sdt_header_t *header)
{
    if (header == NULL || header->length < sizeof(acpi_sdt_header_t)) {
        return false;
    }
    return acpi_checksum(header, header->length) == 0;
}

static const acpi_sdt_header_t *acpi_find_table_rsdt(const acpi_sdt_header_t *rsdt, uint32_t signature)
{
    uint32_t count;
    const uint32_t *entries;

    if (!acpi_table_valid(rsdt) || rsdt->signature != ACPI_SIG_RSDT) {
        return NULL;
    }
    count = (rsdt->length - sizeof(acpi_sdt_header_t)) / sizeof(uint32_t);
    entries = (const uint32_t *) ((const uint8_t *) rsdt + sizeof(acpi_sdt_header_t));
    kernel_log_hex_u32("acpi: rsdt_count=", count);
    for (uint32_t i = 0; i < count; i++) {
        kernel_log_hex_u32("acpi: rsdt_entry=", entries[i]);
    }
    const uint64_t MAX_SAFE_PHYS = 0x00800000ULL; /* 8 MiB */
    for (uint32_t i = 0; i < count; i++) {
        uint64_t phys = (uint64_t) entries[i];
        if (phys == 0 || phys > MAX_SAFE_PHYS) {
            kernel_log_hex_u32("acpi: skipping rsdt_entry_phys=", (uint32_t)(phys & 0xFFFFFFFF));
            continue;
        }
        const acpi_sdt_header_t *table = (const acpi_sdt_header_t *) (uint64_t) phys;

        if (acpi_table_valid(table) && table->signature == signature) {
            return table;
        }
    }
    return NULL;
}

static const acpi_sdt_header_t *acpi_find_table_xsdt(const acpi_sdt_header_t *xsdt, uint32_t signature)
{
    uint32_t count;
    const uint64_t *entries;

    if (!acpi_table_valid(xsdt) || xsdt->signature != ACPI_SIG_XSDT) {
        return NULL;
    }
    count = (xsdt->length - sizeof(acpi_sdt_header_t)) / sizeof(uint64_t);
    entries = (const uint64_t *) ((const uint8_t *) xsdt + sizeof(acpi_sdt_header_t));
    kernel_log_hex_u32("acpi: xsdt_count=", count);
    for (uint32_t i = 0; i < count; i++) {
        kernel_log_hex_u32("acpi: xsdt_entry_lo=", (uint32_t)(entries[i] & 0xFFFFFFFF));
        kernel_log_hex_u32("acpi: xsdt_entry_hi=", (uint32_t)((entries[i] >> 32) & 0xFFFFFFFF));
    }
    const uint64_t MAX_SAFE_PHYS64 = 0x00800000ULL; /* 8 MiB */
    for (uint32_t i = 0; i < count; i++) {
        uint64_t phys = (uint64_t) entries[i];
        if (phys == 0 || phys > MAX_SAFE_PHYS64) {
            kernel_log_hex_u32("acpi: skipping xsdt_entry_phys_lo=", (uint32_t)(phys & 0xFFFFFFFF));
            continue;
        }
        const acpi_sdt_header_t *table = (const acpi_sdt_header_t *) (uint64_t) phys;

        if (acpi_table_valid(table) && table->signature == signature) {
            return table;
        }
    }
    return NULL;
}

static bool acpi_parse_s5(const acpi_sdt_header_t *dsdt)
{
    const uint8_t *aml;
    uint32_t aml_len;

    if (!acpi_table_valid(dsdt) || dsdt->signature != ACPI_SIG_DSDT) {
        return false;
    }
    aml = (const uint8_t *) dsdt + sizeof(acpi_sdt_header_t);
    aml_len = dsdt->length - sizeof(acpi_sdt_header_t);
    for (uint32_t i = 0; i + 8 < aml_len; i++) {
        if (aml[i] == '_' && aml[i + 1] == 'S' && aml[i + 2] == '5' && aml[i + 3] == '_') {
            uint32_t j = i + 4;
            uint8_t pkg_len_bytes;
            uint8_t slp_a;
            uint8_t slp_b;

            while (j < aml_len && aml[j] != 0x12) {
                j++;
            }
            if (j + 5 >= aml_len) {
                continue;
            }
            j++;
            pkg_len_bytes = (uint8_t) (((aml[j] >> 6) & 0x03u) + 1u);
            j += pkg_len_bytes;
            if (j >= aml_len) {
                continue;
            }
            j++;
            if (j >= aml_len) {
                continue;
            }
            if (aml[j] == 0x0A || aml[j] == 0x0B || aml[j] == 0x0C) {
                j++;
            }
            if (j >= aml_len) {
                continue;
            }
            slp_a = aml[j];
            j++;
            if (j >= aml_len) {
                continue;
            }
            if (aml[j] == 0x0A || aml[j] == 0x0B || aml[j] == 0x0C) {
                j++;
            }
            if (j >= aml_len) {
                continue;
            }
            slp_b = aml[j];
            g_slp_typa = (uint16_t) (slp_a << 10);
            g_slp_typb = (uint16_t) (slp_b << 10);
            return true;
        }
    }
    return false;
}

void acpi_init(void)
{
    const acpi_rsdp_t *rsdp = acpi_find_rsdp();
    const acpi_sdt_header_t *fadt_header = NULL;
    const acpi_fadt_min_t *fadt;
    const acpi_sdt_header_t *dsdt;

    g_acpi_ready = false;
    g_pm1a_cnt = 0;
    g_pm1b_cnt = 0;
    g_pm1a_evt = 0;
    g_pm1b_evt = 0;
    g_sci_irq = 0;
    g_slp_typa = 0;
    g_slp_typb = 0;
    g_acpi_power_button_ready = false;
    g_acpi_power_button_hooked = false;

    if (rsdp == NULL) {
        log_write("acpi: rsdp not found");
        return;
    }
    kernel_log_hex_u32("acpi: rsdp_va_lo=", (uint32_t) ((uint64_t) rsdp & 0xFFFFFFFF));
    if (rsdp->revision >= 2) {
        kernel_log_hex_u32("acpi: rsdp_xsdt_addr_lo=", (uint32_t) (rsdp->xsdt_address & 0xFFFFFFFF));
        kernel_log_hex_u32("acpi: rsdp_rsdt_addr=", rsdp->rsdt_address);
    } else {
        kernel_log_hex_u32("acpi: rsdp_rsdt_addr=", rsdp->rsdt_address);
    }
    if (rsdp->revision >= 2 && rsdp->xsdt_address != 0) {
        /* Avoid dereferencing XSDT pointer if it points to an unmapped/high phys address. */
        if ((uint64_t) rsdp->xsdt_address <= 0x00800000ULL) {
            fadt_header = acpi_find_table_xsdt((const acpi_sdt_header_t *) (uint64_t) rsdp->xsdt_address, ACPI_SIG_FACP);
        } else {
            kernel_log_hex_u32("acpi: skipping xsdt_addr=", (uint32_t)(rsdp->xsdt_address & 0xFFFFFFFF));
        }
    }
    if (fadt_header == NULL && rsdp->rsdt_address != 0) {
        /* Avoid dereferencing RSDT pointer if it points to an unmapped/high phys address. */
        if ((uint64_t) rsdp->rsdt_address <= 0x00800000ULL) {
            fadt_header = acpi_find_table_rsdt((const acpi_sdt_header_t *) (uint64_t) rsdp->rsdt_address, ACPI_SIG_FACP);
        } else {
            kernel_log_hex_u32("acpi: skipping rsdt_addr=", (uint32_t)(rsdp->rsdt_address & 0xFFFFFFFF));
        }
    }
    if (fadt_header == NULL || fadt_header->length < sizeof(acpi_fadt_min_t)) {
        log_write("acpi: fadt not found");
        return;
    }

    fadt = (const acpi_fadt_min_t *) fadt_header;
    dsdt = (const acpi_sdt_header_t *) (uint64_t) fadt->dsdt;
    if (!acpi_parse_s5(dsdt)) {
        log_write("acpi: s5 not found");
        return;
    }
    g_pm1a_cnt = (uint16_t) fadt->pm1a_cnt_blk;
    g_pm1b_cnt = (uint16_t) fadt->pm1b_cnt_blk;
    g_pm1a_evt = (uint16_t) fadt->pm1a_evt_blk;
    g_pm1b_evt = (uint16_t) fadt->pm1b_evt_blk;
    g_sci_irq = fadt->sci_int;
    if (g_pm1a_cnt == 0) {
        log_write("acpi: pm1 control block missing");
        return;
    }
    g_acpi_ready = true;
    g_acpi_power_button_ready = g_pm1a_evt != 0 && g_sci_irq < 16;
    kernel_log_hex_u32("acpi: pm1a=", g_pm1a_cnt);
    kernel_log_hex_u32("acpi: slp_typa=", g_slp_typa);
    log_write("acpi: s5 ready");
}

static uint16_t acpi_read_pm1_status(void)
{
    uint16_t status = 0;

    if (g_pm1a_evt != 0) {
        status |= inw(g_pm1a_evt);
    }
    if (g_pm1b_evt != 0) {
        status |= inw(g_pm1b_evt);
    }
    return status;
}

static void acpi_clear_pm1_status(uint16_t status)
{
    if (g_pm1a_evt != 0) {
        outw(g_pm1a_evt, status);
    }
    if (g_pm1b_evt != 0) {
        outw(g_pm1b_evt, status);
    }
}

static bool acpi_sci_interrupt(uint8_t irq, void *ctx)
{
    uint16_t status;
    (void) irq;
    (void) ctx;

    status = acpi_read_pm1_status();
    if ((status & ACPI_PM1_PWRBTN_STS) == 0) {
        return false;
    }
    acpi_clear_pm1_status(ACPI_PM1_PWRBTN_STS);
    log_write("power: acpi power button event");
    kernel_request_shutdown();
    return true;
}

void acpi_enable_power_button(void)
{
    uint16_t enable_port;
    uint16_t enable;

    if (!g_acpi_power_button_ready || g_acpi_power_button_hooked) {
        return;
    }

    acpi_clear_pm1_status(ACPI_PM1_WAKE_CLEAR);
    enable_port = (uint16_t) (g_pm1a_evt + 2u);
    enable = inw(enable_port);
    outw(enable_port, (uint16_t) (enable | ACPI_PM1_PWRBTN_EN));
    if (g_pm1b_evt != 0) {
        enable_port = (uint16_t) (g_pm1b_evt + 2u);
        enable = inw(enable_port);
        outw(enable_port, (uint16_t) (enable | ACPI_PM1_PWRBTN_EN));
    }

    if (interrupt_register_irq_handler((uint8_t) g_sci_irq, acpi_sci_interrupt, NULL)) {
        g_acpi_power_button_hooked = true;
        kernel_log_hex_u32("acpi: sci irq=", g_sci_irq);
        log_write("acpi: power button hook ready");
    } else {
        log_write("acpi: power button hook failed");
    }
}

bool acpi_poweroff(void)
{
    if (g_acpi_ready) {
        kernel_log_hex_u32("acpi: poweroff pm1a=", g_pm1a_cnt);
        kernel_log_hex_u32("acpi: poweroff value=", (uint32_t) (g_slp_typa | ACPI_PM1_SLP_EN));
        outw(g_pm1a_cnt, (uint16_t) (g_slp_typa | ACPI_PM1_SLP_EN));
        if (g_pm1b_cnt != 0) {
            outw(g_pm1b_cnt, (uint16_t) (g_slp_typb | ACPI_PM1_SLP_EN));
        }
        return true;
    }
    return false;
}
