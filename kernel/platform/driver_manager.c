#include "common.h"
#include "ahci.h"
#include "bsod.h"
#include "driver_manager.h"
#include "exec.h"
#include "file.h"
#include "hash.h"
#include "hda.h"
#include "ide.h"
#include "kernel.h"
#include "memory.h"
#include "nvme.h"
#include "registry.h"
#include "ui.h"
#include "xhci.h"

#define DRIVER_MANAGER_MAX 24U
#define DRIVER_MANAGER_EXTERNAL_MAX 8U

#define PE_DOS_MAGIC 0x5A4D
#define PE_NT_SIGNATURE 0x00004550U
#define PE_MACHINE_AMD64 0x8664
#define PE_OPTIONAL_MAGIC_PE32_PLUS 0x20B
#define PE_FILE_EXECUTABLE_IMAGE 0x0002
#define PE_FILE_DLL 0x2000
#define PE_DIRECTORY_SECURITY 4U
#define PE_WIN_CERT_REVISION_1 0x0100
#define PE_WIN_CERT_REVISION_2 0x0200
#define PE_WIN_CERT_TYPE_PKCS_SIGNED_DATA 0x0002
#define MONIOS_SIGNER_MARKER_SIZE 16U
#define MONIOS_SIGNER_ID_SIZE SHA256_DIGEST_SIZE

static kernel_driver_t g_drivers[DRIVER_MANAGER_MAX];
static uint32_t g_driver_count;
static char g_external_driver_names[DRIVER_MANAGER_EXTERNAL_MAX][REGISTRY_VALUE_MAX];
static uint32_t g_external_driver_count;
static uint8_t g_kernel_signer_id[MONIOS_SIGNER_ID_SIZE];
static bool g_kernel_signature_ready;
static const uint8_t g_monios_signer_marker[MONIOS_SIGNER_MARKER_SIZE] = "MONIOS-SIGNER-V1";

extern bool smbus_driver_init(void);
extern bool net_driver_init(void);
extern bool graphics_driver_init(void);
extern void smbus_driver_shutdown(void);
extern void net_shutdown(void);
extern void graphics_shutdown(void);
extern void audio_shutdown(void);

static uint16_t driver_manager_read_u16(const uint8_t *data, uint32_t offset)
{
    return (uint16_t) data[offset] | (uint16_t) ((uint16_t) data[offset + 1] << 8);
}

static uint32_t driver_manager_read_u32(const uint8_t *data, uint32_t offset)
{
    return (uint32_t) data[offset] |
           ((uint32_t) data[offset + 1] << 8) |
           ((uint32_t) data[offset + 2] << 16) |
           ((uint32_t) data[offset + 3] << 24);
}

static bool driver_manager_range_ok(uint32_t size, uint32_t offset, uint32_t length)
{
    return offset <= size && length <= size - offset;
}

static bool driver_manager_path_has_suffix(const char *path, const char *suffix)
{
    uint32_t path_len;
    uint32_t suffix_len;

    if (path == NULL || suffix == NULL) {
        return false;
    }
    path_len = (uint32_t) strlen(path);
    suffix_len = (uint32_t) strlen(suffix);
    if (suffix_len > path_len) {
        return false;
    }
    return strcasecmp(path + path_len - suffix_len, suffix) == 0;
}

static uint32_t driver_manager_parse_count(const char *text)
{
    uint32_t value = 0;

    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    for (uint32_t i = 0; text[i] != '\0'; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return 0;
        }
        value = value * 10U + (uint32_t) (text[i] - '0');
        if (value > DRIVER_MANAGER_EXTERNAL_MAX) {
            return DRIVER_MANAGER_EXTERNAL_MAX;
        }
    }
    return value;
}

static void driver_manager_build_boot_key(uint32_t index, char key[REGISTRY_KEY_MAX])
{
    const char *prefix = "drivers.boot.";
    uint32_t pos = 0;
    uint32_t divisor = 1;

    strcpy(key, prefix);
    pos = (uint32_t) strlen(key);
    while (index / divisor >= 10U) {
        divisor *= 10U;
    }
    while (divisor > 0U && pos + 1U < REGISTRY_KEY_MAX) {
        key[pos++] = (char) ('0' + ((index / divisor) % 10U));
        divisor /= 10U;
    }
    key[pos] = '\0';
}

static bool driver_manager_verify_win_certificate(const uint8_t *data, uint32_t size, uint32_t cert_offset, uint32_t cert_size)
{
    uint32_t cert_len;
    uint16_t revision;
    uint16_t cert_type;

    if (cert_offset == 0 || cert_size < 8 ||
        !driver_manager_range_ok(size, cert_offset, cert_size)) {
        return false;
    }
    cert_len = driver_manager_read_u32(data, cert_offset);
    revision = driver_manager_read_u16(data, cert_offset + 4U);
    cert_type = driver_manager_read_u16(data, cert_offset + 6U);
    if (cert_len < 8U || cert_len > cert_size) {
        return false;
    }
    if (revision != PE_WIN_CERT_REVISION_1 && revision != PE_WIN_CERT_REVISION_2) {
        return false;
    }
    return cert_type == PE_WIN_CERT_TYPE_PKCS_SIGNED_DATA;
}

static bool driver_manager_mem_contains(const uint8_t *haystack, uint32_t haystack_size,
                                        const uint8_t *needle, uint32_t needle_size)
{
    if (haystack == NULL || needle == NULL || needle_size == 0 || needle_size > haystack_size) {
        return false;
    }
    for (uint32_t i = 0; i + needle_size <= haystack_size; i++) {
        if (memcmp(haystack + i, needle, needle_size) == 0) {
            return true;
        }
    }
    return false;
}

static bool driver_manager_authenticode_digest_present(const uint8_t *data,
                                                       uint32_t size,
                                                       uint32_t optional_offset,
                                                       uint32_t security_dir_offset,
                                                       uint32_t cert_offset,
                                                       uint32_t cert_size)
{
    uint32_t checksum_offset = optional_offset + 64U;
    uint32_t cert_end = cert_offset + cert_size;
    uint8_t digest[SHA256_DIGEST_SIZE];
    sha256_ctx_t ctx;

    if (!driver_manager_range_ok(size, checksum_offset, 4U) ||
        !driver_manager_range_ok(size, security_dir_offset, 8U) ||
        !driver_manager_range_ok(size, cert_offset, cert_size) ||
        cert_end < cert_offset ||
        security_dir_offset < checksum_offset + 4U ||
        cert_offset < security_dir_offset + 8U) {
        return false;
    }

    sha256_init(&ctx);
    sha256_update(&ctx, data, checksum_offset);
    sha256_update(&ctx,
                  data + checksum_offset + 4U,
                  security_dir_offset - (checksum_offset + 4U));
    sha256_update(&ctx,
                  data + security_dir_offset + 8U,
                  cert_offset - (security_dir_offset + 8U));
    if (cert_end < size) {
        sha256_update(&ctx, data + cert_end, size - cert_end);
    }
    sha256_final(&ctx, digest);

    return driver_manager_mem_contains(data + cert_offset, cert_size, digest, sizeof(digest));
}

static bool driver_manager_extract_signer_id(const uint8_t *data,
                                             uint32_t cert_offset,
                                             uint8_t signer_id[MONIOS_SIGNER_ID_SIZE])
{
    uint32_t marker_end;
    bool found = false;

    if (data == NULL || signer_id == NULL ||
        cert_offset < MONIOS_SIGNER_MARKER_SIZE + MONIOS_SIGNER_ID_SIZE) {
        return false;
    }
    marker_end = cert_offset - MONIOS_SIGNER_MARKER_SIZE - MONIOS_SIGNER_ID_SIZE;
    for (uint32_t offset = 0; offset <= marker_end; offset++) {
        if (memcmp(data + offset, g_monios_signer_marker, MONIOS_SIGNER_MARKER_SIZE) == 0) {
            memcpy(signer_id,
                   data + offset + MONIOS_SIGNER_MARKER_SIZE,
                   MONIOS_SIGNER_ID_SIZE);
            found = true;
        }
    }
    return found;
}

static bool driver_manager_verify_signed_sys_image(const uint8_t *data,
                                                    uint32_t size,
                                                    uint8_t signer_id[MONIOS_SIGNER_ID_SIZE])
{
    uint32_t pe_offset;
    uint32_t file_header_offset;
    uint32_t optional_offset;
    uint16_t machine;
    uint16_t size_of_optional_header;
    uint16_t characteristics;
    uint16_t magic;
    uint16_t subsystem;
    uint32_t number_of_rva_and_sizes;
    uint32_t security_dir_offset;
    uint32_t cert_offset;
    uint32_t cert_size;

    if (data == NULL || size < 0x100U || driver_manager_read_u16(data, 0) != PE_DOS_MAGIC) {
        return false;
    }
    pe_offset = driver_manager_read_u32(data, 0x3CU);
    if (!driver_manager_range_ok(size, pe_offset, 24U) ||
        driver_manager_read_u32(data, pe_offset) != PE_NT_SIGNATURE) {
        return false;
    }

    file_header_offset = pe_offset + 4U;
    machine = driver_manager_read_u16(data, file_header_offset);
    size_of_optional_header = driver_manager_read_u16(data, file_header_offset + 16U);
    characteristics = driver_manager_read_u16(data, file_header_offset + 18U);
    if (machine != PE_MACHINE_AMD64 ||
        (characteristics & PE_FILE_EXECUTABLE_IMAGE) == 0 ||
        (characteristics & PE_FILE_DLL) != 0) {
        return false;
    }

    optional_offset = file_header_offset + 20U;
    if (size_of_optional_header < 120U ||
        !driver_manager_range_ok(size, optional_offset, size_of_optional_header)) {
        return false;
    }
    magic = driver_manager_read_u16(data, optional_offset);
    subsystem = driver_manager_read_u16(data, optional_offset + 68U);
    number_of_rva_and_sizes = driver_manager_read_u32(data, optional_offset + 108U);
    if (magic != PE_OPTIONAL_MAGIC_PE32_PLUS ||
        subsystem != EXEC_SUBSYSTEM_NATIVE ||
        number_of_rva_and_sizes <= PE_DIRECTORY_SECURITY) {
        return false;
    }

    security_dir_offset = optional_offset + 112U + PE_DIRECTORY_SECURITY * 8U;
    if (security_dir_offset + 8U > optional_offset + size_of_optional_header) {
        return false;
    }
    cert_offset = driver_manager_read_u32(data, security_dir_offset);
    cert_size = driver_manager_read_u32(data, security_dir_offset + 4U);
    if (!driver_manager_verify_win_certificate(data, size, cert_offset, cert_size)) {
        return false;
    }
    if (!driver_manager_authenticode_digest_present(data,
                                                    size,
                                                    optional_offset,
                                                    security_dir_offset,
                                                    cert_offset,
                                                    cert_size)) {
        return false;
    }
    return driver_manager_extract_signer_id(data, cert_offset, signer_id);
}

static bool driver_manager_verify_signed_pe_file(const char *path,
                                                 uint8_t signer_id[MONIOS_SIGNER_ID_SIZE])
{
    int32_t file_len;
    uint8_t *data;
    bool verified;

    if (path == NULL || signer_id == NULL) {
        return false;
    }
    if (!file_exists(path) || file_is_dir(path)) {
        return false;
    }
    file_len = file_size(path);
    if (file_len <= 0) {
        return false;
    }
    data = (uint8_t *) kmalloc((uint32_t) file_len);
    if (data == NULL) {
        bsod_out_of_memory((uint64_t) file_len);
        return false;
    }
    if (file_read(path, data, (uint32_t) file_len) != file_len) {
        kfree(data);
        return false;
    }
    verified = driver_manager_verify_signed_sys_image(data, (uint32_t) file_len, signer_id);
    kfree(data);
    return verified;
}

static bool driver_manager_verify_signed_sys_file(const char *path,
                                                  uint8_t signer_id[MONIOS_SIGNER_ID_SIZE])
{
    if (path == NULL || !driver_manager_path_has_suffix(path, ".sys")) {
        return false;
    }
    if (!file_exists(path) || file_is_dir(path)) {
        bsod_panic("DRIVER LOAD FAILED", path);
        return false;
    }
    return driver_manager_verify_signed_pe_file(path, signer_id);
}

static bool driver_manager_load_kernel_signature(void)
{
    if (!file_exists(UI_KERNEL_IMAGE_PATH) || file_is_dir(UI_KERNEL_IMAGE_PATH) ||
        !driver_manager_verify_signed_pe_file(UI_KERNEL_IMAGE_PATH, g_kernel_signer_id)) {
        bsod_panic("KERNEL SIGNATURE INVALID", UI_KERNEL_IMAGE_PATH);
        return false;
    }
    g_kernel_signature_ready = true;
    log_write("driver-manager: kernel signature verified");
    return true;
}

static void driver_manager_append(const kernel_driver_t *driver)
{
    if (g_driver_count >= DRIVER_MANAGER_MAX) {
        bsod_panic("DRIVER TABLE FULL", "too many kernel drivers");
        return;
    }
    g_drivers[g_driver_count++] = *driver;
}

static void driver_manager_register_external_driver(const char *path)
{
    kernel_driver_t driver;
    uint8_t signer_id[MONIOS_SIGNER_ID_SIZE];

    if (g_external_driver_count >= DRIVER_MANAGER_EXTERNAL_MAX) {
        bsod_panic("DRIVER TABLE FULL", path);
        return;
    }
    if (!driver_manager_verify_signed_sys_file(path, signer_id)) {
        bsod_panic("DRIVER SIGNATURE INVALID", path);
        return;
    }
    if (!g_kernel_signature_ready ||
        memcmp(signer_id, g_kernel_signer_id, MONIOS_SIGNER_ID_SIZE) != 0) {
        bsod_panic("CORE SIGNATURE MISMATCH", path);
        return;
    }

    strlcpy(g_external_driver_names[g_external_driver_count], path, REGISTRY_VALUE_MAX);
    driver.name = g_external_driver_names[g_external_driver_count];
    driver.init = NULL;
    driver.shutdown = NULL;
    driver.loaded = true;
    g_external_driver_count++;
    driver_manager_append(&driver);
    log_write("driver-manager: signed external driver registered");
    log_write(path);
}

static void driver_manager_load_registry_drivers(void)
{
    char count_text[REGISTRY_VALUE_MAX];
    uint32_t count;

    if (!registry_get_copy("drivers.boot.count", count_text, sizeof(count_text))) {
        return;
    }
    count = driver_manager_parse_count(count_text);
    if (count == 0) {
        bsod_panic("CORE DRIVER MISSING", UI_RZDRV_PATH);
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        char key[REGISTRY_KEY_MAX];
        char path[REGISTRY_VALUE_MAX];

        driver_manager_build_boot_key(i, key);
        if (!registry_get_copy(key, path, sizeof(path))) {
            bsod_panic("DRIVER REGISTRY INVALID", key);
            return;
        }
        driver_manager_register_external_driver(path);
    }
}

void driver_manager_init(void)
{
    kernel_driver_t driver;

    memset(g_drivers, 0, sizeof(g_drivers));
    memset(g_external_driver_names, 0, sizeof(g_external_driver_names));
    g_driver_count = 0;
    g_external_driver_count = 0;
    memset(g_kernel_signer_id, 0, sizeof(g_kernel_signer_id));
    g_kernel_signature_ready = false;

    if (!driver_manager_load_kernel_signature()) {
        return;
    }

    driver = (kernel_driver_t) { "graphics", graphics_driver_init, graphics_shutdown, false };
    driver_manager_append(&driver);
    driver = (kernel_driver_t) { "ide", ide_driver_init, ide_shutdown, false };
    driver_manager_append(&driver);
    driver = (kernel_driver_t) { "ahci", ahci_driver_init, ahci_shutdown, false };
    driver_manager_append(&driver);
    driver = (kernel_driver_t) { "nvme", nvme_driver_init, nvme_shutdown, false };
    driver_manager_append(&driver);
    driver = (kernel_driver_t) { "xhci", xhci_driver_init, xhci_shutdown, false };
    driver_manager_append(&driver);
    driver = (kernel_driver_t) { "hda", hda_driver_init, hda_shutdown, false };
    driver_manager_append(&driver);
    driver = (kernel_driver_t) { "smbus", smbus_driver_init, smbus_driver_shutdown, false };
    driver_manager_append(&driver);
    driver = (kernel_driver_t) { "onboard-net", net_driver_init, net_shutdown, false };
    driver_manager_append(&driver);
    driver = (kernel_driver_t) { "onboard-audio", NULL, audio_shutdown, true };
    driver_manager_append(&driver);

    for (uint32_t i = 0; i < g_driver_count; i++) {
        if (g_drivers[i].init != NULL) {
            g_drivers[i].loaded = g_drivers[i].init();
        }
    }
    driver_manager_load_registry_drivers();
}

void driver_manager_shutdown(void)
{
    for (int32_t i = (int32_t) g_driver_count - 1; i >= 0; i--) {
        if (g_drivers[i].shutdown != NULL && g_drivers[i].loaded) {
            g_drivers[i].shutdown();
            g_drivers[i].loaded = false;
        }
    }
}

uint32_t driver_manager_count(void)
{
    return g_driver_count;
}

const kernel_driver_t *driver_manager_at(uint32_t index)
{
    if (index >= g_driver_count) {
        return NULL;
    }
    return &g_drivers[index];
}
