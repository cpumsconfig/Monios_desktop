#include "common.h"
#include "ahci.h"
#include "authenticode.h"
#include "bsod.h"
#include "driver_manager.h"
#include "exec.h"
#include "file.h"
#include "hash.h"
#include "hda.h"
#include "ide.h"
#include "kernel.h"
#include "memory.h"
#include "net.h"
#include "nvme.h"
#include "registry.h"
#include "graphics.h"
#include "gpu.h"
#include "ui.h"
#include "virtio.h"
#include "xhci.h"

#define DRIVER_MANAGER_MAX 24U
#define DRIVER_MANAGER_EXTERNAL_MAX 8U

#define PE_DOS_MAGIC 0x5A4D
#define PE_NT_SIGNATURE 0x00004550U
#define PE_MACHINE_AMD64 0x8664
#define PE_OPTIONAL_MAGIC_PE32_PLUS 0x20B
#define PE_FILE_EXECUTABLE_IMAGE 0x0002
#define PE_FILE_DLL 0x2000
#define PE_SECTION_MEM_EXECUTE 0x20000000U
#define PE_DIRECTORY_EXPORT 0U
#define PE_DIRECTORY_IMPORT 1U
#define PE_DIRECTORY_BASERELOC 5U
#define PE_IMAGE_REL_BASED_ABSOLUTE 0U
#define PE_IMAGE_REL_BASED_DIR64 10U
#define MONIOS_SIGNER_ID_SIZE AUTHENTICODE_SIGNER_ID_SIZE
#define DRIVER_IMAGE_MAX_SIZE (4U * 1024U * 1024U)
#define DRIVER_PACKAGE_MAX_SIZE (8U * 1024U * 1024U)
#define DRIVER_PE_DIRECTORY_COUNT 16U
#define DRIVER_MANAGER_TICK_INTERVAL 30U
#define DRIVER_MANAGER_RESCAN_INTERVAL 300U

static kernel_driver_t g_drivers[DRIVER_MANAGER_MAX];
static uint32_t g_driver_count;
static char g_external_driver_names[DRIVER_MANAGER_EXTERNAL_MAX][REGISTRY_VALUE_MAX];
static uint32_t g_external_driver_count;
static uint8_t g_kernel_signer_id[MONIOS_SIGNER_ID_SIZE];
static bool g_kernel_signature_ready;
static bool g_driver_manager_update_running;
static kernel_driver_t *g_driver_manager_callback_driver;
static uint64_t g_driver_manager_last_rescan_tick;

typedef struct {
    uint32_t virtual_address;
    uint32_t size;
} __attribute__((packed)) driver_pe_data_directory_t;

typedef struct {
    uint16_t magic;
    uint8_t major_linker_version;
    uint8_t minor_linker_version;
    uint32_t size_of_code;
    uint32_t size_of_initialized_data;
    uint32_t size_of_uninitialized_data;
    uint32_t address_of_entry_point;
    uint32_t base_of_code;
    uint64_t image_base;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint16_t major_operating_system_version;
    uint16_t minor_operating_system_version;
    uint16_t major_image_version;
    uint16_t minor_image_version;
    uint16_t major_subsystem_version;
    uint16_t minor_subsystem_version;
    uint32_t win32_version_value;
    uint32_t size_of_image;
    uint32_t size_of_headers;
    uint32_t checksum;
    uint16_t subsystem;
    uint16_t dll_characteristics;
    uint64_t size_of_stack_reserve;
    uint64_t size_of_stack_commit;
    uint64_t size_of_heap_reserve;
    uint64_t size_of_heap_commit;
    uint32_t loader_flags;
    uint32_t number_of_rva_and_sizes;
    driver_pe_data_directory_t data_directories[DRIVER_PE_DIRECTORY_COUNT];
} __attribute__((packed)) driver_pe_optional_header64_t;

typedef struct {
    uint32_t characteristics;
    uint32_t time_date_stamp;
    uint16_t major_version;
    uint16_t minor_version;
    uint32_t name;
    uint32_t base;
    uint32_t number_of_functions;
    uint32_t number_of_names;
    uint32_t address_of_functions;
    uint32_t address_of_names;
    uint32_t address_of_name_ordinals;
} __attribute__((packed)) driver_pe_export_directory_t;

typedef struct {
    uint8_t *image;
    uint32_t image_size;
    monios_driver_entry_fn_t entry;
    monios_driver_unload_fn_t unload;
    monios_driver_tick_fn_t tick;
} driver_loaded_image_t;

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

static uint64_t driver_manager_read_u64(const uint8_t *data, uint32_t offset)
{
    return (uint64_t) driver_manager_read_u32(data, offset) |
           ((uint64_t) driver_manager_read_u32(data, offset + 4U) << 32);
}

static void driver_manager_write_u64(uint8_t *data, uint32_t offset, uint64_t value)
{
    for (uint32_t i = 0; i < 8; i++) {
        data[offset + i] = (uint8_t) (value >> (i * 8U));
    }
}

static bool driver_manager_range_ok(uint32_t size, uint32_t offset, uint32_t length)
{
    return offset <= size && length <= size - offset;
}

static bool driver_manager_image_rva_ok(uint32_t image_size, uint32_t rva, uint32_t length)
{
    return rva <= image_size && length <= image_size - rva;
}

static bool driver_manager_image_string(const uint8_t *image,
                                        uint32_t image_size,
                                        uint32_t rva,
                                        const char **text_out)
{
    uint32_t length = 0;

    if (image == NULL || text_out == NULL || rva >= image_size) {
        return false;
    }
    while (rva + length < image_size && image[rva + length] != '\0') {
        length++;
    }
    if (rva + length >= image_size) {
        return false;
    }
    *text_out = (const char *) (image + rva);
    return true;
}

static uint32_t driver_manager_find_export_rva(const uint8_t *image,
                                               uint32_t image_size,
                                               uint32_t export_rva,
                                               uint32_t export_size,
                                               const char *wanted_name)
{
    const driver_pe_export_directory_t *exports;
    uint32_t function_bytes;
    uint32_t name_bytes;
    uint32_t ordinal_bytes;

    if (image == NULL || wanted_name == NULL ||
        export_size < sizeof(driver_pe_export_directory_t) ||
        !driver_manager_image_rva_ok(image_size, export_rva, export_size) ||
        !driver_manager_image_rva_ok(image_size, export_rva, sizeof(*exports))) {
        return 0;
    }
    exports = (const driver_pe_export_directory_t *) (image + export_rva);
    if (exports->number_of_functions == 0 ||
        exports->number_of_names == 0 ||
        exports->address_of_functions == 0 ||
        exports->address_of_names == 0 ||
        exports->address_of_name_ordinals == 0 ||
        exports->number_of_functions > 0xFFFFFFFFU / sizeof(uint32_t) ||
        exports->number_of_names > 0xFFFFFFFFU / sizeof(uint32_t) ||
        exports->number_of_names > 0xFFFFFFFFU / sizeof(uint16_t)) {
        return 0;
    }
    function_bytes = exports->number_of_functions * sizeof(uint32_t);
    name_bytes = exports->number_of_names * sizeof(uint32_t);
    ordinal_bytes = exports->number_of_names * sizeof(uint16_t);
    if (!driver_manager_image_rva_ok(image_size,
                                      exports->address_of_functions,
                                      function_bytes) ||
        !driver_manager_image_rva_ok(image_size,
                                      exports->address_of_names,
                                      name_bytes) ||
        !driver_manager_image_rva_ok(image_size,
                                      exports->address_of_name_ordinals,
                                      ordinal_bytes)) {
        return 0;
    }

    for (uint32_t i = 0; i < exports->number_of_names; i++) {
        uint32_t name_rva = driver_manager_read_u32(image,
                                                    exports->address_of_names + i * sizeof(uint32_t));
        const char *name;
        uint16_t ordinal;
        uint32_t function_rva;

        if (!driver_manager_image_string(image, image_size, name_rva, &name) ||
            strcmp(name, wanted_name) != 0) {
            continue;
        }
        ordinal = (uint16_t) (image[exports->address_of_name_ordinals + i * sizeof(uint16_t)] |
                              ((uint16_t) image[exports->address_of_name_ordinals + i * sizeof(uint16_t) + 1U] << 8));
        if (ordinal >= exports->number_of_functions) {
            return 0;
        }
        function_rva = driver_manager_read_u32(image,
                                               exports->address_of_functions + ordinal * sizeof(uint32_t));
        if (function_rva == 0 ||
            (uint64_t) function_rva >= image_size ||
            (function_rva >= export_rva && function_rva < export_rva + export_size)) {
            return 0;
        }
        return function_rva;
    }
    return 0;
}

static bool driver_manager_apply_relocations(uint8_t *image,
                                             uint32_t image_size,
                                             uint64_t preferred_base,
                                             const driver_pe_data_directory_t *directory)
{
    uint64_t delta;
    uint32_t offset = 0;

    if (image == NULL || directory == NULL ||
        directory->virtual_address == 0 ||
        directory->size < 8U ||
        !driver_manager_image_rva_ok(image_size,
                                      directory->virtual_address,
                                      directory->size)) {
        return false;
    }
    delta = (uint64_t) (uintptr_t) image - preferred_base;
    while (offset < directory->size) {
        uint32_t block_rva;
        uint32_t block_size;
        uint32_t entry_count;
        uint32_t block_end;

        if (directory->size - offset < 8U) {
            return false;
        }
        block_rva = driver_manager_read_u32(image,
                                            directory->virtual_address + offset);
        block_size = driver_manager_read_u32(image,
                                             directory->virtual_address + offset + 4U);
        if (block_size < 8U || block_size > directory->size - offset ||
            (block_size - 8U) % sizeof(uint16_t) != 0) {
            return false;
        }
        entry_count = (block_size - 8U) / sizeof(uint16_t);
        block_end = offset + block_size;
        for (uint32_t i = 0; i < entry_count; i++) {
            uint32_t entry_offset = directory->virtual_address + offset + 8U + i * sizeof(uint16_t);
            uint16_t entry = (uint16_t) (image[entry_offset] |
                                         ((uint16_t) image[entry_offset + 1U] << 8));
            uint16_t type = entry >> 12;
            uint32_t target_offset = entry & 0x0FFFU;
            uint32_t target_rva;

            if (type == PE_IMAGE_REL_BASED_ABSOLUTE) {
                continue;
            }
            if (block_rva > image_size ||
                target_offset > image_size - block_rva) {
                return false;
            }
            target_rva = block_rva + target_offset;
            if (type != PE_IMAGE_REL_BASED_DIR64 ||
                !driver_manager_image_rva_ok(image_size, target_rva, sizeof(uint64_t))) {
                return false;
            }
            driver_manager_write_u64(image,
                                     target_rva,
                                     driver_manager_read_u64(image, target_rva) + delta);
        }
        offset = block_end;
    }
    return offset == directory->size;
}

static bool driver_manager_load_native_image(const uint8_t *data,
                                             uint32_t size,
                                             driver_loaded_image_t *loaded)
{
    uint32_t pe_offset;
    uint32_t file_header_offset;
    uint32_t optional_offset;
    uint32_t section_offset;
    uint16_t section_count;
    uint16_t optional_size;
    uint16_t characteristics;
    const driver_pe_optional_header64_t *optional;
    uint8_t *image;
    bool entry_executable = false;
    uint32_t entry_rva;
    uint32_t unload_rva;
    uint32_t tick_rva;

    if (data == NULL || loaded == NULL ||
        size < 0x100U ||
        driver_manager_read_u16(data, 0) != PE_DOS_MAGIC) {
        return false;
    }
    pe_offset = driver_manager_read_u32(data, 0x3CU);
    if (!driver_manager_range_ok(size, pe_offset, 24U) ||
        driver_manager_read_u32(data, pe_offset) != PE_NT_SIGNATURE) {
        return false;
    }
    file_header_offset = pe_offset + 4U;
    section_count = driver_manager_read_u16(data, file_header_offset + 2U);
    optional_size = driver_manager_read_u16(data, file_header_offset + 16U);
    characteristics = driver_manager_read_u16(data, file_header_offset + 18U);
    if (section_count == 0 ||
        (characteristics & PE_FILE_EXECUTABLE_IMAGE) == 0 ||
        (characteristics & PE_FILE_DLL) != 0) {
        return false;
    }
    optional_offset = file_header_offset + 20U;
    if (optional_size < sizeof(driver_pe_optional_header64_t) ||
        !driver_manager_range_ok(size, optional_offset, optional_size)) {
        return false;
    }
    optional = (const driver_pe_optional_header64_t *) (data + optional_offset);
    if (optional->magic != PE_OPTIONAL_MAGIC_PE32_PLUS ||
        optional->subsystem != EXEC_SUBSYSTEM_NATIVE ||
        optional->number_of_rva_and_sizes < DRIVER_PE_DIRECTORY_COUNT ||
        optional->size_of_image == 0 ||
        optional->size_of_image > DRIVER_IMAGE_MAX_SIZE ||
        optional->size_of_headers == 0 ||
        optional->size_of_headers > optional->size_of_image ||
        optional->address_of_entry_point >= optional->size_of_image ||
        optional->section_alignment == 0) {
        return false;
    }
    section_offset = optional_offset + optional_size;
    if (!driver_manager_range_ok(size,
                                 section_offset,
                                 (uint32_t) section_count * 40U) ||
        optional->size_of_headers > size) {
        return false;
    }
    if (optional->data_directories[PE_DIRECTORY_IMPORT].virtual_address != 0 ||
        optional->data_directories[PE_DIRECTORY_IMPORT].size != 0 ||
        optional->data_directories[PE_DIRECTORY_EXPORT].virtual_address == 0 ||
        optional->data_directories[PE_DIRECTORY_EXPORT].size == 0) {
        return false;
    }

    image = (uint8_t *) kmalloc(optional->size_of_image);
    if (image == NULL) {
        return false;
    }
    memset(image, 0, optional->size_of_image);
    memcpy(image, data, optional->size_of_headers);

    for (uint16_t i = 0; i < section_count; i++) {
        const uint8_t *section = data + section_offset + (uint32_t) i * 40U;
        uint32_t virtual_size = driver_manager_read_u32(section, 8U);
        uint32_t virtual_address = driver_manager_read_u32(section, 12U);
        uint32_t raw_size = driver_manager_read_u32(section, 16U);
        uint32_t raw_offset = driver_manager_read_u32(section, 20U);
        uint32_t section_flags = driver_manager_read_u32(section, 36U);
        uint32_t section_size = virtual_size > raw_size ? virtual_size : raw_size;

        if (section_size == 0) {
            continue;
        }
        if (!driver_manager_image_rva_ok(optional->size_of_image,
                                          virtual_address,
                                          section_size) ||
            (raw_size != 0 &&
             !driver_manager_range_ok(size, raw_offset, raw_size))) {
            kfree(image);
            return false;
        }
        if (raw_size != 0) {
            memcpy(image + virtual_address, data + raw_offset, raw_size);
        }
        if (optional->address_of_entry_point >= virtual_address &&
            optional->address_of_entry_point - virtual_address < section_size &&
            (section_flags & PE_SECTION_MEM_EXECUTE) != 0) {
            entry_executable = true;
        }
    }
    if (!entry_executable ||
        (optional->data_directories[PE_DIRECTORY_BASERELOC].virtual_address != 0 &&
         optional->data_directories[PE_DIRECTORY_BASERELOC].size != 0 &&
         !driver_manager_apply_relocations(image,
                                           optional->size_of_image,
                                           optional->image_base,
                                           &optional->data_directories[PE_DIRECTORY_BASERELOC]))) {
        kfree(image);
        return false;
    }

    entry_rva = driver_manager_find_export_rva(image,
                                                optional->size_of_image,
                                                optional->data_directories[PE_DIRECTORY_EXPORT].virtual_address,
                                                optional->data_directories[PE_DIRECTORY_EXPORT].size,
                                                "DriverEntry");
    unload_rva = driver_manager_find_export_rva(image,
                                                optional->size_of_image,
                                                optional->data_directories[PE_DIRECTORY_EXPORT].virtual_address,
                                                optional->data_directories[PE_DIRECTORY_EXPORT].size,
                                                "DriverUnload");
    tick_rva = driver_manager_find_export_rva(image,
                                              optional->size_of_image,
                                              optional->data_directories[PE_DIRECTORY_EXPORT].virtual_address,
                                              optional->data_directories[PE_DIRECTORY_EXPORT].size,
                                              "DriverTick");
    if (entry_rva == 0 ||
        entry_rva != optional->address_of_entry_point ||
        unload_rva == 0) {
        kfree(image);
        return false;
    }
    loaded->image = image;
    loaded->image_size = optional->size_of_image;
    loaded->entry = (monios_driver_entry_fn_t) (uintptr_t) (image + entry_rva);
    loaded->unload = (monios_driver_unload_fn_t) (uintptr_t) (image + unload_rva);
    loaded->tick = tick_rva == 0 ?
                   NULL :
                   (monios_driver_tick_fn_t) (uintptr_t) (image + tick_rva);
    return true;
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

static bool driver_manager_path_under_driver_dir(const char *path)
{
    static const char prefix[] = UI_DRIVERS_DIR "\\";
    const char *component;
    uint32_t prefix_len;
    uint32_t i;

    if (path == NULL) {
        return false;
    }
    prefix_len = (uint32_t) strlen(prefix);
    if (strlen(path) <= prefix_len) {
        return false;
    }
    for (i = 0; i < prefix_len; i++) {
        char left = path[i];
        char right = prefix[i];

        if (left >= 'A' && left <= 'Z') {
            left = (char) (left - 'A' + 'a');
        }
        if (right >= 'A' && right <= 'Z') {
            right = (char) (right - 'A' + 'a');
        }
        if (left != right) {
            return false;
        }
    }
    if (path[prefix_len - 1U] != '\\') {
        return false;
    }
    component = path + prefix_len;
    while (*component != '\0') {
        const char *end = strchr(component, '\\');
        uint32_t length = end == NULL ?
                          (uint32_t) strlen(component) :
                          (uint32_t) (end - component);

        if (length == 0 ||
            (length == 1 && component[0] == '.') ||
            (length == 2 && component[0] == '.' && component[1] == '.')) {
            return false;
        }
        if (end == NULL) {
            break;
        }
        component = end + 1;
    }
    return true;
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
    uint16_t subsystem;

    if (!authenticode_verify_pe(data, size, signer_id)) {
        return false;
    }
    if (data == NULL || size < 0x100U ||
        driver_manager_read_u16(data, 0) != PE_DOS_MAGIC) {
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
    optional_offset = file_header_offset + 20U;
    if (machine != PE_MACHINE_AMD64 ||
        (characteristics & PE_FILE_EXECUTABLE_IMAGE) == 0 ||
        (characteristics & PE_FILE_DLL) != 0 ||
        size_of_optional_header < 120U ||
        !driver_manager_range_ok(size, optional_offset, size_of_optional_header) ||
        driver_manager_read_u16(data, optional_offset) !=
            PE_OPTIONAL_MAGIC_PE32_PLUS) {
        return false;
    }
    subsystem = driver_manager_read_u16(data, optional_offset + 68U);
    return subsystem == EXEC_SUBSYSTEM_NATIVE;
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
    if (file_len <= 0 || (uint32_t) file_len > DRIVER_PACKAGE_MAX_SIZE) {
        return false;
    }
    data = (uint8_t *) kmalloc((uint32_t) file_len);
    if (data == NULL) {
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
    if (path == NULL ||
        !driver_manager_path_under_driver_dir(path) ||
        !driver_manager_path_has_suffix(path, ".sys")) {
        return false;
    }
    if (!file_exists(path) || file_is_dir(path)) {
        return false;
    }
    return driver_manager_verify_signed_pe_file(path, signer_id);
}

static bool driver_manager_load_kernel_signature(void)
{
    static const char * const kernel_candidates[] = {
        UI_KERNEL_IMAGE_PATH,
        "C:\\kernel.exe",
        "C:\\KERNEL.EXE",
    };
    const char *failed_path = UI_KERNEL_IMAGE_PATH;

    for (uint32_t i = 0; i < sizeof(kernel_candidates) / sizeof(kernel_candidates[0]); i++) {
        const char *path = kernel_candidates[i];

        if (!file_exists(path) || file_is_dir(path)) {
            continue;
        }
        failed_path = path;
        log_write("driver-manager: verifying Authenticode signature");
        if (driver_manager_verify_signed_pe_file(path, g_kernel_signer_id)) {
            g_kernel_signature_ready = true;
            log_write("driver-manager: kernel signature verified");
            return true;
        }
    }

    if (!g_kernel_signature_ready) {
        bsod_panic("KERNEL SIGNATURE INVALID", failed_path);
        return false;
    }
    return false;
}

static void driver_manager_append(const kernel_driver_t *driver)
{
    if (g_driver_count >= DRIVER_MANAGER_MAX) {
        bsod_panic("DRIVER TABLE FULL", "too many kernel drivers");
        return;
    }
    g_drivers[g_driver_count++] = *driver;
}

static int32_t driver_manager_find_index(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return -1;
    }
    for (uint32_t i = 0; i < g_driver_count; i++) {
        if (g_drivers[i].name != NULL &&
            strcasecmp(g_drivers[i].name, name) == 0) {
            return (int32_t) i;
        }
    }
    return -1;
}

static void driver_manager_module_log(const char *text)
{
    if (text != NULL && text[0] != '\0') {
        log_write(text);
    }
}

static void *driver_manager_module_alloc(uint64_t size)
{
    return kmalloc(size);
}

static void driver_manager_module_free(void *ptr)
{
    kfree(ptr);
}

static bool driver_manager_call_entry(const char *name,
                                      const char *path,
                                      monios_driver_entry_fn_t entry,
                                      bool critical)
{
    monios_driver_runtime_t runtime;

    if (entry == NULL) {
        return false;
    }
    memset(&runtime, 0, sizeof(runtime));
    runtime.abi_version = MONIOS_DRIVER_ABI_VERSION;
    runtime.flags = critical ? MONIOS_DRIVER_FLAG_CRITICAL : 0;
    runtime.name = name;
    runtime.path = path;
    runtime.log = driver_manager_module_log;
    runtime.alloc = driver_manager_module_alloc;
    runtime.free = driver_manager_module_free;
    return entry(&runtime);
}

static void driver_manager_release(kernel_driver_t *driver)
{
    if (driver == NULL || !driver->loaded) {
        return;
    }
    if (driver->external) {
        if (driver->unload != NULL) {
            kernel_driver_t *previous = g_driver_manager_callback_driver;
            g_driver_manager_callback_driver = driver;
            driver->unload();
            g_driver_manager_callback_driver = previous;
        }
    } else if (driver->shutdown != NULL) {
        driver->shutdown();
    }
    if (driver->image != NULL) {
        kfree(driver->image);
        driver->image = NULL;
    }
    driver->image_size = 0;
    driver->next_update_tick = 0;
    driver->last_update_tick = 0;
    driver->scheduled = false;
    driver->adapter_ready = false;
    driver->adapter_score = 0;
    driver->loaded = false;
}

static bool driver_manager_load_external_image(const char *path,
                                               driver_loaded_image_t *loaded)
{
    int32_t file_len;
    uint8_t *data;
    uint8_t signer_id[MONIOS_SIGNER_ID_SIZE];
    bool ok;

    if (path == NULL || loaded == NULL ||
        !driver_manager_path_under_driver_dir(path) ||
        !driver_manager_path_has_suffix(path, ".sys") ||
        !g_kernel_signature_ready) {
        return false;
    }
    file_len = file_size(path);
    if (file_len <= 0 || (uint32_t) file_len > DRIVER_PACKAGE_MAX_SIZE || file_is_dir(path)) {
        return false;
    }
    data = (uint8_t *) kmalloc((uint32_t) file_len);
    if (data == NULL) {
        return false;
    }
    if (file_read(path, data, (uint32_t) file_len) != file_len ||
        !driver_manager_verify_signed_sys_image(data, (uint32_t) file_len, signer_id) ||
        memcmp(signer_id, g_kernel_signer_id, MONIOS_SIGNER_ID_SIZE) != 0) {
        kfree(data);
        return false;
    }
    memset(loaded, 0, sizeof(*loaded));
    ok = driver_manager_load_native_image(data, (uint32_t) file_len, loaded);
    kfree(data);
    return ok;
}

bool driver_manager_load(const char *path)
{
    driver_loaded_image_t loaded;
    kernel_driver_t driver;
    uint8_t signer_id[MONIOS_SIGNER_ID_SIZE];
    int32_t existing_index;
    uint32_t slot;

    if (path == NULL ||
        !driver_manager_path_under_driver_dir(path) ||
        !driver_manager_path_has_suffix(path, ".sys")) {
        return false;
    }
    existing_index = driver_manager_find_index(path);
    if (existing_index >= 0) {
        kernel_driver_t *existing = &g_drivers[existing_index];

        if (!existing->external) {
            return false;
        }
        if (existing->loaded) {
            return true;
        }
        memset(&loaded, 0, sizeof(loaded));
        if (!driver_manager_load_external_image(path, &loaded) ||
            !driver_manager_call_entry(existing->name,
                                        existing->name,
                                        loaded.entry,
                                        existing->critical)) {
            if (loaded.image != NULL) {
                kfree(loaded.image);
            }
            return false;
        }
        existing->image = loaded.image;
        existing->image_size = loaded.image_size;
        existing->unload = loaded.unload;
        existing->tick = loaded.tick;
        existing->update_interval_ticks = loaded.tick == NULL ?
                                          0 :
                                          DRIVER_MANAGER_TICK_INTERVAL;
        existing->next_update_tick = timer_ticks() + existing->update_interval_ticks;
        existing->scheduled = loaded.tick != NULL;
        existing->loaded = true;
        existing->verified = true;
        log_write("driver-manager: external driver loaded");
        log_write(existing->name);
        return true;
    }
    if (!g_kernel_signature_ready ||
        g_external_driver_count >= DRIVER_MANAGER_EXTERNAL_MAX ||
        g_driver_count >= DRIVER_MANAGER_MAX ||
        !driver_manager_verify_signed_sys_file(path, signer_id) ||
        memcmp(signer_id, g_kernel_signer_id, MONIOS_SIGNER_ID_SIZE) != 0) {
        return false;
    }
    if (!driver_manager_load_external_image(path, &loaded)) {
        log_write("driver-manager: native driver ABI rejected");
        log_write(path);
        return false;
    }
    slot = g_external_driver_count;
    strlcpy(g_external_driver_names[slot], path, REGISTRY_VALUE_MAX);
    if (!driver_manager_call_entry(g_external_driver_names[slot],
                                    g_external_driver_names[slot],
                                    loaded.entry,
                                    false)) {
        kfree(loaded.image);
        log_write("driver-manager: driver entry failed");
        log_write(path);
        return false;
    }
    memset(&driver, 0, sizeof(driver));
    driver.name = g_external_driver_names[slot];
    driver.loaded = true;
    driver.external = true;
    driver.verified = true;
    driver.unloadable = true;
    driver.image = loaded.image;
    driver.image_size = loaded.image_size;
    driver.unload = loaded.unload;
    driver.tick = loaded.tick;
    driver.update_interval_ticks = loaded.tick == NULL ?
                                   0 :
                                   DRIVER_MANAGER_TICK_INTERVAL;
    driver.next_update_tick = timer_ticks() + driver.update_interval_ticks;
    driver.scheduled = loaded.tick != NULL;
    driver.priority = 500;
    driver.adapter_score = -1;
    g_external_driver_count++;
    driver_manager_append(&driver);
    log_write("driver-manager: external driver loaded");
    log_write(path);
    return true;
}

static void driver_manager_register_external_driver(const char *path)
{
    uint8_t signer_id[MONIOS_SIGNER_ID_SIZE];

    if (!driver_manager_verify_signed_sys_file(path, signer_id)) {
        bsod_panic("DRIVER SIGNATURE INVALID", path);
        return;
    }
    if (!g_kernel_signature_ready ||
        memcmp(signer_id, g_kernel_signer_id, MONIOS_SIGNER_ID_SIZE) != 0) {
        bsod_panic("CORE SIGNATURE MISMATCH", path);
        return;
    }
    if (!driver_manager_load(path)) {
        log_write("driver-manager: boot driver not loaded");
        log_write(path);
    }
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
        if (strcmp(count_text, "0") != 0) {
            bsod_panic("DRIVER REGISTRY INVALID", "drivers.boot.count");
        }
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

static void driver_manager_log_result(const kernel_driver_t *driver)
{
    char line[96];
    uint32_t pos = 0;
    const char *name;
    const char *suffix;

    if (driver == NULL || driver->name == NULL) {
        return;
    }
    strcpy(line, "driver-manager: ");
    pos = (uint32_t) strlen(line);
    name = driver->name;
    while (*name != '\0' && pos + 1U < sizeof(line)) {
        line[pos] = *name++;
        pos++;
    }
    suffix = driver->loaded ? " ready" : " unavailable";
    while (*suffix != '\0' && pos + 1U < sizeof(line)) {
        line[pos++] = *suffix++;
    }
    line[pos] = '\0';
    log_write(line);
}

static int32_t driver_manager_probe_loaded(void)
{
    return 100;
}

static int32_t driver_manager_probe_graphics(void)
{
    return graphics_active() ? 100 : 50;
}

static int32_t driver_manager_probe_nvidia(void)
{
    const gpu_info_t *info = gpu_info();

    if (info == NULL || !info->nvidia_detected) {
        return 0;
    }
    if (info->nvidia_ready) {
        return 100;
    }
    if (info->nvidia_mmio_ready && info->nvidia_memory_enabled) {
        return 75;
    }
    return 25;
}

static bool driver_manager_init_nvidia(void)
{
    const gpu_info_t *info = gpu_info();

    return info != NULL && info->nvidia_detected;
}

static int32_t driver_manager_probe_igpu(void)
{
    const gpu_info_t *info = gpu_info();

    if (info == NULL || !info->igpu_detected) {
        return 0;
    }
    if (info->igpu_ready) {
        return 100;
    }
    if (info->igpu_mmio_ready && info->igpu_memory_enabled) {
        return 75;
    }
    return 25;
}

static bool driver_manager_init_igpu(void)
{
    const gpu_info_t *info = gpu_info();

    return info != NULL && info->igpu_detected;
}

static int32_t driver_manager_probe_virtio(void)
{
    const virtio_info_t *info = virtio_info();

    if (info == NULL || !info->present) {
        return 0;
    }
    if ((info->io_ready || info->mmio_ready) && info->bus_master_enabled) {
        return 100;
    }
    if (info->modern && info->vendor_capability && info->bus_master_enabled) {
        return 75;
    }
    return 25;
}

static int32_t driver_manager_probe_virtio_blk(void)
{
    const virtio_blk_info_t *info = virtio_blk_info();

    if (info == NULL || !info->present) {
        return 0;
    }
    return info->ready ? 100 : 50;
}

static int32_t driver_manager_probe_ahci(void)
{
    const ahci_info_t *info = ahci_info();

    if (info == NULL || !info->present) {
        return 0;
    }
    if (info->ready) {
        return 100;
    }
    if (info->mmio_ready) {
        return 60;
    }
    return 25;
}

static int32_t driver_manager_probe_hda(void)
{
    const hda_info_t *info = hda_info();

    if (info == NULL || !info->present) {
        return 0;
    }
    return info->mmio_ready ? 100 : 50;
}

static int32_t driver_manager_probe_net(void)
{
    const net_info_t *info = net_info();

    if (info == NULL || !info->present) {
        return 0;
    }
    return info->onboard ? 100 : 60;
}

static bool driver_manager_dependencies_ready(const kernel_driver_t *driver)
{
    int32_t dependency_index;

    if (driver == NULL || driver->depends_on == NULL || driver->depends_on[0] == '\0') {
        return true;
    }
    dependency_index = driver_manager_find_index(driver->depends_on);
    return dependency_index >= 0 && g_drivers[dependency_index].loaded;
}

static void driver_manager_register_builtin_ex(const char *name,
                                               driver_init_fn_t init,
                                               driver_shutdown_fn_t shutdown,
                                               bool already_loaded,
                                               bool critical,
                                               const char *depends_on,
                                               uint32_t priority,
                                               driver_probe_fn_t probe)
{
    kernel_driver_t driver;

    memset(&driver, 0, sizeof(driver));
    driver.name = name;
    driver.init = init;
    driver.shutdown = shutdown;
    driver.depends_on = depends_on;
    driver.probe = probe != NULL ? probe : driver_manager_probe_loaded;
    driver.priority = priority;
    driver.loaded = already_loaded;
    driver.verified = g_kernel_signature_ready;
    driver.critical = critical;
    driver.unloadable = shutdown != NULL;
    driver.adapter_score = -1;
    driver_manager_append(&driver);
}

static void driver_manager_register_builtin(const char *name,
                                            driver_init_fn_t init,
                                            driver_shutdown_fn_t shutdown,
                                            bool already_loaded,
                                            bool critical)
{
    driver_manager_register_builtin_ex(name,
                                       init,
                                       shutdown,
                                       already_loaded,
                                       critical,
                                       NULL,
                                       1000U - g_driver_count * 10U,
                                       NULL);
}

bool driver_manager_unload(const char *name, bool force)
{
    int32_t index = driver_manager_find_index(name);
    kernel_driver_t *driver;

    if (index < 0 || g_driver_manager_callback_driver != NULL) {
        return false;
    }
    driver = &g_drivers[index];
    if (!driver->loaded || !driver->unloadable ||
        (driver->critical && !force)) {
        return false;
    }
    driver_manager_release(driver);
    driver_manager_log_result(driver);
    return true;
}

bool driver_manager_snapshot(driver_status_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    for (uint32_t i = 0; i < g_driver_count && snapshot->count < DRIVER_STATUS_MAX; i++) {
        const kernel_driver_t *driver = &g_drivers[i];
        driver_status_entry_t *entry = &snapshot->entries[snapshot->count++];

        strlcpy(entry->name, driver->name != NULL ? driver->name : "", sizeof(entry->name));
        if (driver->external) {
            entry->flags |= DRIVER_STATUS_FLAG_EXTERNAL;
        }
        if (driver->verified) {
            entry->flags |= DRIVER_STATUS_FLAG_VERIFIED;
        }
        if (driver->critical) {
            entry->flags |= DRIVER_STATUS_FLAG_CRITICAL;
        }
        if (driver->unloadable) {
            entry->flags |= DRIVER_STATUS_FLAG_UNLOADABLE;
        }
        if (driver->adapter_ready) {
            entry->flags |= DRIVER_STATUS_FLAG_ADAPTER_READY;
        } else if (driver->loaded && driver->adapter_score > 0) {
            entry->flags |= DRIVER_STATUS_FLAG_DEGRADED;
        }
        if (driver->scheduled) {
            entry->flags |= DRIVER_STATUS_FLAG_SCHEDULED;
        }
        entry->adapter_score = driver->adapter_score;
        entry->priority = driver->priority;
        entry->last_update_tick = driver->last_update_tick;
        entry->loaded = driver->loaded ? 1 : 0;
        entry->adapter_ready = driver->adapter_ready ? 1 : 0;
        entry->scheduled = driver->scheduled ? 1 : 0;
        if (driver->loaded) {
            snapshot->loaded_count++;
        }
    }
    return true;
}

bool driver_manager_rescan(void)
{
    gpu_rescan();
    for (uint32_t i = 0; i < g_driver_count; i++) {
        kernel_driver_t *driver = &g_drivers[i];

        if (!driver->loaded) {
            driver->adapter_score = 0;
            driver->adapter_ready = false;
            continue;
        }
        if (driver->probe == NULL) {
            driver->adapter_score = -1;
            driver->adapter_ready = false;
            continue;
        }
        driver->adapter_score = driver->probe();
        if (driver->adapter_score < 0) {
            driver->adapter_score = 0;
        } else if (driver->adapter_score > 100) {
            driver->adapter_score = 100;
        }
        driver->adapter_ready = driver->adapter_score >= 50;
    }
    return true;
}

void driver_manager_update(uint64_t now_ticks)
{
    if (g_driver_manager_update_running) {
        return;
    }
    g_driver_manager_update_running = true;
    if (g_driver_manager_last_rescan_tick == (uint64_t) -1 ||
        now_ticks < g_driver_manager_last_rescan_tick ||
        now_ticks - g_driver_manager_last_rescan_tick >= DRIVER_MANAGER_RESCAN_INTERVAL) {
        driver_manager_rescan();
        g_driver_manager_last_rescan_tick = now_ticks;
    }

    for (uint32_t i = 0; i < g_driver_count; i++) {
        kernel_driver_t *driver = &g_drivers[i];

        if (!driver->loaded ||
            !driver->external ||
            driver->tick == NULL ||
            driver->update_interval_ticks == 0 ||
            (driver->next_update_tick != 0 && now_ticks < driver->next_update_tick)) {
            continue;
        }
        g_driver_manager_callback_driver = driver;
        driver->tick(now_ticks);
        g_driver_manager_callback_driver = NULL;
        if (!driver->loaded) {
            continue;
        }
        driver->last_update_tick = now_ticks;
        driver->next_update_tick = now_ticks + driver->update_interval_ticks;
        driver->scheduled = true;
    }
    g_driver_manager_update_running = false;
}

bool driver_manager_rebind(const char *name)
{
    int32_t index = driver_manager_find_index(name);
    kernel_driver_t *driver;
    char path[REGISTRY_VALUE_MAX];
    bool loaded;

    if (index < 0 || g_driver_manager_callback_driver != NULL) {
        return false;
    }
    driver = &g_drivers[index];
    if (!driver->loaded || !driver->unloadable) {
        return false;
    }
    if (driver->external) {
        strlcpy(path, driver->name, sizeof(path));
        driver_manager_release(driver);
        loaded = driver_manager_load(path);
        driver_manager_rescan();
        return loaded;
    }
    driver_manager_release(driver);
    loaded = driver->init != NULL && driver->init();
    driver->loaded = loaded;
    driver_manager_rescan();
    driver_manager_log_result(driver);
    return loaded;
}

void driver_manager_init(void)
{
    memset(g_drivers, 0, sizeof(g_drivers));
    memset(g_external_driver_names, 0, sizeof(g_external_driver_names));
    g_driver_count = 0;
    g_external_driver_count = 0;
    memset(g_kernel_signer_id, 0, sizeof(g_kernel_signer_id));
    g_kernel_signature_ready = false;
    g_driver_manager_update_running = false;
    g_driver_manager_callback_driver = NULL;
    g_driver_manager_last_rescan_tick = (uint64_t) -1;

    if (!driver_manager_load_kernel_signature()) {
        return;
    }

    driver_manager_register_builtin_ex("graphics",
                                       graphics_driver_init,
                                       graphics_shutdown,
                                       false,
                                       true,
                                       NULL,
                                       1000,
                                       driver_manager_probe_graphics);
    driver_manager_register_builtin_ex("nvidia",
                                       driver_manager_init_nvidia,
                                       NULL,
                                       false,
                                       false,
                                       "graphics",
                                       980,
                                       driver_manager_probe_nvidia);
    driver_manager_register_builtin_ex("igpu",
                                       driver_manager_init_igpu,
                                       NULL,
                                       false,
                                       false,
                                       "graphics",
                                       970,
                                       driver_manager_probe_igpu);
    driver_manager_register_builtin_ex("virtio",
                                       virtio_driver_init,
                                       virtio_shutdown,
                                       false,
                                       false,
                                       NULL,
                                       960,
                                       driver_manager_probe_virtio);
    driver_manager_register_builtin_ex("virtio-blk",
                                       virtio_blk_driver_init,
                                       virtio_blk_shutdown,
                                       false,
                                       false,
                                       "virtio",
                                       955,
                                       driver_manager_probe_virtio_blk);
    driver_manager_register_builtin("ide", ide_driver_init, ide_shutdown, false, true);
    driver_manager_register_builtin_ex("ahci",
                                       ahci_driver_init,
                                       ahci_shutdown,
                                       false,
                                       true,
                                       NULL,
                                       945,
                                       driver_manager_probe_ahci);
    driver_manager_register_builtin("nvme", nvme_driver_init, nvme_shutdown, false, true);
    driver_manager_register_builtin("xhci", xhci_driver_init, xhci_shutdown, false, true);
    driver_manager_register_builtin_ex("hda",
                                       hda_driver_init,
                                       hda_shutdown,
                                       false,
                                       true,
                                       NULL,
                                       950,
                                       driver_manager_probe_hda);
    driver_manager_register_builtin("smbus",
                                    smbus_driver_init,
                                    smbus_driver_shutdown,
                                    false,
                                    true);
    driver_manager_register_builtin_ex("onboard-net",
                                       net_driver_init,
                                       net_shutdown,
                                       false,
                                       true,
                                       NULL,
                                       930,
                                       driver_manager_probe_net);
    driver_manager_register_builtin("onboard-audio", NULL, audio_shutdown, true, true);

    {
        bool initialized[DRIVER_MANAGER_MAX];

        memset(initialized, 0, sizeof(initialized));
        for (uint32_t pass = 0; pass < g_driver_count; pass++) {
            int32_t selected = -1;

            for (uint32_t i = 0; i < g_driver_count; i++) {
                if (initialized[i] ||
                    !driver_manager_dependencies_ready(&g_drivers[i]) ||
                    (selected >= 0 && g_drivers[i].priority <=
                     g_drivers[(uint32_t) selected].priority)) {
                    continue;
                }
                selected = (int32_t) i;
            }
            if (selected < 0) {
                break;
            }
            initialized[(uint32_t) selected] = true;
            if (g_drivers[(uint32_t) selected].init != NULL) {
                g_drivers[(uint32_t) selected].loaded =
                    g_drivers[(uint32_t) selected].init();
            }
            driver_manager_log_result(&g_drivers[(uint32_t) selected]);
        }
        for (uint32_t i = 0; i < g_driver_count; i++) {
            if (!initialized[i]) {
                log_write("driver-manager: dependency not ready");
                driver_manager_log_result(&g_drivers[i]);
            }
        }
    }
    driver_manager_load_registry_drivers();
    driver_manager_rescan();
}

void driver_manager_shutdown(void)
{
    for (int32_t i = (int32_t) g_driver_count - 1; i >= 0; i--) {
        driver_manager_release(&g_drivers[i]);
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
