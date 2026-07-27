#include "acpi.h"
#include "common.h"
#include "interrupt.h"
#include "kernel.h"

#define ACPI_RSDP_LO_START 0x000E0000U
#define ACPI_RSDP_LO_END   0x00100000U
#define ACPI_RSDP_EBDA_PTR 0x0000040EU
#define ACPI_SIG_RSDP_0    0x2052545020445352ULL
#define ACPI_SIG_FACP      0x50434146U
#define ACPI_SIG_APIC      0x43495041U
#define ACPI_SIG_DSDT      0x54445344U
#define ACPI_SIG_RSDT      0x54445352U
#define ACPI_SIG_XSDT      0x54445358U
#define ACPI_PM1_SLP_EN    0x2000U
#define ACPI_PM1_SLP_TYP_MASK 0x1C00U
#define ACPI_PM1_SCI_EN    0x0001U
#define ACPI_PM1_PWRBTN_STS 0x0100U
#define ACPI_PM1_PWRBTN_EN  0x0100U
#define ACPI_PM1_WAKE_CLEAR 0x8D00U
#define ACPI_TABLE_PHYS_LIMIT 0xC0000000ULL
#define ACPI_RSDP_MAX_LENGTH 4096U
#define ACPI_GAS_SYSTEM_MEMORY 0U
#define ACPI_GAS_SYSTEM_IO     1U
#define ACPI_RESET_CF9_PORT    0x0CF9U
#define ACPI_RESET_KBD_PORT    0x0064U
#define ACPI_RESET_KBD_COMMAND 0xFEU
#define ACPI_FADT_HAS(fadt, member) ((fadt)->header.length >= (uint32_t) ((uint64_t) &(((acpi_fadt_t *) 0)->member) + sizeof((fadt)->member)))

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
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_evt_len;
    uint8_t pm1_cnt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_blk_len;
    uint8_t gpe1_blk_len;
    uint8_t gpe1_base;
    uint8_t cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alrm;
    uint8_t mon_alrm;
    uint8_t century;
    uint16_t boot_arch_flags;
    uint8_t reserved1;
    uint32_t flags;
} __attribute__((packed)) acpi_fadt_v1_t;

typedef struct {
    acpi_sdt_header_t header;
    uint32_t local_apic_address;
    uint32_t flags;
} __attribute__((packed)) acpi_madt_t;

typedef struct {
    uint8_t type;
    uint8_t length;
} __attribute__((packed)) acpi_madt_entry_header_t;

typedef struct {
    acpi_madt_entry_header_t header;
    uint8_t processor_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed)) acpi_madt_local_apic_t;

typedef struct {
    acpi_madt_entry_header_t header;
    uint16_t reserved;
    uint64_t local_apic_address;
} __attribute__((packed)) acpi_madt_address_override_t;

typedef struct {
    acpi_madt_entry_header_t header;
    uint16_t reserved;
    uint32_t x2apic_id;
    uint32_t flags;
    uint32_t uid;
} __attribute__((packed)) acpi_madt_x2apic_t;

typedef struct {
    uint8_t address_space_id;
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t access_size;
    uint64_t address;
} __attribute__((packed)) acpi_gas_t;

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
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_evt_len;
    uint8_t pm1_cnt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_blk_len;
    uint8_t gpe1_blk_len;
    uint8_t gpe1_base;
    uint8_t cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alrm;
    uint8_t mon_alrm;
    uint8_t century;
    uint16_t boot_arch_flags;
    uint8_t reserved1;
    uint32_t flags;
    acpi_gas_t reset_reg;
    uint8_t reset_value;
    uint16_t arm_boot_arch;
    uint8_t fadt_minor_version;
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
    acpi_gas_t x_pm1a_evt_blk;
    acpi_gas_t x_pm1b_evt_blk;
    acpi_gas_t x_pm1a_cnt_blk;
    acpi_gas_t x_pm1b_cnt_blk;
} __attribute__((packed)) acpi_fadt_t;

static uint16_t g_pm1a_cnt;
static uint16_t g_pm1b_cnt;
static uint16_t g_pm1a_evt;
static uint16_t g_pm1b_evt;
static uint16_t g_sci_irq;
static uint16_t g_slp_typa;
static uint16_t g_slp_typb;
static uint16_t g_s1_slp_typa;
static uint16_t g_s1_slp_typb;
static uint16_t g_s3_slp_typa;
static uint16_t g_s3_slp_typb;
static uint16_t g_sleep_slp_typa;
static uint16_t g_sleep_slp_typb;
static uint8_t g_sleep_state;
static acpi_gas_t g_reset_reg;
static uint8_t g_reset_value;
static bool g_acpi_ready;
static bool g_acpi_madt_ready;
static uint32_t g_acpi_madt_lapic_address;
static uint32_t g_acpi_madt_processor_count;
static uint32_t g_acpi_madt_enabled_processor_count;
static uint32_t g_acpi_madt_enabled_lapic_id_count;
static uint32_t g_acpi_madt_enabled_lapic_ids[ACPI_MADT_CPU_ID_MAX];
static bool g_acpi_power_button_ready;
static bool g_acpi_power_button_hooked;
static bool g_acpi_sleep_ready;
static bool g_acpi_reset_ready;
static acpi_info_t g_acpi_info;
static char g_acpi_status[64];

static void acpi_clear_pm1_status(uint16_t status);

static void acpi_refresh_public_info(void)
{
    g_acpi_info.ready = g_acpi_ready;
    g_acpi_info.madt_ready = g_acpi_madt_ready;
    g_acpi_info.power_button_ready = g_acpi_power_button_ready;
    g_acpi_info.power_button_hooked = g_acpi_power_button_hooked;
    g_acpi_info.sleep_ready = g_acpi_sleep_ready;
    g_acpi_info.reset_ready = g_acpi_reset_ready;
    g_acpi_info.madt_lapic_address = g_acpi_madt_lapic_address;
    g_acpi_info.madt_processor_count = g_acpi_madt_processor_count;
    g_acpi_info.madt_enabled_processor_count = g_acpi_madt_enabled_processor_count;
    g_acpi_info.madt_enabled_lapic_id_count = g_acpi_madt_enabled_lapic_id_count;
    memset(g_acpi_info.madt_enabled_lapic_ids, 0, sizeof(g_acpi_info.madt_enabled_lapic_ids));
    for (uint32_t i = 0; i < g_acpi_madt_enabled_lapic_id_count && i < ACPI_MADT_CPU_ID_MAX; i++) {
        g_acpi_info.madt_enabled_lapic_ids[i] = g_acpi_madt_enabled_lapic_ids[i];
    }
    g_acpi_info.sci_irq = g_sci_irq;
    g_acpi_info.pm1a_cnt = g_pm1a_cnt;
    g_acpi_info.pm1b_cnt = g_pm1b_cnt;
    g_acpi_info.pm1a_evt = g_pm1a_evt;
    g_acpi_info.pm1b_evt = g_pm1b_evt;
    g_acpi_info.slp_typa = g_slp_typa;
    g_acpi_info.slp_typb = g_slp_typb;
    g_acpi_info.sleep_state = g_sleep_state;
    g_acpi_info.sleep_slp_typa = g_sleep_slp_typa;
    g_acpi_info.sleep_slp_typb = g_sleep_slp_typb;
    g_acpi_info.s3_slp_typa = g_s3_slp_typa;
    g_acpi_info.s3_slp_typb = g_s3_slp_typb;
    if (g_acpi_ready) {
        if (g_acpi_sleep_ready && g_acpi_reset_ready) {
            strcpy(g_acpi_status, g_acpi_power_button_hooked ? "acpi: sleep/s5/reset/button ready" : "acpi: sleep/s5/reset ready");
        } else if (g_acpi_sleep_ready) {
            strcpy(g_acpi_status, g_acpi_power_button_hooked ? "acpi: sleep/s5/button ready" : "acpi: sleep/s5 ready");
        } else if (g_acpi_reset_ready) {
            strcpy(g_acpi_status, g_acpi_power_button_hooked ? "acpi: s5/reset/button ready" : "acpi: s5/reset ready");
        } else {
            strcpy(g_acpi_status, g_acpi_power_button_hooked ? "acpi: s5/power button ready" : "acpi: s5 ready");
        }
    } else {
        strcpy(g_acpi_status, "acpi: not ready");
    }
}

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

static bool acpi_rsdp_valid(const acpi_rsdp_t *rsdp)
{
    if (rsdp == NULL || !acpi_signature_matches(rsdp, ACPI_SIG_RSDP_0) || acpi_checksum(rsdp, 20) != 0) {
        return false;
    }
    if (rsdp->revision >= 2) {
        if (rsdp->length < sizeof(acpi_rsdp_t) || rsdp->length > ACPI_RSDP_MAX_LENGTH) {
            return false;
        }
        if (acpi_checksum(rsdp, rsdp->length) != 0) {
            return false;
        }
    }
    return true;
}

static const acpi_rsdp_t *acpi_find_rsdp_range(uint32_t start, uint32_t end)
{
    for (uint32_t addr = start; addr + sizeof(acpi_rsdp_t) <= end; addr += 16) {
        const acpi_rsdp_t *rsdp = (const acpi_rsdp_t *) (uint64_t) addr;

        if (acpi_rsdp_valid(rsdp)) {
            return rsdp;
        }
    }
    return NULL;
}

static const acpi_rsdp_t *acpi_find_rsdp(uint64_t boot_rsdp)
{
    uint16_t ebda_segment = *(const uint16_t *) (uint64_t) ACPI_RSDP_EBDA_PTR;
    uint32_t ebda_addr = (uint32_t) ebda_segment << 4;
    const acpi_rsdp_t *rsdp = NULL;
    if (boot_rsdp != 0) {
        if (boot_rsdp < ACPI_TABLE_PHYS_LIMIT) {
            rsdp = (const acpi_rsdp_t *) (uintptr_t) boot_rsdp;
            if (acpi_rsdp_valid(rsdp)) {
                kernel_log_hex_u32("acpi: boot rsdp=", (uint32_t) (boot_rsdp & 0xFFFFFFFF));
                return rsdp;
            }
            kernel_log_hex_u32("acpi: boot rsdp invalid=", (uint32_t) (boot_rsdp & 0xFFFFFFFF));
        } else {
            kernel_log_hex_u32("acpi: boot rsdp high=", (uint32_t) (boot_rsdp & 0xFFFFFFFF));
        }
    }


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

static void acpi_store_enabled_lapic_id(uint32_t apic_id)
{
    if (g_acpi_madt_enabled_lapic_id_count >= ACPI_MADT_CPU_ID_MAX) {
        return;
    }
    g_acpi_madt_enabled_lapic_ids[g_acpi_madt_enabled_lapic_id_count++] = apic_id;
}

static void acpi_parse_madt(const acpi_sdt_header_t *header)
{
    const acpi_madt_t *madt;
    const uint8_t *cursor;
    const uint8_t *end;

    g_acpi_madt_ready = false;
    g_acpi_madt_lapic_address = 0;
    g_acpi_madt_processor_count = 0;
    g_acpi_madt_enabled_processor_count = 0;
    g_acpi_madt_enabled_lapic_id_count = 0;
    memset(g_acpi_madt_enabled_lapic_ids, 0, sizeof(g_acpi_madt_enabled_lapic_ids));
    if (header == NULL ||
        header->signature != ACPI_SIG_APIC ||
        header->length < sizeof(acpi_madt_t) ||
        !acpi_table_valid(header)) {
        return;
    }

    madt = (const acpi_madt_t *) header;
    g_acpi_madt_lapic_address = madt->local_apic_address;
    cursor = (const uint8_t *) madt + sizeof(acpi_madt_t);
    end = (const uint8_t *) madt + madt->header.length;
    while (cursor + sizeof(acpi_madt_entry_header_t) <= end) {
        const acpi_madt_entry_header_t *entry =
            (const acpi_madt_entry_header_t *) cursor;

        if (entry->length < sizeof(acpi_madt_entry_header_t) ||
            cursor + entry->length > end) {
            break;
        }
        if (entry->type == 0 &&
            entry->length >= sizeof(acpi_madt_local_apic_t)) {
            const acpi_madt_local_apic_t *local =
                (const acpi_madt_local_apic_t *) cursor;

            g_acpi_madt_processor_count++;
            if ((local->flags & 0x3U) != 0) {
                g_acpi_madt_enabled_processor_count++;
                acpi_store_enabled_lapic_id(local->apic_id);
            }
        } else if (entry->type == 5 &&
                   entry->length >= sizeof(acpi_madt_address_override_t)) {
            const acpi_madt_address_override_t *override =
                (const acpi_madt_address_override_t *) cursor;

            if (override->local_apic_address < 0x100000000ULL) {
                g_acpi_madt_lapic_address =
                    (uint32_t) override->local_apic_address;
            }
        } else if (entry->type == 9 &&
                   entry->length >= sizeof(acpi_madt_x2apic_t)) {
            const acpi_madt_x2apic_t *x2apic =
                (const acpi_madt_x2apic_t *) cursor;

            g_acpi_madt_processor_count++;
            if ((x2apic->flags & 0x3U) != 0) {
                g_acpi_madt_enabled_processor_count++;
                acpi_store_enabled_lapic_id(x2apic->x2apic_id);
            }
        }
        cursor += entry->length;
    }

    g_acpi_madt_ready = g_acpi_madt_lapic_address != 0 &&
                        g_acpi_madt_processor_count != 0;
    if (g_acpi_madt_ready) {
        kernel_log_hex_u32("acpi: madt lapic=", g_acpi_madt_lapic_address);
        kernel_log_hex_u32("acpi: madt processors=", g_acpi_madt_processor_count);
        kernel_log_hex_u32("acpi: madt enabled=", g_acpi_madt_enabled_processor_count);
    }
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
    const uint64_t MAX_SAFE_PHYS = ACPI_TABLE_PHYS_LIMIT;
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
    const uint64_t MAX_SAFE_PHYS64 = ACPI_TABLE_PHYS_LIMIT;
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

static bool acpi_skip_pkg_length(const uint8_t *aml, uint32_t aml_len, uint32_t *offset)
{
    uint8_t lead;
    uint8_t byte_count;
    uint32_t length;

    if (aml == NULL || offset == NULL || *offset >= aml_len) {
        return false;
    }
    lead = aml[*offset];
    byte_count = (uint8_t) ((lead >> 6) + 1u);
    if (*offset + byte_count > aml_len) {
        return false;
    }
    length = lead & (byte_count == 1 ? 0x3Fu : 0x0Fu);
    for (uint8_t i = 1; i < byte_count; i++) {
        length |= (uint32_t) aml[*offset + i] << (4 + (i - 1u) * 8u);
    }
    (void) length;
    *offset += byte_count;
    return true;
}

static bool acpi_parse_aml_integer(const uint8_t *aml, uint32_t aml_len, uint32_t *offset, uint8_t *value)
{
    uint8_t op;

    if (aml == NULL || offset == NULL || value == NULL || *offset >= aml_len) {
        return false;
    }
    op = aml[*offset];
    if (op == 0x00) {
        *value = 0;
        (*offset)++;
        return true;
    }
    if (op == 0x01) {
        *value = 1;
        (*offset)++;
        return true;
    }
    if (op == 0x0A) {
        if (*offset + 1 >= aml_len) {
            return false;
        }
        *value = aml[*offset + 1];
        *offset += 2;
        return true;
    }
    if (op == 0x0B) {
        if (*offset + 2 >= aml_len) {
            return false;
        }
        *value = aml[*offset + 1];
        *offset += 3;
        return true;
    }
    if (op == 0x0C) {
        if (*offset + 4 >= aml_len) {
            return false;
        }
        *value = aml[*offset + 1];
        *offset += 5;
        return true;
    }
    if (op == 0x0E) {
        if (*offset + 8 >= aml_len) {
            return false;
        }
        *value = aml[*offset + 1];
        *offset += 9;
        return true;
    }
    *value = op;
    (*offset)++;
    return true;
}

static bool acpi_parse_sleep_state(const acpi_sdt_header_t *dsdt, char state, uint16_t *slp_typa, uint16_t *slp_typb)
{
    const uint8_t *aml;
    uint32_t aml_len;
    char name[4];

    if (!acpi_table_valid(dsdt) || dsdt->signature != ACPI_SIG_DSDT ||
        slp_typa == NULL || slp_typb == NULL) {
        return false;
    }
    name[0] = '_';
    name[1] = 'S';
    name[2] = state;
    name[3] = '_';
    aml = (const uint8_t *) dsdt + sizeof(acpi_sdt_header_t);
    aml_len = dsdt->length - sizeof(acpi_sdt_header_t);
    for (uint32_t i = 0; i + 8 < aml_len; i++) {
        if (aml[i] == (uint8_t) name[0] &&
            aml[i + 1] == (uint8_t) name[1] &&
            aml[i + 2] == (uint8_t) name[2] &&
            aml[i + 3] == (uint8_t) name[3]) {
            uint32_t j = i + 4;
            uint8_t slp_a;
            uint8_t slp_b;

            while (j < aml_len && j < i + 96 && aml[j] != 0x12) {
                j++;
            }
            if (j >= aml_len || aml[j] != 0x12) {
                continue;
            }
            j++;
            if (!acpi_skip_pkg_length(aml, aml_len, &j)) {
                continue;
            }
            /* NumElements byte. */
            j++;
            if (j >= aml_len) {
                continue;
            }
            if (!acpi_parse_aml_integer(aml, aml_len, &j, &slp_a)) {
                continue;
            }
            if (!acpi_parse_aml_integer(aml, aml_len, &j, &slp_b)) {
                slp_b = slp_a;
            }
            *slp_typa = (uint16_t) (slp_a << 10);
            *slp_typb = (uint16_t) (slp_b << 10);
            return true;
        }
    }
    return false;
}

static bool acpi_gas_is_io_port(const acpi_gas_t *gas)
{
    return gas != NULL &&
           gas->address_space_id == ACPI_GAS_SYSTEM_IO &&
           gas->address != 0 &&
           gas->address <= 0xFFFFULL &&
           gas->register_bit_offset == 0;
}

static uint16_t acpi_fadt_io_port(uint32_t legacy_port, const acpi_gas_t *x_port)
{
    if (acpi_gas_is_io_port(x_port)) {
        return (uint16_t) x_port->address;
    }
    if (legacy_port <= 0xFFFFU) {
        return (uint16_t) legacy_port;
    }
    return 0;
}

static uint8_t acpi_gas_effective_width(const acpi_gas_t *gas)
{
    uint8_t width;

    if (gas == NULL) {
        return 0;
    }
    width = gas->register_bit_width;
    if (gas->access_size == 1) {
        width = 8;
    } else if (gas->access_size == 2) {
        width = 16;
    } else if (gas->access_size == 3) {
        width = 32;
    } else if (gas->access_size == 4) {
        width = 64;
    }
    return width;
}

static bool acpi_gas_write(const acpi_gas_t *gas, uint64_t value)
{
    uint8_t width;

    if (gas == NULL || gas->address == 0 || gas->register_bit_offset != 0) {
        return false;
    }
    width = acpi_gas_effective_width(gas);

    if (gas->address_space_id == ACPI_GAS_SYSTEM_IO) {
        if (gas->address > 0xFFFFULL) {
            return false;
        }
        if (width <= 8) {
            outb((uint16_t) gas->address, (uint8_t) value);
            return true;
        }
        if (width <= 16) {
            outw((uint16_t) gas->address, (uint16_t) value);
            return true;
        }
        if (width <= 32) {
            outl((uint16_t) gas->address, (uint32_t) value);
            return true;
        }
        return false;
    }
    if (gas->address_space_id == ACPI_GAS_SYSTEM_MEMORY && gas->address < ACPI_TABLE_PHYS_LIMIT) {
        if (width <= 8) {
            *(volatile uint8_t *) (uintptr_t) gas->address = (uint8_t) value;
            return true;
        }
        if (width <= 16) {
            *(volatile uint16_t *) (uintptr_t) gas->address = (uint16_t) value;
            return true;
        }
        if (width <= 32) {
            *(volatile uint32_t *) (uintptr_t) gas->address = (uint32_t) value;
            return true;
        }
    }
    return false;
}

static bool acpi_gas_supported(const acpi_gas_t *gas)
{
    uint8_t width = acpi_gas_effective_width(gas);

    if (gas == NULL || gas->address == 0 || gas->register_bit_offset != 0) {
        return false;
    }
    if (width == 0 || width > 32) {
        return false;
    }
    if (gas->address_space_id == ACPI_GAS_SYSTEM_IO) {
        return gas->address <= 0xFFFFULL;
    }
    if (gas->address_space_id == ACPI_GAS_SYSTEM_MEMORY) {
        return gas->address < ACPI_TABLE_PHYS_LIMIT;
    }
    return false;
}

static void acpi_enable_controller(const acpi_fadt_t *fadt)
{
    if (fadt == NULL || g_pm1a_cnt == 0) {
        return;
    }
    if ((inw(g_pm1a_cnt) & ACPI_PM1_SCI_EN) != 0) {
        return;
    }
    if (fadt->smi_cmd == 0 || fadt->smi_cmd > 0xFFFFU || fadt->acpi_enable == 0) {
        return;
    }
    outb((uint16_t) fadt->smi_cmd, fadt->acpi_enable);
    for (uint32_t i = 0; i < 100000; i++) {
        if ((inw(g_pm1a_cnt) & ACPI_PM1_SCI_EN) != 0) {
            log_write("acpi: controller enabled");
            return;
        }
        io_wait();
    }
    log_write("acpi: controller enable timeout");
}

static bool acpi_enter_sleep_state(uint16_t slp_typa, uint16_t slp_typb)
{
    uint16_t value;

    if (g_pm1a_cnt == 0) {
        return false;
    }
    acpi_clear_pm1_status(ACPI_PM1_WAKE_CLEAR);
    value = inw(g_pm1a_cnt);
    value &= (uint16_t) ~(ACPI_PM1_SLP_TYP_MASK | ACPI_PM1_SLP_EN);
    value |= (uint16_t) (slp_typa | ACPI_PM1_SLP_EN);
    outw(g_pm1a_cnt, value);
    if (g_pm1b_cnt != 0) {
        value = inw(g_pm1b_cnt);
        value &= (uint16_t) ~(ACPI_PM1_SLP_TYP_MASK | ACPI_PM1_SLP_EN);
        value |= (uint16_t) (slp_typb | ACPI_PM1_SLP_EN);
        outw(g_pm1b_cnt, value);
    }
    return true;
}

void acpi_init(uint64_t boot_rsdp)
{
    const acpi_rsdp_t *rsdp = acpi_find_rsdp(boot_rsdp);
    const acpi_sdt_header_t *fadt_header = NULL;
    const acpi_sdt_header_t *madt_header = NULL;
    const acpi_fadt_t *fadt;
    const acpi_sdt_header_t *dsdt;
    uint64_t dsdt_address;

    g_acpi_ready = false;
    g_acpi_madt_ready = false;
    g_acpi_madt_lapic_address = 0;
    g_acpi_madt_processor_count = 0;
    g_acpi_madt_enabled_processor_count = 0;
    g_acpi_madt_enabled_lapic_id_count = 0;
    memset(g_acpi_madt_enabled_lapic_ids, 0, sizeof(g_acpi_madt_enabled_lapic_ids));
    g_pm1a_cnt = 0;
    g_pm1b_cnt = 0;
    g_pm1a_evt = 0;
    g_pm1b_evt = 0;
    g_sci_irq = 0;
    g_slp_typa = 0;
    g_slp_typb = 0;
    g_s1_slp_typa = 0;
    g_s1_slp_typb = 0;
    g_s3_slp_typa = 0;
    g_s3_slp_typb = 0;
    g_sleep_slp_typa = 0;
    g_sleep_slp_typb = 0;
    g_sleep_state = 0;
    memset(&g_reset_reg, 0, sizeof(g_reset_reg));
    g_reset_value = 0;
    g_acpi_power_button_ready = false;
    g_acpi_power_button_hooked = false;
    g_acpi_sleep_ready = false;
    g_acpi_reset_ready = false;
    acpi_refresh_public_info();

    if (rsdp == NULL) {
        log_write("acpi: rsdp not found");
        acpi_refresh_public_info();
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
        if ((uint64_t) rsdp->xsdt_address < ACPI_TABLE_PHYS_LIMIT) {
            fadt_header = acpi_find_table_xsdt((const acpi_sdt_header_t *) (uint64_t) rsdp->xsdt_address, ACPI_SIG_FACP);
            madt_header = acpi_find_table_xsdt((const acpi_sdt_header_t *) (uint64_t) rsdp->xsdt_address, ACPI_SIG_APIC);
        } else {
            kernel_log_hex_u32("acpi: skipping xsdt_addr=", (uint32_t)(rsdp->xsdt_address & 0xFFFFFFFF));
        }
    }
    if (fadt_header == NULL && rsdp->rsdt_address != 0) {
        /* Avoid dereferencing RSDT pointer if it points to an unmapped/high phys address. */
        if ((uint64_t) rsdp->rsdt_address < ACPI_TABLE_PHYS_LIMIT) {
            fadt_header = acpi_find_table_rsdt((const acpi_sdt_header_t *) (uint64_t) rsdp->rsdt_address, ACPI_SIG_FACP);
            madt_header = acpi_find_table_rsdt((const acpi_sdt_header_t *) (uint64_t) rsdp->rsdt_address, ACPI_SIG_APIC);
        } else {
            kernel_log_hex_u32("acpi: skipping rsdt_addr=", (uint32_t)(rsdp->rsdt_address & 0xFFFFFFFF));
        }
    }
    acpi_parse_madt(madt_header);
    if (fadt_header == NULL || fadt_header->length < sizeof(acpi_fadt_v1_t)) {
        log_write("acpi: fadt not found");
        acpi_refresh_public_info();
        return;
    }

    fadt = (const acpi_fadt_t *) fadt_header;
    dsdt_address = fadt->dsdt;
    if (ACPI_FADT_HAS(fadt, x_dsdt) && fadt->x_dsdt != 0 && fadt->x_dsdt < ACPI_TABLE_PHYS_LIMIT) {
        dsdt_address = fadt->x_dsdt;
    }
    if (dsdt_address == 0 || dsdt_address >= ACPI_TABLE_PHYS_LIMIT) {
        log_write("acpi: dsdt address invalid");
        acpi_refresh_public_info();
        return;
    }
    dsdt = (const acpi_sdt_header_t *) (uintptr_t) dsdt_address;
    if (!acpi_parse_sleep_state(dsdt, '5', &g_slp_typa, &g_slp_typb)) {
        log_write("acpi: s5 not found");
        acpi_refresh_public_info();
        return;
    }
    bool s1_ready = acpi_parse_sleep_state(dsdt, '1', &g_s1_slp_typa, &g_s1_slp_typb);
    bool s3_ready = acpi_parse_sleep_state(dsdt, '3', &g_s3_slp_typa, &g_s3_slp_typb);
    if (s3_ready) {
        g_sleep_state = 3;
        g_sleep_slp_typa = g_s3_slp_typa;
        g_sleep_slp_typb = g_s3_slp_typb;
        g_acpi_sleep_ready = true;
    } else if (s1_ready) {
        g_sleep_state = 1;
        g_sleep_slp_typa = g_s1_slp_typa;
        g_sleep_slp_typb = g_s1_slp_typb;
        g_acpi_sleep_ready = true;
    } else {
        g_sleep_state = 1;
        g_sleep_slp_typa = 0;
        g_sleep_slp_typb = 0;
        g_acpi_sleep_ready = true;
    }
    g_pm1a_cnt = acpi_fadt_io_port(fadt->pm1a_cnt_blk,
                                   ACPI_FADT_HAS(fadt, x_pm1a_cnt_blk) ? &fadt->x_pm1a_cnt_blk : NULL);
    g_pm1b_cnt = acpi_fadt_io_port(fadt->pm1b_cnt_blk,
                                   ACPI_FADT_HAS(fadt, x_pm1b_cnt_blk) ? &fadt->x_pm1b_cnt_blk : NULL);
    g_pm1a_evt = acpi_fadt_io_port(fadt->pm1a_evt_blk,
                                   ACPI_FADT_HAS(fadt, x_pm1a_evt_blk) ? &fadt->x_pm1a_evt_blk : NULL);
    g_pm1b_evt = acpi_fadt_io_port(fadt->pm1b_evt_blk,
                                   ACPI_FADT_HAS(fadt, x_pm1b_evt_blk) ? &fadt->x_pm1b_evt_blk : NULL);
    g_sci_irq = fadt->sci_int;
    if (g_pm1a_cnt == 0) {
        log_write("acpi: pm1 control block missing");
        g_acpi_sleep_ready = false;
        acpi_refresh_public_info();
        return;
    }
    acpi_enable_controller(fadt);
    if (ACPI_FADT_HAS(fadt, reset_reg) && acpi_gas_supported(&fadt->reset_reg)) {
        g_reset_reg = fadt->reset_reg;
        g_reset_value = ACPI_FADT_HAS(fadt, reset_value) ? fadt->reset_value : 0x06;
        g_acpi_reset_ready = true;
    }
    g_acpi_ready = true;
    g_acpi_power_button_ready = g_pm1a_evt != 0 && g_sci_irq < 16;
    kernel_log_hex_u32("acpi: pm1a=", g_pm1a_cnt);
    kernel_log_hex_u32("acpi: slp_typa=", g_slp_typa);
    if (g_acpi_sleep_ready) {
        kernel_log_hex_u32("acpi: sleep_state=", g_sleep_state);
        kernel_log_hex_u32("acpi: sleep_typa=", g_sleep_slp_typa);
    }
    if (!s1_ready && !s3_ready) {
        log_write("acpi: s1 sleep fallback");
    }
    log_write(g_acpi_sleep_ready ? "acpi: sleep/s5 ready" : "acpi: s5 ready");
    acpi_refresh_public_info();
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
    acpi_refresh_public_info();
}

bool acpi_poweroff(void)
{
    if (g_acpi_ready) {
        kernel_log_hex_u32("acpi: poweroff pm1a=", g_pm1a_cnt);
        kernel_log_hex_u32("acpi: poweroff value=", (uint32_t) (g_slp_typa | ACPI_PM1_SLP_EN));
        return acpi_enter_sleep_state(g_slp_typa, g_slp_typb);
    }
    return false;
}

bool acpi_reboot(void)
{
    if (g_acpi_reset_ready) {
        log_write("acpi: reset register");
        if (acpi_gas_write(&g_reset_reg, g_reset_value)) {
            for (uint32_t i = 0; i < 100000; i++) {
                io_wait();
            }
        }
    }

    log_write("acpi: cf9 reset fallback");
    outb(ACPI_RESET_CF9_PORT, 0x02);
    io_wait();
    outb(ACPI_RESET_CF9_PORT, 0x06);
    for (uint32_t i = 0; i < 100000; i++) {
        io_wait();
    }

    log_write("acpi: keyboard reset fallback");
    outb(ACPI_RESET_KBD_PORT, ACPI_RESET_KBD_COMMAND);
    return true;
}

bool acpi_sleep(void)
{
    if (!g_acpi_ready || !g_acpi_sleep_ready) {
        return false;
    }
    kernel_log_hex_u32("acpi: sleep state=", g_sleep_state);
    kernel_log_hex_u32("acpi: sleep value=", (uint32_t) (g_sleep_slp_typa | ACPI_PM1_SLP_EN));
    return acpi_enter_sleep_state(g_sleep_slp_typa, g_sleep_slp_typb);
}

const acpi_info_t *acpi_info(void)
{
    acpi_refresh_public_info();
    return &g_acpi_info;
}

const char *acpi_status(void)
{
    acpi_refresh_public_info();
    return g_acpi_status;
}
