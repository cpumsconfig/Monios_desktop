#include "common.h"
#include "authenticode.h"
#include "exec.h"
#include "file.h"
#include "hash.h"
#include "rsa.h"
#include "memory.h"
#include "mmu.h"
#include "path.h"
#include "pcb.h"
#include "session.h"
#include "terminal.h"
#include "trust_store.h"
#include "ui.h"
#include "vm.h"
#include "user_mgmt.h"

#define ELF_MAGIC_0 0x7F
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'
#define ELF_CLASS_64 2
#define ELF_DATA_LSB 1
#define ELF_TYPE_EXEC 2
#define ELF_MACHINE_X86_64 0x3E
#define ELF_PT_LOAD 1

#define PE_DOS_MAGIC 0x5A4D
#define PE_NT_SIGNATURE 0x00004550U
#define PE_MACHINE_AMD64 0x8664
#define PE_OPTIONAL_MAGIC_PE32_PLUS 0x20B
#define PE_FILE_EXECUTABLE_IMAGE 0x0002
#define PE_FILE_DLL 0x2000
#define PE_SECTION_MEM_EXECUTE 0x20000000U
#define PE_DIRECTORY_EXPORT 0U
#define PE_DIRECTORY_IMPORT 1U
#define PE_DIRECTORY_RESOURCE 2U
#define PE_DIRECTORY_SECURITY 4U
#define PE_DIRECTORY_COUNT 16U
#define PE_IMPORT_ORDINAL_FLAG64 0x8000000000000000ULL
#define PE_RESOURCE_ENTRY_NAME_IS_STRING 0x80000000U
#define PE_RESOURCE_ENTRY_IS_DIRECTORY 0x80000000U
#define PE_RESOURCE_OFFSET_MASK 0x7FFFFFFFU
#define PE_RESOURCE_ID_ICON 3U
#define PE_RESOURCE_ID_GROUP_ICON 14U
#define PE_WIN_CERT_REVISION_1 0x0100
#define PE_WIN_CERT_REVISION_2 0x0200
#define PE_WIN_CERT_TYPE_PKCS_SIGNED_DATA 0x0002
#define PE_RESOURCE_ID_VERSION 16U
#define PE_RESOURCE_ID_MANIFEST 24U
#define PE_RESOURCE_MAX_ENTRIES 256U
#define PE_RESOURCE_MAX_DEPTH 4U
#define PE_RESOURCE_ANY_ID 0xFFFFFFFFU
#define MONIOS_SIGNER_ID_SIZE 32U
#define EXEC_DLL_MAX 8U

#define RZS_MAGIC_0 'R'
#define RZS_MAGIC_1 'Z'
#define RZS_MAGIC_2 'S'
#define RZS_MAGIC_3 '1'
#define RZS_VERSION 1U
#define RZS_SIGNATURE_SIZE 64U
#define RZS_SIGNED_MANIFEST_SIZE (4U + 2U + 2U + 4U + 4U + 4U + HASH_SHA256_DIGEST_SIZE)

static const uint8_t g_rzs_public_modulus[RSA_MAX_MODULUS_BYTES] = {
    0x8C, 0x39, 0xED, 0x88, 0x71, 0xA8, 0xED, 0xF7,
    0x3B, 0xFB, 0x66, 0x36, 0x1D, 0xFF, 0x1A, 0x10,
    0x8E, 0x9F, 0x46, 0x72, 0xD8, 0xA2, 0xA6, 0x71,
    0xA5, 0x30, 0x25, 0x86, 0x39, 0x5F, 0xE8, 0x80,
    0x12, 0x3B, 0x86, 0x5F, 0xC6, 0x04, 0x95, 0xAB,
    0x45, 0x90, 0xF3, 0xFA, 0x5A, 0x92, 0x00, 0x68,
    0x97, 0x9F, 0x1C, 0xC5, 0x1F, 0xDF, 0x49, 0xD9,
    0x71, 0x5F, 0x70, 0x4B, 0xA6, 0x52, 0x4D, 0xDD
};

static const uint8_t g_rzs_public_exponent[] = { 0x01, 0x00, 0x01 };

static void exec_write_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t) (value & 0xFF);
    dst[1] = (uint8_t) ((value >> 8) & 0xFF);
}

static void exec_write_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t) (value & 0xFF);
    dst[1] = (uint8_t) ((value >> 8) & 0xFF);
    dst[2] = (uint8_t) ((value >> 16) & 0xFF);
    dst[3] = (uint8_t) ((value >> 24) & 0xFF);
}

static void exec_write_u64(uint8_t *dst, uint64_t value)
{
    dst[0] = (uint8_t) (value & 0xFF);
    dst[1] = (uint8_t) ((value >> 8) & 0xFF);
    dst[2] = (uint8_t) ((value >> 16) & 0xFF);
    dst[3] = (uint8_t) ((value >> 24) & 0xFF);
    dst[4] = (uint8_t) ((value >> 32) & 0xFF);
    dst[5] = (uint8_t) ((value >> 40) & 0xFF);
    dst[6] = (uint8_t) ((value >> 48) & 0xFF);
    dst[7] = (uint8_t) ((value >> 56) & 0xFF);
}

static uint16_t exec_read_u16(const uint8_t *src)
{
    return (uint16_t) src[0] |
           ((uint16_t) src[1] << 8);
}

static uint32_t exec_read_u32(const uint8_t *src)
{
    return (uint32_t) src[0] |
           ((uint32_t) src[1] << 8) |
           ((uint32_t) src[2] << 16) |
           ((uint32_t) src[3] << 24);
}

static uint64_t exec_read_u64(const uint8_t *src)
{
    return (uint64_t) exec_read_u32(src) |
           ((uint64_t) exec_read_u32(src + 4) << 32);
}

static void exec_build_rzs_manifest(const uint8_t magic[4],
                                    uint16_t version,
                                    uint16_t header_size,
                                    uint32_t image_size,
                                    uint32_t image_flags,
                                    uint32_t signature_size,
                                    const uint8_t image_hash[HASH_SHA256_DIGEST_SIZE],
                                    uint8_t manifest[RZS_SIGNED_MANIFEST_SIZE])
{
    uint32_t offset = 0;

    memcpy(manifest + offset, magic, 4);
    offset += 4;
    exec_write_u16(manifest + offset, version);
    offset += 2;
    exec_write_u16(manifest + offset, header_size);
    offset += 2;
    exec_write_u32(manifest + offset, image_size);
    offset += 4;
    exec_write_u32(manifest + offset, image_flags);
    offset += 4;
    exec_write_u32(manifest + offset, signature_size);
    offset += 4;
    memcpy(manifest + offset, image_hash, HASH_SHA256_DIGEST_SIZE);
}

static bool exec_rzs_signature_zero(const uint8_t signature[RZS_SIGNATURE_SIZE])
{
    for (uint32_t i = 0; i < RZS_SIGNATURE_SIZE; i++) {
        if (signature[i] != 0) {
            return false;
        }
    }
    return true;
}

typedef struct {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf64_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) elf64_phdr_t;

typedef struct {
    uint16_t e_magic;
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    int32_t e_lfanew;
} __attribute__((packed)) pe_dos_header_t;

typedef struct {
    uint16_t machine;
    uint16_t number_of_sections;
    uint32_t time_date_stamp;
    uint32_t pointer_to_symbol_table;
    uint32_t number_of_symbols;
    uint16_t size_of_optional_header;
    uint16_t characteristics;
} __attribute__((packed)) pe_file_header_t;

typedef struct {
    uint32_t virtual_address;
    uint32_t size;
} __attribute__((packed)) pe_data_directory_t;

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
    pe_data_directory_t data_directories[PE_DIRECTORY_COUNT];
} __attribute__((packed)) pe_optional_header64_t;

typedef struct {
    uint8_t name[8];
    uint32_t virtual_size;
    uint32_t virtual_address;
    uint32_t size_of_raw_data;
    uint32_t pointer_to_raw_data;
    uint32_t pointer_to_relocations;
    uint32_t pointer_to_linenumbers;
    uint16_t number_of_relocations;
    uint16_t number_of_linenumbers;
    uint32_t characteristics;
} __attribute__((packed)) pe_section_header_t;

typedef struct {
    uint32_t original_first_thunk;
    uint32_t time_date_stamp;
    uint32_t forwarder_chain;
    uint32_t name;
    uint32_t first_thunk;
} __attribute__((packed)) pe_import_descriptor_t;

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
} __attribute__((packed)) pe_export_directory_t;

typedef struct {
    uint32_t characteristics;
    uint32_t time_date_stamp;
    uint16_t major_version;
    uint16_t minor_version;
    uint16_t number_of_named_entries;
    uint16_t number_of_id_entries;
} __attribute__((packed)) pe_resource_directory_t;

typedef struct {
    uint32_t name;
    uint32_t offset_to_data;
} __attribute__((packed)) pe_resource_directory_entry_t;

typedef struct {
    uint32_t data_rva;
    uint32_t size;
    uint32_t code_page;
    uint32_t reserved;
} __attribute__((packed)) pe_resource_data_entry_t;

typedef struct {
    uint8_t magic[4];
    uint16_t version;
    uint16_t header_size;
    uint32_t image_size;
    uint32_t image_flags;
    uint32_t signature_size;
    uint8_t image_sha256[HASH_SHA256_DIGEST_SIZE];
    uint8_t signature[RZS_SIGNATURE_SIZE];
} __attribute__((packed)) rzs_header_t;

typedef enum {
    EXEC_IMAGE_FORMAT_ELF,
    EXEC_IMAGE_FORMAT_PE,
    EXEC_IMAGE_FORMAT_DOS
} exec_image_format_t;

/* DOS compat entry hook.  kernel/compat/dos.c registers itself here once
 * it is linked into the build (see report).  Until then it is NULL and a
 * pure-DOS MZ image is rejected exactly as before - no behaviour change. */
static int (*g_dos_compat_run)(const uint8_t *image, uint32_t size, int32_t *exit_code) = NULL;

void exec_set_dos_entry(int (*fn)(const uint8_t *, uint32_t, int32_t *))
{
    g_dos_compat_run = fn;
}

/* Detect MZ magic with NO PE signature (legacy 16-bit DOS image). */
static bool exec_is_pure_dos_mz(const uint8_t *image, uint32_t size)
{
    if (image == NULL || size < 0x40) return false;
    if (image[0] != 'M' || image[1] != 'Z') return false;
    int32_t lfanew = (int32_t)image[0x3C]
                   | ((int32_t)image[0x3D] << 8)
                   | ((int32_t)image[0x3E] << 16)
                   | ((int32_t)image[0x3F] << 24);
    if (lfanew >= 0 && (uint32_t)lfanew + 4u <= size) {
        const uint8_t *sig = image + (uint32_t)lfanew;
        if (sig[0] == 'P' && sig[1] == 'E' && sig[2] == 0 && sig[3] == 0)
            return false;
    }
    return true;
}

typedef struct {
    exec_image_format_t format;
    const uint8_t *image;
    uint32_t image_size;
    const elf64_ehdr_t *elf_header;
    const pe_dos_header_t *pe_dos_header;
    const pe_file_header_t *pe_file_header;
    const pe_optional_header64_t *pe_optional_header;
    const pe_section_header_t *pe_sections;
    uint64_t entry;
    uint32_t subsystem;
    uint32_t certificate_table_offset;
    uint32_t certificate_table_size;
    uint32_t resource_table_rva;
    uint32_t resource_table_size;
    bool is_dll;
    bool certificate_present;
    bool resource_present;
    bool icon_resource_present;
    bool manifest_resource_present;
    bool elevated_manifest_present;
    bool version_resource_present;
} exec_prepared_image_t;

typedef struct {
    char name[32];
    char path[PATH_MAX_LEN];
    uint8_t *image;
    uint32_t image_size;
    exec_prepared_image_t prepared;
    bool loaded;
} exec_loaded_dll_t;

typedef int32_t (*exec_entry_t)(const exec_launch_info_t *info);

typedef struct {
    const exec_launch_info_t *launch_info;
    uint32_t image_flags;
    uint32_t privilege_level;
    char cwd[PATH_MAX_LEN];
    char program_path[PATH_MAX_LEN];
    int32_t exit_code;
    uint8_t fault_vector;
    uint64_t fault_error_code;
    bool completed;
    bool faulted;
    uint8_t *kernel_stack;
    uint64_t resume_rsp;
} exec_runtime_context_t;

static exec_runtime_context_t *g_exec_context;

extern void exec_enter_user_mode(uint64_t entry,
                                 uint64_t user_stack_top,
                                 uint64_t launch_info_ptr,
                                 uint64_t kernel_resume_stack_top,
                                 uint64_t resume_rsp_slot_ptr);
extern void exec_enter_kernel_mode(uint64_t entry,
                                   uint64_t launch_info_ptr,
                                   uint64_t kernel_stack_top,
                                   uint64_t resume_rsp_slot_ptr);

static bool exec_scan_pe_resources(exec_prepared_image_t *prepared);

static bool exec_range_valid(uint64_t start, uint64_t size)
{
    uint64_t end;

    if (size > EXEC_IMAGE_LIMIT - EXEC_LOAD_BASE) {
        return false;
    }
    if (start < EXEC_LOAD_BASE || start >= EXEC_IMAGE_LIMIT) {
        return false;
    }

    end = start + size;
    if (end < start || end > EXEC_IMAGE_LIMIT) {
        return false;
    }
    return true;
}

static uint8_t *exec_user_alloc(uint8_t **cursor, uint32_t size, uint32_t align, uint8_t *limit)
{
    uint64_t address = (uint64_t) *cursor;
    uint64_t aligned;
    uint8_t *result;

    if (cursor == NULL || limit == NULL || align == 0 ||
        (align & (align - 1U)) != 0 ||
        address > 0xFFFFFFFFFFFFFFFFULL - (uint64_t) (align - 1U)) {
        return NULL;
    }
    aligned = (address + (uint64_t) (align - 1U)) & ~(uint64_t) (align - 1U);
    if (aligned > (uint64_t) limit ||
        (uint64_t) size > (uint64_t) limit - aligned) {
        return NULL;
    }
    result = (uint8_t *) aligned;
    *cursor = result + size;
    return result;
}

static char *exec_user_copy_string(uint8_t **cursor, uint8_t *limit, const char *text)
{
    uint32_t size = (uint32_t) strlen(text) + 1;
    char *target = (char *) exec_user_alloc(cursor, size, 1, limit);

    if (target == NULL) {
        return NULL;
    }
    memcpy(target, text, size);
    return target;
}

static exec_launch_info_t *exec_build_user_launch_info(uint32_t argc, char *argv[], const char *cwd, const char *program_path, char *env[], uint32_t env_count, uint32_t image_flags, uint32_t privilege_level, uint32_t subsystem)
{
#define EXEC_MAX_ARGC 128U
#define EXEC_MAX_ENV  128U
    uint8_t *cursor = (uint8_t *) EXEC_USER_DATA_BASE;
    uint8_t *limit = (uint8_t *) EXEC_USER_DATA_LIMIT;
    exec_launch_info_t *info = (exec_launch_info_t *) exec_user_alloc(&cursor, sizeof(exec_launch_info_t), 16, limit);
    char **user_argv = NULL;
    char **user_env = NULL;

    if (argc > EXEC_MAX_ARGC || env_count > EXEC_MAX_ENV ||
        (argc > 0 && (argv == NULL || argv[0] == NULL)) ||
        (env_count > 0 && env == NULL) ||
        cwd == NULL || program_path == NULL || info == NULL) {
        return NULL;
    }

    if (argc > 0) {
        user_argv = (char **) exec_user_alloc(&cursor, sizeof(char *) * argc, 8, limit);
        if (user_argv == NULL) {
            return NULL;
        }
    }

    info->abi_version = EXEC_ABI_VERSION;
    info->image_flags = image_flags;
    info->privilege_level = privilege_level;
    info->subsystem = subsystem;
    info->argc = argc;
    info->argv = user_argv;
    info->env = NULL;
    info->env_count = 0;
    info->cwd = exec_user_copy_string(&cursor, limit, cwd);
    info->program_path = exec_user_copy_string(&cursor, limit, program_path);
    {
        const session_user_t *user = session_current_user();
        info->user_name = exec_user_copy_string(&cursor, limit, user != NULL ? user->name : "system");
    }
    info->stdin_handle = EXEC_HANDLE_STDIN;
    info->stdout_handle = EXEC_HANDLE_STDOUT;
    info->stderr_handle = EXEC_HANDLE_STDERR;

    if (info->cwd == NULL || info->program_path == NULL || info->user_name == NULL) {
        return NULL;
    }

    for (uint32_t i = 0; i < argc; i++) {
        user_argv[i] = exec_user_copy_string(&cursor, limit, argv[i]);
        if (user_argv[i] == NULL) {
            return NULL;
        }
    }
    /* copy environment strings if provided */
    if (env != NULL && env_count > 0) {
        user_env = (char **) exec_user_alloc(&cursor, sizeof(char *) * env_count, 8, limit);
        if (user_env == NULL) {
            return NULL;
        }
        for (uint32_t i = 0; i < env_count; i++) {
            user_env[i] = exec_user_copy_string(&cursor, limit, env[i]);
            if (user_env[i] == NULL) {
                return NULL;
            }
        }
        info->env = user_env;
        info->env_count = env_count;
    }

    return info;
#undef EXEC_MAX_ARGC
#undef EXEC_MAX_ENV
}

static bool exec_path_has_suffix(const char *path, const char *suffix)
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

static bool exec_path_basename_equals(const char *path, const char *name)
{
    const char *base = path;

    if (path == NULL || name == NULL) {
        return false;
    }
    while (*path != '\0') {
        if (*path == PATH_SEPARATOR) {
            base = path + 1;
        }
        path++;
    }
    return strcasecmp(base, name) == 0;
}

static bool exec_path_is_driver_image(const char *path)
{
    return exec_path_has_suffix(path, ".sys") || exec_path_has_suffix(path, ".rzs");
}

static bool exec_path_is_gui_image(const char *path)
{
    return exec_path_basename_equals(path, "explorar.exe") ||
           exec_path_basename_equals(path, "monilog.exe") ||
           exec_path_basename_equals(path, "player.exe") ||
           exec_path_basename_equals(path, "notepad.exe") ||
           exec_path_basename_equals(path, "taskmgr.exe") ||
           exec_path_basename_equals(path, "cube3d.exe");
}

static bool exec_path_is_elevated_tool(const char *path)
{
    return exec_path_basename_equals(path, "setup.exe") ||
           exec_path_basename_equals(path, "sysinst.exe");
}

static uint32_t exec_default_flags_for_path(const char *resolved_path)
{
    uint32_t flags;

    if (exec_path_is_driver_image(resolved_path)) {
        return EXEC_IMAGE_FLAG_DRIVER | EXEC_IMAGE_FLAG_NEEDS_R2 | EXEC_IMAGE_FLAG_CONSOLE;
    }
    flags = exec_path_is_gui_image(resolved_path) ? EXEC_IMAGE_FLAG_GUI : EXEC_IMAGE_FLAG_CONSOLE;
    if (exec_path_is_elevated_tool(resolved_path)) {
        flags |= EXEC_IMAGE_FLAG_NEEDS_R2;
    }
    return flags;
}

static uint32_t exec_privilege_flags_for_path(const char *resolved_path)
{
    if (exec_path_is_driver_image(resolved_path) || exec_path_is_elevated_tool(resolved_path)) {
        return EXEC_IMAGE_FLAG_NEEDS_R2;
    }
    return 0;
}

static uint32_t exec_surface_flags_for_path(const char *resolved_path)
{
    if (exec_path_is_gui_image(resolved_path)) {
        return EXEC_IMAGE_FLAG_GUI;
    }
    return EXEC_IMAGE_FLAG_CONSOLE;
}

static uint32_t exec_flags_for_prepared_image(const exec_prepared_image_t *prepared, const char *resolved_path, uint32_t base_flags)
{
    uint32_t flags = base_flags;

    flags &= ~(EXEC_IMAGE_FLAG_CONSOLE | EXEC_IMAGE_FLAG_GUI);
    if (exec_path_is_driver_image(resolved_path)) {
        flags |= EXEC_IMAGE_FLAG_DRIVER | EXEC_IMAGE_FLAG_NEEDS_R2 | EXEC_IMAGE_FLAG_CONSOLE;
    } else if (prepared != NULL && prepared->format == EXEC_IMAGE_FORMAT_PE) {
        if (prepared->subsystem == EXEC_SUBSYSTEM_WINDOWS) {
            flags |= EXEC_IMAGE_FLAG_GUI;
        } else {
            flags |= EXEC_IMAGE_FLAG_CONSOLE;
        }
    } else {
        flags |= exec_surface_flags_for_path(resolved_path);
    }
    flags |= exec_privilege_flags_for_path(resolved_path);
    if (prepared != NULL && prepared->certificate_present) {
        flags |= EXEC_IMAGE_FLAG_CERT_PRESENT;
    }
    if (prepared != NULL && prepared->resource_present) {
        flags |= EXEC_IMAGE_FLAG_RESOURCE_TABLE;
        if (prepared->icon_resource_present) {
            flags |= EXEC_IMAGE_FLAG_ICON_RESOURCE;
        }
        if (prepared->manifest_resource_present) {
            flags |= EXEC_IMAGE_FLAG_MANIFEST_RESOURCE;
        }
        if (prepared->elevated_manifest_present) {
            flags |= EXEC_IMAGE_FLAG_NEEDS_R2;
        }
    }
    return flags;
}

static bool exec_verify_rzs_header(const uint8_t *image, uint32_t image_size, const uint8_t **payload_image, uint32_t *payload_size, uint32_t *image_flags)
{
    const rzs_header_t *header;
    uint8_t digest[HASH_SHA256_DIGEST_SIZE];
    uint8_t manifest[RZS_SIGNED_MANIFEST_SIZE];
    uint8_t manifest_digest[HASH_SHA256_DIGEST_SIZE];
    rsa_pubkey_t rzs_pubkey;
    bool signature_valid = false;

    if (image == NULL || payload_image == NULL || payload_size == NULL || image_flags == NULL) {
        return false;
    }
    if (image_size < sizeof(rzs_header_t)) {
        return false;
    }
    header = (const rzs_header_t *) image;
    if (header->magic[0] != RZS_MAGIC_0 ||
        header->magic[1] != RZS_MAGIC_1 ||
        header->magic[2] != RZS_MAGIC_2 ||
        header->magic[3] != RZS_MAGIC_3) {
        return false;
    }
    if (header->version != RZS_VERSION || header->header_size < sizeof(rzs_header_t)) {
        return false;
    }
    if (header->header_size > image_size ||
        header->image_size == 0 ||
        header->image_size > image_size - header->header_size ||
        header->signature_size != RZS_SIGNATURE_SIZE) {
        return false;
    }
    *payload_image = image + header->header_size;
    *payload_size = header->image_size;
    hash_sha256(*payload_image, *payload_size, digest);
    if (memcmp(digest, header->image_sha256, sizeof(digest)) != 0) {
        return false;
    }
    exec_build_rzs_manifest(header->magic,
                            header->version,
                            header->header_size,
                            header->image_size,
                            header->image_flags,
                            header->signature_size,
                            digest,
                            manifest);
    hash_sha256(manifest, sizeof(manifest), manifest_digest);
    if (!exec_rzs_signature_zero(header->signature)) {
        if (rsa_pubkey_init(&rzs_pubkey,
                            g_rzs_public_modulus, sizeof(g_rzs_public_modulus),
                            g_rzs_public_exponent, sizeof(g_rzs_public_exponent)) != 0) {
            return false;
        }
        signature_valid = rsa_verify_pkcs1_v15(&rzs_pubkey,
                                               header->signature,
                                               RZS_SIGNATURE_SIZE,
                                               manifest_digest,
                                               sizeof(manifest_digest),
                                               RSA_HASH_SHA256) == 0;
        if (!signature_valid) {
            return false;
        }
    } else if ((header->image_flags & EXEC_IMAGE_FLAG_NEEDS_R0) != 0) {
        return false;
    }
    *image_flags = header->image_flags &
                   (EXEC_IMAGE_FLAG_CONSOLE |
                    EXEC_IMAGE_FLAG_GUI |
                    EXEC_IMAGE_FLAG_DRIVER |
                    EXEC_IMAGE_FLAG_NEEDS_R2 |
                    EXEC_IMAGE_FLAG_NEEDS_R0);
    if (signature_valid) {
        *image_flags |= EXEC_IMAGE_FLAG_SIGNED;
    }
    return true;
}

static bool exec_image_has_pe_signature(const uint8_t *image, uint32_t image_size)
{
    const pe_dos_header_t *dos_header;
    const uint8_t *signature;

    if (image == NULL || image_size < sizeof(pe_dos_header_t)) {
        return false;
    }
    dos_header = (const pe_dos_header_t *) image;
    if (dos_header->e_magic != PE_DOS_MAGIC ||
        dos_header->e_lfanew < (int32_t) sizeof(pe_dos_header_t) ||
        (uint32_t) dos_header->e_lfanew + 4U > image_size) {
        return false;
    }
    signature = image + (uint32_t) dos_header->e_lfanew;
    return signature[0] == 'P' && signature[1] == 'E' && signature[2] == '\0' && signature[3] == '\0';
}

static bool exec_prepare_image(const uint8_t *image, uint32_t image_size, const char *resolved_path, exec_prepared_image_t *prepared, uint32_t *image_flags)
{
    uint32_t flags = 0;
    const uint8_t *payload = image;
    uint32_t payload_size = image_size;

    if (image == NULL || prepared == NULL || image_flags == NULL) {
        return false;
    }
    if (exec_path_has_suffix(resolved_path, ".rzs")) {
        if (!exec_verify_rzs_header(image, image_size, &payload, &payload_size, &flags)) {
            return false;
        }
    }

    memset(prepared, 0, sizeof(*prepared));
    prepared->image = payload;
    prepared->image_size = payload_size;
    if (!exec_image_has_pe_signature(payload, payload_size)) {
        /* Legacy 16-bit DOS MZ/COM image? Route to NTVDM compat layer when
         * linked in.  Hook is NULL by default -> behaviour unchanged. */
        if (g_dos_compat_run != NULL &&
            exec_is_pure_dos_mz(payload, payload_size)) {
            prepared->format     = EXEC_IMAGE_FORMAT_DOS;
            prepared->image      = payload;
            prepared->image_size = payload_size;
            prepared->entry      = 0;
            *image_flags = flags | EXEC_IMAGE_FLAG_CONSOLE;
            return true;
        }
        return false;
    }
    prepared->format = EXEC_IMAGE_FORMAT_PE;
    *image_flags = flags | exec_default_flags_for_path(resolved_path);
    return true;
}

static bool exec_validate_elf_header(exec_prepared_image_t *prepared)
{
    const elf64_ehdr_t *header;
    uint32_t image_size;

    if (prepared == NULL || prepared->image == NULL) {
        return false;
    }
    header = (const elf64_ehdr_t *) prepared->image;
    image_size = prepared->image_size;
    if (image_size < sizeof(elf64_ehdr_t)) {
        return false;
    }
    if (header->e_ident[0] != ELF_MAGIC_0 ||
        header->e_ident[1] != ELF_MAGIC_1 ||
        header->e_ident[2] != ELF_MAGIC_2 ||
        header->e_ident[3] != ELF_MAGIC_3) {
        return false;
    }
    if (header->e_ident[4] != ELF_CLASS_64 || header->e_ident[5] != ELF_DATA_LSB) {
        return false;
    }
    if (header->e_type != ELF_TYPE_EXEC || header->e_machine != ELF_MACHINE_X86_64) {
        return false;
    }
    if (header->e_phentsize != sizeof(elf64_phdr_t)) {
        return false;
    }
    if (header->e_phoff > image_size ||
        header->e_phnum > (image_size - header->e_phoff) / sizeof(elf64_phdr_t)) {
        return false;
    }
    if (!exec_range_valid(header->e_entry, 1)) {
        return false;
    }
    prepared->elf_header = header;
    prepared->entry = header->e_entry;
    prepared->subsystem = EXEC_SUBSYSTEM_CONSOLE;
    return true;
}

static bool exec_validate_pe_header(exec_prepared_image_t *prepared)
{
    const uint8_t *image;
    uint32_t image_size;
    const pe_dos_header_t *dos_header;
    const pe_file_header_t *file_header;
    const pe_optional_header64_t *optional_header;
    const pe_section_header_t *sections;
    const pe_data_directory_t *security_directory;
        const pe_data_directory_t *resource_directory;
uint64_t pe_offset;
    uint64_t file_header_offset;
    uint64_t optional_header_offset;
    uint64_t section_header_offset;
    uint64_t entry;
    bool entry_executable = false;
    bool entry_required;

    if (prepared == NULL || prepared->image == NULL) {
        return false;
    }
    image = prepared->image;
    image_size = prepared->image_size;
    if (image_size < sizeof(pe_dos_header_t) || !exec_image_has_pe_signature(image, image_size)) {
        return false;
    }
    dos_header = (const pe_dos_header_t *) image;
    pe_offset = (uint32_t) dos_header->e_lfanew;
    file_header_offset = pe_offset + 4U;
    optional_header_offset = file_header_offset + sizeof(pe_file_header_t);
    if (optional_header_offset > image_size ||
        optional_header_offset + sizeof(pe_optional_header64_t) > image_size) {
        return false;
    }
    file_header = (const pe_file_header_t *) (image + file_header_offset);
    if (file_header->machine != PE_MACHINE_AMD64 ||
        file_header->number_of_sections == 0 ||
        (file_header->characteristics & PE_FILE_EXECUTABLE_IMAGE) == 0 ||
        file_header->size_of_optional_header < sizeof(pe_optional_header64_t)) {
        return false;
    }
    section_header_offset = optional_header_offset + file_header->size_of_optional_header;
    if (section_header_offset > image_size ||
        section_header_offset + (uint64_t) file_header->number_of_sections * sizeof(pe_section_header_t) > image_size) {
        return false;
    }
    optional_header = (const pe_optional_header64_t *) (image + optional_header_offset);
    if (optional_header->magic != PE_OPTIONAL_MAGIC_PE32_PLUS ||
        optional_header->image_base == 0 ||
        optional_header->section_alignment == 0 ||
        optional_header->file_alignment == 0 ||
        optional_header->size_of_headers == 0 ||
        optional_header->size_of_headers > image_size ||
        optional_header->number_of_rva_and_sizes > PE_DIRECTORY_COUNT ||
        (optional_header->subsystem != EXEC_SUBSYSTEM_NATIVE &&
         optional_header->subsystem != EXEC_SUBSYSTEM_WINDOWS &&
         optional_header->subsystem != EXEC_SUBSYSTEM_CONSOLE)) {
        return false;
    }
    prepared->is_dll = (file_header->characteristics & PE_FILE_DLL) != 0;
    entry_required = !prepared->is_dll || optional_header->address_of_entry_point != 0;
    entry = optional_header->image_base + optional_header->address_of_entry_point;
    if (entry_required &&
        (entry < optional_header->image_base || !exec_range_valid(entry, 1))) {
        return false;
    }
    sections = (const pe_section_header_t *) (image + section_header_offset);
    for (uint16_t i = 0; i < file_header->number_of_sections; i++) {
        const pe_section_header_t *section = &sections[i];
        uint64_t section_start = optional_header->image_base + section->virtual_address;
        uint32_t section_size = section->virtual_size;

        if (section->size_of_raw_data > section_size) {
            section_size = section->size_of_raw_data;
        }
        if (section_size == 0 ||
            section_start < optional_header->image_base ||
            !exec_range_valid(section_start, section_size)) {
            return false;
        }
        if ((section->characteristics & PE_SECTION_MEM_EXECUTE) != 0 &&
            entry >= section_start &&
            entry - section_start < section_size) {
            entry_executable = true;
        }
    }
    if (entry_required && !entry_executable) {
        return false;
    }
    prepared->subsystem = optional_header->subsystem;
    if (optional_header->number_of_rva_and_sizes > PE_DIRECTORY_RESOURCE) {
        resource_directory = &optional_header->data_directories[PE_DIRECTORY_RESOURCE];
        if (resource_directory->virtual_address != 0 || resource_directory->size != 0) {
            if (resource_directory->virtual_address == 0 ||
                resource_directory->size < sizeof(pe_resource_directory_t) ||
                resource_directory->virtual_address + resource_directory->size < resource_directory->virtual_address) {
                return false;
            }
            prepared->resource_table_rva = resource_directory->virtual_address;
            prepared->resource_table_size = resource_directory->size;
            prepared->resource_present = true;
        }
    }
    if (optional_header->number_of_rva_and_sizes > PE_DIRECTORY_SECURITY) {
        security_directory = &optional_header->data_directories[PE_DIRECTORY_SECURITY];
        if (security_directory->virtual_address != 0 || security_directory->size != 0) {
            if (security_directory->virtual_address == 0 ||
                security_directory->size == 0 ||
                security_directory->virtual_address > image_size ||
                security_directory->virtual_address + security_directory->size < security_directory->virtual_address ||
                security_directory->virtual_address + security_directory->size > image_size) {
                return false;
            }
            prepared->certificate_table_offset = security_directory->virtual_address;
            prepared->certificate_table_size = security_directory->size;
            prepared->certificate_present = true;
        }
    }
    prepared->pe_dos_header = dos_header;
    prepared->pe_file_header = file_header;
    prepared->pe_optional_header = optional_header;
    prepared->pe_sections = sections;
    prepared->entry = entry;
    return true;
}

static bool exec_validate_prepared_image(exec_prepared_image_t *prepared)
{
    if (prepared == NULL) {
        return false;
    }
    if (prepared->format == EXEC_IMAGE_FORMAT_PE) {
        if (!exec_validate_pe_header(prepared)) {
            return false;
        }
        return exec_scan_pe_resources(prepared);
    }
    return exec_validate_elf_header(prepared);
}

static bool exec_load_elf_segments(const exec_prepared_image_t *prepared)
{
    const uint8_t *image;
    uint32_t image_size;
    const elf64_ehdr_t *header;
    const elf64_phdr_t *program_headers;

    if (prepared == NULL || prepared->image == NULL || prepared->elf_header == NULL) {
        return false;
    }
    image = prepared->image;
    image_size = prepared->image_size;
    header = prepared->elf_header;
    program_headers = (const elf64_phdr_t *) (image + header->e_phoff);

    memset((void *) EXEC_LOAD_BASE, 0, EXEC_LOAD_LIMIT - EXEC_LOAD_BASE);

    for (uint16_t i = 0; i < header->e_phnum; i++) {
        const elf64_phdr_t *program_header = &program_headers[i];

        if (program_header->p_type != ELF_PT_LOAD) {
            continue;
        }
        if (program_header->p_memsz < program_header->p_filesz) {
            return false;
        }
        if (program_header->p_offset > image_size ||
            program_header->p_filesz > image_size - program_header->p_offset) {
            return false;
        }
        if (!exec_range_valid(program_header->p_vaddr, program_header->p_memsz == 0 ? 1 : program_header->p_memsz)) {
            return false;
        }

        memset((void *) (uint64_t) program_header->p_vaddr, 0, (uint32_t) program_header->p_memsz);
        memcpy((void *) (uint64_t) program_header->p_vaddr,
               image + program_header->p_offset,
               (uint32_t) program_header->p_filesz);
    }

    return true;
}

static bool exec_load_pe_sections(const exec_prepared_image_t *prepared, bool clear_range)
{
    const uint8_t *image;
    uint32_t image_size;
    const pe_optional_header64_t *optional_header;
    const pe_section_header_t *sections;

    if (prepared == NULL ||
        prepared->image == NULL ||
        prepared->pe_file_header == NULL ||
        prepared->pe_optional_header == NULL ||
        prepared->pe_sections == NULL) {
        return false;
    }
    image = prepared->image;
    image_size = prepared->image_size;
    optional_header = prepared->pe_optional_header;
    sections = prepared->pe_sections;

    if (clear_range) {
        memset((void *) EXEC_LOAD_BASE, 0, EXEC_LOAD_LIMIT - EXEC_LOAD_BASE);
    }

    for (uint16_t i = 0; i < prepared->pe_file_header->number_of_sections; i++) {
        const pe_section_header_t *section = &sections[i];
        uint64_t target = optional_header->image_base + section->virtual_address;
        uint32_t mem_size = section->virtual_size;
        uint32_t raw_size = section->size_of_raw_data;
        uint32_t copy_size;

        if (raw_size > mem_size) {
            mem_size = raw_size;
        }
        if (mem_size == 0) {
            continue;
        }
        if (target < optional_header->image_base || !exec_range_valid(target, mem_size)) {
            return false;
        }
        if (raw_size > 0 &&
            (section->pointer_to_raw_data == 0 ||
             section->pointer_to_raw_data > image_size ||
             section->pointer_to_raw_data + raw_size > image_size ||
             section->pointer_to_raw_data + raw_size < section->pointer_to_raw_data)) {
            return false;
        }

        memset((void *) target, 0, mem_size);
        copy_size = raw_size;
        if (copy_size > mem_size) {
            copy_size = mem_size;
        }
        if (copy_size > 0) {
            memcpy((void *) target, image + section->pointer_to_raw_data, copy_size);
        }
    }

    return true;
}

static bool exec_load_prepared_image(const exec_prepared_image_t *prepared)
{
    if (prepared == NULL) {
        return false;
    }
    if (prepared->format == EXEC_IMAGE_FORMAT_PE) {
        return exec_load_pe_sections(prepared, true);
    }
    return exec_load_elf_segments(prepared);
}

static bool exec_pe_rva_to_file_offset(const exec_prepared_image_t *prepared, uint32_t rva, uint32_t size, uint32_t *offset_out)
{
    const pe_optional_header64_t *optional_header;
    const pe_section_header_t *sections;

    if (prepared == NULL ||
        prepared->image == NULL ||
        prepared->pe_file_header == NULL ||
        prepared->pe_optional_header == NULL ||
        prepared->pe_sections == NULL ||
        offset_out == NULL ||
        size == 0) {
        return false;
    }
    optional_header = prepared->pe_optional_header;
    sections = prepared->pe_sections;
    if ((uint64_t) rva + size < rva) {
        return false;
    }
    if ((uint64_t) rva + size <= optional_header->size_of_headers &&
        (uint64_t) rva + size <= prepared->image_size) {
        *offset_out = rva;
        return true;
    }
    for (uint16_t i = 0; i < prepared->pe_file_header->number_of_sections; i++) {
        const pe_section_header_t *section = &sections[i];
        uint32_t section_span = section->virtual_size;
        uint64_t section_end;
        uint64_t raw_offset;

        if (section->size_of_raw_data > section_span) {
            section_span = section->size_of_raw_data;
        }
        if (section_span == 0) {
            continue;
        }
        section_end = (uint64_t) section->virtual_address + section_span;
        if (rva < section->virtual_address ||
            (uint64_t) rva + size > section_end) {
            continue;
        }
        raw_offset = (uint64_t) rva - section->virtual_address;
        if (section->pointer_to_raw_data == 0 ||
            raw_offset + size > section->size_of_raw_data ||
            (uint64_t) section->pointer_to_raw_data + raw_offset + size > prepared->image_size) {
            return false;
        }
        *offset_out = section->pointer_to_raw_data + (uint32_t) raw_offset;
        return true;
    }
    return false;
}

static const char *exec_pe_string_at_rva(const exec_prepared_image_t *prepared, uint32_t rva)
{
    uint32_t offset;

    if (!exec_pe_rva_to_file_offset(prepared, rva, 1, &offset)) {
        return NULL;
    }
    for (uint32_t i = offset; i < prepared->image_size; i++) {
        if (prepared->image[i] == '\0') {
            return (const char *) (prepared->image + offset);
        }
    }
    return NULL;
}


static bool exec_resource_offset_in_table(const exec_prepared_image_t *prepared, uint32_t offset, uint32_t size)
{
    if (prepared == NULL || !prepared->resource_present) {
        return false;
    }
    if (offset > prepared->resource_table_size) {
        return false;
    }
    if (size > prepared->resource_table_size - offset) {
        return false;
    }
    return true;
}

static bool exec_validate_pe_resource_name(const exec_prepared_image_t *prepared, uint32_t name)
{
    uint32_t name_offset;
    uint32_t file_offset;
    uint16_t length;
    uint32_t bytes;

    if ((name & PE_RESOURCE_ENTRY_NAME_IS_STRING) == 0) {
        return true;
    }
    name_offset = name & PE_RESOURCE_OFFSET_MASK;
    if (!exec_resource_offset_in_table(prepared, name_offset, sizeof(uint16_t))) {
        return false;
    }
    if (!exec_pe_rva_to_file_offset(prepared, prepared->resource_table_rva + name_offset, sizeof(uint16_t), &file_offset)) {
        return false;
    }
    length = exec_read_u16(prepared->image + file_offset);
    bytes = sizeof(uint16_t) + (uint32_t) length * sizeof(uint16_t);
    if (bytes < sizeof(uint16_t)) {
        return false;
    }
    return exec_resource_offset_in_table(prepared, name_offset, bytes);
}

static bool exec_mark_pe_resource_type(exec_prepared_image_t *prepared, uint32_t type_id)
{
    if (prepared == NULL) {
        return false;
    }
    if (type_id == PE_RESOURCE_ID_ICON || type_id == PE_RESOURCE_ID_GROUP_ICON) {
        prepared->icon_resource_present = true;
    } else if (type_id == PE_RESOURCE_ID_MANIFEST) {
        prepared->manifest_resource_present = true;
    } else if (type_id == PE_RESOURCE_ID_VERSION) {
        prepared->version_resource_present = true;
    }
    return true;
}

static char exec_ascii_lower(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char) (ch - 'A' + 'a');
    }
    return ch;
}

static bool exec_bytes_contain_ascii_casefold(const uint8_t *data, uint32_t size, const char *needle)
{
    uint32_t needle_len;

    if (data == NULL || needle == NULL) {
        return false;
    }
    needle_len = (uint32_t) strlen(needle);
    if (needle_len == 0 || size < needle_len) {
        return false;
    }
    for (uint32_t offset = 0; offset + needle_len <= size; offset++) {
        bool matched = true;

        for (uint32_t i = 0; i < needle_len; i++) {
            if (exec_ascii_lower((char) data[offset + i]) != exec_ascii_lower(needle[i])) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

static bool exec_bytes_contain_utf16le_ascii_casefold(const uint8_t *data, uint32_t size, const char *needle)
{
    uint32_t needle_len;

    if (data == NULL || needle == NULL) {
        return false;
    }
    needle_len = (uint32_t) strlen(needle);
    if (needle_len == 0 || size < needle_len * 2U) {
        return false;
    }
    for (uint32_t offset = 0; offset + needle_len * 2U <= size; offset += 2U) {
        bool matched = true;

        for (uint32_t i = 0; i < needle_len; i++) {
            if (data[offset + i * 2U + 1U] != 0 ||
                exec_ascii_lower((char) data[offset + i * 2U]) != exec_ascii_lower(needle[i])) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

static void exec_scan_pe_manifest_resource(exec_prepared_image_t *prepared, uint32_t data_file_offset, uint32_t data_size)
{
    const uint8_t *data;

    if (prepared == NULL || data_size == 0 || data_file_offset > prepared->image_size ||
        data_size > prepared->image_size - data_file_offset) {
        return;
    }
    data = prepared->image + data_file_offset;
    if (exec_bytes_contain_ascii_casefold(data, data_size, "requireAdministrator") ||
        exec_bytes_contain_ascii_casefold(data, data_size, "highestAvailable") ||
        exec_bytes_contain_utf16le_ascii_casefold(data, data_size, "requireAdministrator") ||
        exec_bytes_contain_utf16le_ascii_casefold(data, data_size, "highestAvailable")) {
        prepared->elevated_manifest_present = true;
    }
}

static bool exec_scan_pe_resource_directory(exec_prepared_image_t *prepared,
                                           uint32_t directory_rva,
                                           uint32_t depth,
                                           uint32_t type_id)
{
    uint32_t directory_offset;
    uint32_t relative_offset;
    uint32_t entry_count;
    uint32_t entries_size;
    const pe_resource_directory_t *directory;
    const pe_resource_directory_entry_t *entries;

    if (prepared == NULL || depth >= PE_RESOURCE_MAX_DEPTH) {
        return false;
    }
    if (directory_rva < prepared->resource_table_rva) {
        return false;
    }
    relative_offset = directory_rva - prepared->resource_table_rva;
    if (!exec_resource_offset_in_table(prepared, relative_offset, sizeof(pe_resource_directory_t))) {
        return false;
    }
    if (!exec_pe_rva_to_file_offset(prepared, directory_rva, sizeof(pe_resource_directory_t), &directory_offset)) {
        return false;
    }
    directory = (const pe_resource_directory_t *) (prepared->image + directory_offset);
    entry_count = (uint32_t) directory->number_of_named_entries + (uint32_t) directory->number_of_id_entries;
    if (entry_count > PE_RESOURCE_MAX_ENTRIES || entry_count > 0xFFFFFFFFU / sizeof(pe_resource_directory_entry_t)) {
        return false;
    }
    entries_size = entry_count * sizeof(pe_resource_directory_entry_t);
    if (!exec_resource_offset_in_table(prepared, relative_offset + sizeof(pe_resource_directory_t), entries_size)) {
        return false;
    }
    entries = (const pe_resource_directory_entry_t *) (prepared->image + directory_offset + sizeof(pe_resource_directory_t));

    for (uint32_t i = 0; i < entry_count; i++) {
        uint32_t child_type = type_id;

        if (!exec_validate_pe_resource_name(prepared, entries[i].name)) {
            return false;
        }
        if (depth == 0 && (entries[i].name & PE_RESOURCE_ENTRY_NAME_IS_STRING) == 0) {
            child_type = entries[i].name & 0xFFFFU;
        }
        if ((entries[i].offset_to_data & PE_RESOURCE_ENTRY_IS_DIRECTORY) != 0) {
            uint32_t child_offset = entries[i].offset_to_data & PE_RESOURCE_OFFSET_MASK;

            if (!exec_resource_offset_in_table(prepared, child_offset, sizeof(pe_resource_directory_t))) {
                return false;
            }
            if (!exec_scan_pe_resource_directory(prepared,
                                                 prepared->resource_table_rva + child_offset,
                                                 depth + 1,
                                                 child_type)) {
                return false;
            }
        } else {
            uint32_t data_entry_offset = entries[i].offset_to_data & PE_RESOURCE_OFFSET_MASK;
            uint32_t data_entry_file_offset;
            const pe_resource_data_entry_t *data_entry;

            if (!exec_resource_offset_in_table(prepared, data_entry_offset, sizeof(pe_resource_data_entry_t))) {
                return false;
            }
            if (!exec_pe_rva_to_file_offset(prepared,
                                            prepared->resource_table_rva + data_entry_offset,
                                            sizeof(pe_resource_data_entry_t),
                                            &data_entry_file_offset)) {
                return false;
            }
            data_entry = (const pe_resource_data_entry_t *) (prepared->image + data_entry_file_offset);
            if (data_entry->size > 0) {
                uint32_t data_file_offset;

                if (!exec_pe_rva_to_file_offset(prepared, data_entry->data_rva, data_entry->size, &data_file_offset)) {
                    return false;
                }
                if (child_type == PE_RESOURCE_ID_MANIFEST) {
                    exec_scan_pe_manifest_resource(prepared, data_file_offset, data_entry->size);
                }
            }
            exec_mark_pe_resource_type(prepared, child_type);
        }
    }
    return true;
}

static bool exec_scan_pe_resources(exec_prepared_image_t *prepared)
{
    if (prepared == NULL || !prepared->resource_present) {
        return true;
    }
    prepared->icon_resource_present = false;
    prepared->manifest_resource_present = false;
    prepared->elevated_manifest_present = false;
    prepared->version_resource_present = false;
    return exec_scan_pe_resource_directory(prepared, prepared->resource_table_rva, 0, 0);
}

static bool exec_find_pe_resource_data(const exec_prepared_image_t *prepared,
                                       uint32_t directory_rva,
                                       uint32_t depth,
                                       uint32_t type_id,
                                       uint32_t resource_id,
                                       uint32_t wanted_type,
                                       uint32_t wanted_id,
                                       uint32_t *data_file_offset,
                                       uint32_t *data_size)
{
    uint32_t directory_offset;
    uint32_t relative_offset;
    uint32_t entry_count;
    uint32_t entries_size;
    const pe_resource_directory_t *directory;
    const pe_resource_directory_entry_t *entries;

    if (prepared == NULL || !prepared->resource_present ||
        depth >= PE_RESOURCE_MAX_DEPTH ||
        data_file_offset == NULL || data_size == NULL ||
        directory_rva < prepared->resource_table_rva) {
        return false;
    }
    relative_offset = directory_rva - prepared->resource_table_rva;
    if (!exec_resource_offset_in_table(prepared, relative_offset, sizeof(pe_resource_directory_t)) ||
        !exec_pe_rva_to_file_offset(prepared,
                                    directory_rva,
                                    sizeof(pe_resource_directory_t),
                                    &directory_offset)) {
        return false;
    }
    directory = (const pe_resource_directory_t *) (prepared->image + directory_offset);
    entry_count = (uint32_t) directory->number_of_named_entries +
                  (uint32_t) directory->number_of_id_entries;
    if (entry_count > PE_RESOURCE_MAX_ENTRIES ||
        entry_count > 0xFFFFFFFFU / sizeof(pe_resource_directory_entry_t)) {
        return false;
    }
    entries_size = entry_count * sizeof(pe_resource_directory_entry_t);
    if (!exec_resource_offset_in_table(prepared,
                                       relative_offset + sizeof(pe_resource_directory_t),
                                       entries_size)) {
        return false;
    }
    entries = (const pe_resource_directory_entry_t *)
        (prepared->image + directory_offset + sizeof(pe_resource_directory_t));

    for (uint32_t i = 0; i < entry_count; i++) {
        uint32_t child_type = type_id;
        uint32_t child_resource_id = resource_id;

        if (!exec_validate_pe_resource_name(prepared, entries[i].name)) {
            return false;
        }
        if ((entries[i].name & PE_RESOURCE_ENTRY_NAME_IS_STRING) == 0) {
            if (depth == 0) {
                child_type = entries[i].name & 0xFFFFU;
            } else if (depth == 1) {
                child_resource_id = entries[i].name & 0xFFFFU;
            }
        } else if (depth == 1) {
            child_resource_id = PE_RESOURCE_ANY_ID;
        }

        if ((entries[i].offset_to_data & PE_RESOURCE_ENTRY_IS_DIRECTORY) != 0) {
            uint32_t child_offset = entries[i].offset_to_data & PE_RESOURCE_OFFSET_MASK;

            if (!exec_resource_offset_in_table(prepared,
                                               child_offset,
                                               sizeof(pe_resource_directory_t)) ||
                !exec_find_pe_resource_data(prepared,
                                             prepared->resource_table_rva + child_offset,
                                             depth + 1,
                                             child_type,
                                             child_resource_id,
                                             wanted_type,
                                             wanted_id,
                                             data_file_offset,
                                             data_size)) {
                if (!exec_resource_offset_in_table(prepared,
                                                   child_offset,
                                                   sizeof(pe_resource_directory_t))) {
                    return false;
                }
                continue;
            }
            return true;
        }

        if (child_type == wanted_type &&
            (wanted_id == PE_RESOURCE_ANY_ID || child_resource_id == wanted_id)) {
            uint32_t data_entry_offset = entries[i].offset_to_data & PE_RESOURCE_OFFSET_MASK;
            uint32_t data_entry_file_offset;
            const pe_resource_data_entry_t *data_entry;

            if (!exec_resource_offset_in_table(prepared,
                                               data_entry_offset,
                                               sizeof(pe_resource_data_entry_t)) ||
                !exec_pe_rva_to_file_offset(prepared,
                                            prepared->resource_table_rva + data_entry_offset,
                                            sizeof(pe_resource_data_entry_t),
                                            &data_entry_file_offset)) {
                return false;
            }
            data_entry = (const pe_resource_data_entry_t *)
                (prepared->image + data_entry_file_offset);
            if (data_entry->size == 0 ||
                !exec_pe_rva_to_file_offset(prepared,
                                            data_entry->data_rva,
                                            data_entry->size,
                                            data_file_offset)) {
                return false;
            }
            *data_size = data_entry->size;
            return true;
        }
    }
    return false;
}

static uint32_t exec_icon_read_pixel(const uint8_t *data,
                                     uint32_t data_size,
                                     uint32_t dib_size,
                                     uint32_t width,
                                     uint32_t height,
                                     uint16_t bit_count,
                                     uint32_t palette_offset,
                                     uint32_t xor_offset,
                                     uint32_t xor_stride,
                                     uint32_t mask_offset,
                                     uint32_t mask_stride,
                                     uint32_t x,
                                     uint32_t y)
{
    uint32_t row;
    uint32_t offset;
    uint8_t alpha = 0xFF;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint32_t palette_entries;

    (void) dib_size;
    if (data == NULL || width == 0 || height == 0 || x >= width || y >= height ||
        xor_stride == 0 || xor_offset > data_size ||
        (uint64_t) xor_offset + (uint64_t) xor_stride * height > data_size) {
        return 0;
    }
    row = height - 1U - y;
    offset = xor_offset + row * xor_stride;

    if (bit_count == 32) {
        uint32_t pixel_offset = offset + x * 4U;
        if (pixel_offset + 4U > data_size) {
            return 0;
        }
        blue = data[pixel_offset];
        green = data[pixel_offset + 1U];
        red = data[pixel_offset + 2U];
        alpha = data[pixel_offset + 3U];
    } else if (bit_count == 24) {
        uint32_t pixel_offset = offset + x * 3U;
        if (pixel_offset + 3U > data_size) {
            return 0;
        }
        blue = data[pixel_offset];
        green = data[pixel_offset + 1U];
        red = data[pixel_offset + 2U];
    } else if (bit_count == 8 || bit_count == 4 || bit_count == 1) {
        /* bits_per_row already folded into caller-supplied offset; unused */
        uint32_t palette_index;
        uint32_t palette_entry_offset;
        uint8_t packed;

        if (bit_count == 8) {
            palette_index = data[offset + x];
        } else if (bit_count == 4) {
            if (offset + (x / 2U) >= data_size) {
                return 0;
            }
            packed = data[offset + x / 2U];
            palette_index = (x & 1U) == 0 ? (packed >> 4) : (packed & 0x0FU);
        } else {
            if (offset + (x / 8U) >= data_size) {
                return 0;
            }
            packed = data[offset + x / 8U];
            palette_index = (packed >> (7U - (x & 7U))) & 1U;
        }
        palette_entries = 1U << bit_count;
        palette_entry_offset = (palette_index < palette_entries ? palette_index : 0U) * 4U;
        if (palette_offset > data_size ||
            palette_entry_offset + 4U > data_size - palette_offset) {
            return 0;
        }
        blue = data[palette_offset + palette_entry_offset];
        green = data[palette_offset + palette_entry_offset + 1U];
        red = data[palette_offset + palette_entry_offset + 2U];
    } else {
        return 0;
    }

    if (mask_offset != 0 && mask_stride != 0 &&
        mask_offset <= data_size &&
        (uint64_t) mask_offset + (uint64_t) mask_stride * height <= data_size) {
        uint32_t mask_byte = mask_offset + row * mask_stride + x / 8U;
        if (mask_byte < data_size && (data[mask_byte] & (uint8_t) (0x80U >> (x & 7U))) != 0) {
            alpha = 0;
        }
    }
    return ((uint32_t) alpha << 24) |
           ((uint32_t) red << 16) |
           ((uint32_t) green << 8) |
           blue;
}

static bool exec_decode_icon_dib(const uint8_t *data,
                                 uint32_t data_size,
                                 uint32_t *pixels,
                                 uint16_t output_width,
                                 uint16_t output_height)
{
    uint32_t dib_size;
    int32_t dib_width;
    int32_t dib_height;
    uint16_t planes;
    uint16_t bit_count;
    uint32_t palette_entries = 0;
    uint32_t palette_offset;
    uint32_t xor_offset;
    uint32_t xor_stride;
    uint32_t mask_offset;
    uint32_t mask_stride;
    uint32_t source_width;
    uint32_t source_height;

    if (data == NULL || pixels == NULL || output_width == 0 || output_height == 0 ||
        data_size < 40U) {
        return false;
    }
    dib_size = exec_read_u32(data);
    dib_width = (int32_t) exec_read_u32(data + 4U);
    dib_height = (int32_t) exec_read_u32(data + 8U);
    planes = exec_read_u16(data + 12U);
    bit_count = exec_read_u16(data + 14U);
    if (dib_size < 40U || dib_size > data_size ||
        dib_width <= 0 || dib_height == 0 || planes == 0 ||
        (bit_count != 32 && bit_count != 24 && bit_count != 8 &&
         bit_count != 4 && bit_count != 1)) {
        return false;
    }
    source_width = (uint32_t) dib_width;
    source_height = (uint32_t) (dib_height < 0 ? -dib_height : dib_height) / 2U;
    if (source_width == 0 || source_height == 0 || source_width > 1024U ||
        source_height > 1024U) {
        return false;
    }
    if (bit_count <= 8) {
        palette_entries = exec_read_u32(data + 32U);
        if (palette_entries == 0) {
            palette_entries = 1U << bit_count;
        }
        if (palette_entries > (1U << bit_count)) {
            return false;
        }
    }
    palette_offset = dib_size;
    if ((uint64_t) palette_offset + (uint64_t) palette_entries * 4U > data_size) {
        return false;
    }
    xor_offset = palette_offset + palette_entries * 4U;
    xor_stride = ((source_width * bit_count + 31U) / 32U) * 4U;
    mask_stride = ((source_width + 31U) / 32U) * 4U;
    if (xor_stride == 0 ||
        (uint64_t) xor_offset + (uint64_t) xor_stride * source_height > data_size) {
        return false;
    }
    mask_offset = xor_offset + xor_stride * source_height;
    if ((uint64_t) mask_offset + (uint64_t) mask_stride * source_height > data_size) {
        mask_offset = 0;
    }
    for (uint32_t y = 0; y < output_height; y++) {
        uint32_t source_y = ((uint64_t) y * source_height) / output_height;
        for (uint32_t x = 0; x < output_width; x++) {
            uint32_t source_x = ((uint64_t) x * source_width) / output_width;
            pixels[y * output_width + x] =
                exec_icon_read_pixel(data,
                                      data_size,
                                      dib_size,
                                      source_width,
                                      source_height,
                                      bit_count,
                                      palette_offset,
                                      xor_offset,
                                      xor_stride,
                                      mask_offset,
                                      mask_stride,
                                      source_x,
                                      source_y);
        }
    }
    return true;
}

bool exec_extract_icon_bitmap(const char *path,
                              uint32_t *pixels,
                              uint16_t width,
                              uint16_t height)
{
    char resolved_path[PATH_MAX_LEN];
    exec_prepared_image_t prepared;
    uint32_t image_flags;
    uint32_t group_offset;
    uint32_t group_size;
    uint32_t icon_offset;
    uint32_t icon_size;
    uint8_t *image;
    int32_t image_size;
    const uint8_t *group;
    uint16_t icon_count;
    uint32_t selected_id = PE_RESOURCE_ANY_ID;
    uint32_t selected_distance = 0xFFFFFFFFU;
    uint32_t target_size;

    if (pixels == NULL || width == 0 || height == 0 ||
        path == NULL || path[0] == '\0' ||
        !path_resolve(exec_current_cwd(), path, resolved_path, sizeof(resolved_path)) ||
        !file_exists(resolved_path) || file_is_dir(resolved_path)) {
        return false;
    }
    image_size = file_size(resolved_path);
    if (image_size <= 0) {
        return false;
    }
    image = (uint8_t *) kmalloc((uint32_t) image_size);
    if (image == NULL) {
        return false;
    }
    if (file_read(resolved_path, image, (uint32_t) image_size) != image_size ||
        !exec_prepare_image(image,
                            (uint32_t) image_size,
                            resolved_path,
                            &prepared,
                            &image_flags) ||
        prepared.format != EXEC_IMAGE_FORMAT_PE ||
        !prepared.resource_present ||
        !prepared.icon_resource_present ||
        !exec_validate_prepared_image(&prepared) ||
        !exec_find_pe_resource_data(&prepared,
                                    prepared.resource_table_rva,
                                    0,
                                    0,
                                    PE_RESOURCE_ANY_ID,
                                    PE_RESOURCE_ID_GROUP_ICON,
                                    PE_RESOURCE_ANY_ID,
                                    &group_offset,
                                    &group_size) ||
        group_size < 6U) {
        kfree(image);
        return false;
    }

    group = image + group_offset;
    icon_count = exec_read_u16(group + 4U);
    target_size = width > height ? width : height;
    if (target_size == 0) {
        target_size = 32;
    }
    for (uint32_t i = 0; i < icon_count; i++) {
        uint32_t entry_offset = 6U + i * 14U;
        uint32_t icon_width;
        uint32_t icon_height;
        uint32_t distance;

        if (entry_offset + 14U > group_size) {
            break;
        }
        icon_width = group[entry_offset] == 0 ? 256U : group[entry_offset];
        icon_height = group[entry_offset + 1U] == 0 ? 256U : group[entry_offset + 1U];
        distance = (icon_width > target_size ? icon_width - target_size : target_size - icon_width) +
                   (icon_height > target_size ? icon_height - target_size : target_size - icon_height);
        if (selected_id == PE_RESOURCE_ANY_ID || distance < selected_distance) {
            selected_distance = distance;
            selected_id = exec_read_u16(group + entry_offset + 12U);
        }
    }
    if (selected_id == PE_RESOURCE_ANY_ID ||
        !exec_find_pe_resource_data(&prepared,
                                    prepared.resource_table_rva,
                                    0,
                                    0,
                                    PE_RESOURCE_ANY_ID,
                                    PE_RESOURCE_ID_ICON,
                                    selected_id,
                                    &icon_offset,
                                    &icon_size) ||
        !exec_decode_icon_dib(image + icon_offset, icon_size, pixels, width, height)) {
        kfree(image);
        return false;
    }
    kfree(image);
    return true;
}

static const char *exec_canonical_dll_name(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    if (strcasecmp(name, "monios.dll") == 0) {
        return "monios.dll";
    }
    if (strcasecmp(name, "console.dll") == 0) {
        return "console.dll";
    }
    if (strcasecmp(name, "windows.dll") == 0) {
        return "windows.dll";
    }
    if (strcasecmp(name, "osui.dll") == 0) {
        return "osui.dll";
    }
    return NULL;
}

static bool exec_build_dll_path(const char *name, char *path, uint32_t path_size)
{
    const char *canonical;

    if (path == NULL || path_size == 0) {
        return false;
    }
    canonical = exec_canonical_dll_name(name);
    if (canonical == NULL) {
        return false;
    }
    if (strlen(UI_SYSTEM_LIB_DIR) + 1 + strlen(canonical) + 1 <= path_size) {
        strlcpy(path, UI_SYSTEM_LIB_DIR, path_size);
        strlcpy(path + strlen(path), PATH_SEPARATOR_STR, path_size - strlen(path));
        strlcpy(path + strlen(path), canonical, path_size - strlen(path));
        if (file_exists(path) && !file_is_dir(path)) {
            return true;
        }
    }
    if (strlen(PATH_ROOT) + strlen(canonical) + 1 > path_size) {
        return false;
    }
    strlcpy(path, PATH_ROOT, path_size);
    strlcpy(path + strlen(path), canonical, path_size - strlen(path));
    return true;
}

static exec_loaded_dll_t *exec_find_loaded_dll(exec_loaded_dll_t dlls[], uint32_t dll_count, const char *name)
{
    const char *canonical = exec_canonical_dll_name(name);

    if (dlls == NULL || canonical == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < dll_count; i++) {
        if (dlls[i].loaded && strcasecmp(dlls[i].name, canonical) == 0) {
            return &dlls[i];
        }
    }
    return NULL;
}

static bool exec_import_descriptor_empty(const pe_import_descriptor_t *descriptor)
{
    return descriptor->original_first_thunk == 0 &&
           descriptor->time_date_stamp == 0 &&
           descriptor->forwarder_chain == 0 &&
           descriptor->name == 0 &&
           descriptor->first_thunk == 0;
}

static uint64_t exec_resolve_export(const exec_loaded_dll_t *dll, const char *name)
{
    const exec_prepared_image_t *prepared;
    const pe_data_directory_t *directory;
    const pe_export_directory_t *exports;
    uint32_t export_offset;
    uint32_t functions_offset;
    uint32_t names_offset;
    uint32_t ordinals_offset;

    if (dll == NULL || !dll->loaded || name == NULL) {
        return 0;
    }
    prepared = &dll->prepared;
    if (prepared->pe_optional_header == NULL ||
        prepared->pe_optional_header->number_of_rva_and_sizes <= PE_DIRECTORY_EXPORT) {
        return 0;
    }
    directory = &prepared->pe_optional_header->data_directories[PE_DIRECTORY_EXPORT];
    if (directory->virtual_address == 0 || directory->size < sizeof(pe_export_directory_t)) {
        return 0;
    }
    if (!exec_pe_rva_to_file_offset(prepared, directory->virtual_address, sizeof(pe_export_directory_t), &export_offset)) {
        return 0;
    }
    exports = (const pe_export_directory_t *) (prepared->image + export_offset);
    if (exports->number_of_functions == 0 ||
        exports->number_of_names == 0 ||
        exports->address_of_functions == 0 ||
        exports->address_of_names == 0 ||
        exports->address_of_name_ordinals == 0) {
        return 0;
    }
    if (exports->number_of_functions > 0xFFFFFFFFU / sizeof(uint32_t) ||
        exports->number_of_names > 0xFFFFFFFFU / sizeof(uint32_t) ||
        exports->number_of_names > 0xFFFFFFFFU / sizeof(uint16_t)) {
        return 0;
    }
    if (!exec_pe_rva_to_file_offset(prepared, exports->address_of_functions, exports->number_of_functions * sizeof(uint32_t), &functions_offset) ||
        !exec_pe_rva_to_file_offset(prepared, exports->address_of_names, exports->number_of_names * sizeof(uint32_t), &names_offset) ||
        !exec_pe_rva_to_file_offset(prepared, exports->address_of_name_ordinals, exports->number_of_names * sizeof(uint16_t), &ordinals_offset)) {
        return 0;
    }

    for (uint32_t i = 0; i < exports->number_of_names; i++) {
        uint32_t name_rva = exec_read_u32(prepared->image + names_offset + i * sizeof(uint32_t));
        const char *export_name = exec_pe_string_at_rva(prepared, name_rva);
        uint16_t ordinal_index;
        uint32_t function_rva;
        uint64_t export_end;

        if (export_name == NULL || strcmp(export_name, name) != 0) {
            continue;
        }
        ordinal_index = exec_read_u16(prepared->image + ordinals_offset + i * sizeof(uint16_t));
        if (ordinal_index >= exports->number_of_functions) {
            return 0;
        }
        function_rva = exec_read_u32(prepared->image + functions_offset + ordinal_index * sizeof(uint32_t));
        if (function_rva == 0) {
            return 0;
        }
        export_end = (uint64_t) directory->virtual_address + directory->size;
        if (function_rva >= directory->virtual_address && function_rva < export_end) {
            return 0;
        }
        return prepared->pe_optional_header->image_base + function_rva;
    }
    return 0;
}

static bool exec_bind_imports_for_image(const exec_prepared_image_t *prepared, exec_loaded_dll_t dlls[], uint32_t *dll_count);

static exec_loaded_dll_t *exec_load_dll(const char *name, exec_loaded_dll_t dlls[], uint32_t *dll_count)
{
    exec_loaded_dll_t *existing;
    exec_loaded_dll_t *dll;
    const char *canonical;
    int32_t image_size;
    uint32_t image_flags;

    if (dlls == NULL || dll_count == NULL) {
        return NULL;
    }
    canonical = exec_canonical_dll_name(name);
    if (canonical == NULL) {
        return NULL;
    }
    existing = exec_find_loaded_dll(dlls, *dll_count, canonical);
    if (existing != NULL) {
        return existing;
    }
    if (*dll_count >= EXEC_DLL_MAX) {
        return NULL;
    }
    dll = &dlls[*dll_count];
    memset(dll, 0, sizeof(*dll));
    strlcpy(dll->name, canonical, sizeof(dll->name));
    if (!exec_build_dll_path(canonical, dll->path, sizeof(dll->path))) {
        return NULL;
    }
    image_size = file_size(dll->path);
    if (image_size <= 0 || file_is_dir(dll->path)) {
        return NULL;
    }
    dll->image = (uint8_t *) kmalloc((uint32_t) image_size);
    if (dll->image == NULL) {
        return NULL;
    }
    dll->image_size = (uint32_t) image_size;
    if (file_read(dll->path, dll->image, dll->image_size) != image_size) {
        kfree(dll->image);
        memset(dll, 0, sizeof(*dll));
        return NULL;
    }
    if (!exec_prepare_image(dll->image, dll->image_size, dll->path, &dll->prepared, &image_flags) ||
        !exec_validate_prepared_image(&dll->prepared) ||
        dll->prepared.format != EXEC_IMAGE_FORMAT_PE ||
        !dll->prepared.is_dll ||
        !exec_load_pe_sections(&dll->prepared, false)) {
        kfree(dll->image);
        memset(dll, 0, sizeof(*dll));
        return NULL;
    }
    dll->loaded = true;
    (*dll_count)++;
    if (!exec_bind_imports_for_image(&dll->prepared, dlls, dll_count)) {
        return NULL;
    }
    return dll;
}

static bool exec_bind_imports_for_image(const exec_prepared_image_t *prepared, exec_loaded_dll_t dlls[], uint32_t *dll_count)
{
    const pe_data_directory_t *directory;
    uint32_t descriptor_limit;
    bool terminated = false;

    if (prepared == NULL ||
        prepared->format != EXEC_IMAGE_FORMAT_PE ||
        prepared->pe_optional_header == NULL ||
        prepared->pe_optional_header->number_of_rva_and_sizes <= PE_DIRECTORY_IMPORT) {
        return true;
    }
    directory = &prepared->pe_optional_header->data_directories[PE_DIRECTORY_IMPORT];
    if (directory->virtual_address == 0 || directory->size == 0) {
        return true;
    }
    if (directory->size < sizeof(pe_import_descriptor_t)) {
        return false;
    }
    descriptor_limit = directory->size / sizeof(pe_import_descriptor_t);
    for (uint32_t descriptor_index = 0; descriptor_index < descriptor_limit; descriptor_index++) {
        pe_import_descriptor_t descriptor;
        uint32_t descriptor_offset;
        uint64_t descriptor_rva;
        const char *dll_name;
        exec_loaded_dll_t *dll;
        uint32_t thunk_rva;
        uint32_t first_thunk;

        descriptor_rva = (uint64_t) directory->virtual_address +
                         (uint64_t) descriptor_index * sizeof(pe_import_descriptor_t);
        if (descriptor_rva > 0xFFFFFFFFULL ||
            !exec_pe_rva_to_file_offset(prepared,
                                        (uint32_t) descriptor_rva,
                                        sizeof(pe_import_descriptor_t),
                                        &descriptor_offset)) {
            return false;
        }
        memcpy(&descriptor, prepared->image + descriptor_offset, sizeof(descriptor));
        if (exec_import_descriptor_empty(&descriptor)) {
            terminated = true;
            break;
        }
        if (descriptor.name == 0 || descriptor.first_thunk == 0) {
            return false;
        }
        dll_name = exec_pe_string_at_rva(prepared, descriptor.name);
        if (dll_name == NULL || exec_canonical_dll_name(dll_name) == NULL) {
            return false;
        }
        dll = exec_load_dll(dll_name, dlls, dll_count);
        if (dll == NULL) {
            return false;
        }
        thunk_rva = descriptor.original_first_thunk != 0 ? descriptor.original_first_thunk : descriptor.first_thunk;
        first_thunk = descriptor.first_thunk;
        for (uint32_t thunk_index = 0; thunk_index < 4096; thunk_index++) {
            uint32_t thunk_offset;
            uint64_t lookup_rva;
            uint64_t thunk_value;
            uint64_t iat_address;
            uint32_t import_name_rva;
            const char *function_name;
            uint64_t resolved;

            lookup_rva = (uint64_t) thunk_rva + (uint64_t) thunk_index * sizeof(uint64_t);
            if (lookup_rva > 0xFFFFFFFFULL ||
                !exec_pe_rva_to_file_offset(prepared,
                                            (uint32_t) lookup_rva,
                                            sizeof(uint64_t),
                                            &thunk_offset)) {
                return false;
            }
            thunk_value = exec_read_u64(prepared->image + thunk_offset);
            if (thunk_value == 0) {
                break;
            }
            if ((thunk_value & PE_IMPORT_ORDINAL_FLAG64) != 0) {
                return false;
            }
            import_name_rva = (uint32_t) thunk_value;
            if (import_name_rva == 0) {
                return false;
            }
            function_name = exec_pe_string_at_rva(prepared, import_name_rva + sizeof(uint16_t));
            if (function_name == NULL) {
                return false;
            }
            resolved = exec_resolve_export(dll, function_name);
            if (resolved == 0 || !exec_range_valid(resolved, 1)) {
                return false;
            }
            iat_address = prepared->pe_optional_header->image_base + first_thunk + thunk_index * sizeof(uint64_t);
            if (iat_address < prepared->pe_optional_header->image_base || !exec_range_valid(iat_address, sizeof(uint64_t))) {
                return false;
            }
            exec_write_u64((uint8_t *) (uintptr_t) iat_address, resolved);
        }
    }
    return terminated;
}

static void exec_free_loaded_dlls(exec_loaded_dll_t dlls[], uint32_t dll_count)
{
    if (dlls == NULL) {
        return;
    }
    for (uint32_t i = 0; i < dll_count; i++) {
        if (dlls[i].image != NULL) {
            kfree(dlls[i].image);
            dlls[i].image = NULL;
        }
        dlls[i].loaded = false;
    }
}

static bool exec_probe_image_flags_for_path(const char *resolved_path, uint32_t *flags_out)
{
    uint8_t *image;
    int32_t image_size;
    exec_prepared_image_t prepared;
    uint32_t image_flags;
    bool ok;

    if (resolved_path == NULL || flags_out == NULL) {
        return false;
    }
    image_size = file_size(resolved_path);
    if (image_size <= 0 || file_is_dir(resolved_path)) {
        return false;
    }
    image = (uint8_t *) kmalloc((uint32_t) image_size);
    if (image == NULL) {
        return false;
    }
    if (file_read(resolved_path, image, (uint32_t) image_size) != image_size) {
        kfree(image);
        return false;
    }
    ok = exec_prepare_image(image, (uint32_t) image_size, resolved_path, &prepared, &image_flags) &&
         exec_validate_prepared_image(&prepared);
    if (ok) {
        *flags_out = exec_flags_for_prepared_image(&prepared, resolved_path, image_flags);
    }
    kfree(image);
    return ok;
}

bool exec_resolve_path(const char *path, char *output, uint32_t output_size)
{
    return path_resolve(exec_current_cwd(), path, output, output_size);
}

uint32_t exec_image_flags_for_path(const char *path)
{
    char resolved_path[PATH_MAX_LEN];
    uint32_t image_flags;

    if (path == NULL || path[0] == '\0') {
        return EXEC_IMAGE_FLAG_CONSOLE;
    }
    if (path_resolve(exec_current_cwd(), path, resolved_path, sizeof(resolved_path))) {
        if (exec_probe_image_flags_for_path(resolved_path, &image_flags)) {
            return image_flags;
        }
        return exec_default_flags_for_path(resolved_path);
    }
    return EXEC_IMAGE_FLAG_CONSOLE;
}

bool exec_signature_status_for_path(const char *path,
                                    bool *signed_present,
                                    bool *signature_valid,
                                    bool *publisher_trusted)
{
    char resolved_path[PATH_MAX_LEN];
    exec_prepared_image_t prepared;
    uint32_t image_flags;
    int32_t image_size;
    uint8_t *image;
    bool signature_valid_local;
    uint8_t signer_id[MONIOS_SIGNER_ID_SIZE];

    if (signed_present != NULL) {
        *signed_present = false;
    }
    if (signature_valid != NULL) {
        *signature_valid = false;
    }
    if (publisher_trusted != NULL) {
        *publisher_trusted = false;
    }
    if (path == NULL || path[0] == '\0' ||
        !path_resolve(exec_current_cwd(), path, resolved_path, sizeof(resolved_path)) ||
        !file_exists(resolved_path) || file_is_dir(resolved_path)) {
        return false;
    }
    image_size = file_size(resolved_path);
    if (image_size <= 0) {
        return false;
    }
    image = (uint8_t *) kmalloc((uint32_t) image_size);
    if (image == NULL) {
        return false;
    }
    if (file_read(resolved_path, image, (uint32_t) image_size) != image_size) {
        kfree(image);
        return false;
    }
    if (!exec_prepare_image(image,
                            (uint32_t) image_size,
                            resolved_path,
                            &prepared,
                            &image_flags) ||
        !exec_validate_prepared_image(&prepared)) {
        kfree(image);
        return false;
    }
    if (signed_present != NULL) {
        *signed_present = prepared.certificate_present;
    }
    signature_valid_local = authenticode_verify_pe(image,
                                                   (uint32_t) image_size,
                                                   signer_id);
    if (signature_valid != NULL) {
        *signature_valid = signature_valid_local;
    }
    if (publisher_trusted != NULL) {
        *publisher_trusted = signature_valid_local;
    }
    kfree(image);
    return true;
}

const char *exec_current_cwd(void)
{
    if (g_exec_context != NULL && g_exec_context->cwd[0] != '\0') {
        return g_exec_context->cwd;
    }
    return PATH_ROOT;
}

bool exec_active(void)
{
    return g_exec_context != NULL;
}

bool exec_address_in_active_image(uint64_t address)
{
    return g_exec_context != NULL && address >= EXEC_LOAD_BASE && address < EXEC_LOAD_LIMIT;
}

bool exec_user_range_valid(const void *ptr, uint64_t size)
{
    uint64_t start = (uint64_t) ptr;
    uint64_t end;

    if (size == 0) {
        return true;
    }
    if (ptr == NULL) {
        return false;
    }
    if (g_exec_context == NULL) {
        return true;
    }
    if (start < EXEC_USER_ADDRESS_MIN || start >= EXEC_USER_ADDRESS_MAX) {
        return false;
    }
    end = start + size;
    if (end < start || end > EXEC_USER_ADDRESS_MAX) {
        return false;
    }
    return true;
}

const exec_launch_info_t *exec_current_launch_info(void)
{
    if (g_exec_context == NULL) {
        return NULL;
    }
    return g_exec_context->launch_info;
}

const char *exec_current_program_path(void)
{
    if (g_exec_context == NULL || g_exec_context->program_path[0] == '\0') {
        return PATH_ROOT;
    }
    return g_exec_context->program_path;
}

uint32_t exec_current_image_flags(void)
{
    return g_exec_context != NULL ? g_exec_context->image_flags : 0;
}

uint32_t exec_current_privilege_level(void)
{
    return g_exec_context != NULL ? g_exec_context->privilege_level : EXEC_PRIV_R0;
}

bool exec_grant_current_privilege(uint32_t privilege_level)
{
    if (g_exec_context == NULL ||
        privilege_level != EXEC_PRIV_R2 ||
        g_exec_context->privilege_level <= privilege_level) {
        return false;
    }
    g_exec_context->privilege_level = privilege_level;
    g_exec_context->image_flags |= EXEC_IMAGE_FLAG_NEEDS_R2;
    if (g_exec_context->launch_info != NULL) {
        ((exec_launch_info_t *) g_exec_context->launch_info)->privilege_level = privilege_level;
        ((exec_launch_info_t *) g_exec_context->launch_info)->image_flags |= EXEC_IMAGE_FLAG_NEEDS_R2;
    }
    return true;
}

void exec_complete_from_syscall(int32_t exit_code)
{
    if (g_exec_context == NULL) {
        return;
    }
    g_exec_context->completed = true;
    g_exec_context->faulted = false;
    g_exec_context->exit_code = exit_code;
}

void exec_abort_from_exception(uint8_t vector, uint64_t error_code)
{
    int32_t exit_code;

    if (g_exec_context == NULL) {
        return;
    }

    exit_code = -((int32_t) vector + 0x100);
    if (error_code != 0) {
        exit_code -= (int32_t) (error_code & 0xFF);
    }
    g_exec_context->completed = true;
    g_exec_context->faulted = true;
    g_exec_context->fault_vector = vector;
    g_exec_context->fault_error_code = error_code;
    g_exec_context->exit_code = exit_code;
}

void exec_shutdown_active(void)
{
    if (g_exec_context == NULL) {
        return;
    }
    g_exec_context->completed = true;
    g_exec_context->faulted = false;
    g_exec_context->exit_code = -1;
}

uint64_t exec_kernel_stack_top(void)
{
    if (g_exec_context == NULL || g_exec_context->kernel_stack == NULL) {
        return 0;
    }
    return (uint64_t) (g_exec_context->kernel_stack + EXEC_KERNEL_STACK_SIZE);
}

uint64_t exec_user_stack_top(void)
{
    return EXEC_USER_STACK_TOP;
}

bool exec_process_completed(void)
{
    return g_exec_context != NULL && g_exec_context->completed;
}

uint64_t exec_resume_stack_pointer(void)
{
    if (g_exec_context == NULL) {
        return 0;
    }
    return g_exec_context->resume_rsp;
}

bool exec_run(const char *path, uint32_t argc, char *argv[], const char *cwd, char *env[], uint32_t env_count, int32_t *exit_code)
{
    return exec_run_with_flags(path, argc, argv, cwd, env, env_count, 0, exit_code);
}

bool exec_run_with_flags(const char *path, uint32_t argc, char *argv[], const char *cwd, char *env[], uint32_t env_count, uint32_t run_flags, int32_t *exit_code)
{
    uint8_t *image = NULL;
    char resolved_path[PATH_MAX_LEN];
    char cwd_copy[PATH_MAX_LEN];
    int32_t image_size;
    exec_prepared_image_t prepared;
    uint32_t image_flags;
    exec_launch_info_t *user_launch_info;
    exec_runtime_context_t runtime_context;
    exec_entry_t entry;
    uint8_t *kernel_stack = NULL;
    int32_t pid = -1;
    uint32_t privilege_level;
    bool run_as_r0_driver = false;
    bool console_window_requested = false;
    bool console_window_opened = false;
    exec_loaded_dll_t dlls[EXEC_DLL_MAX];
    uint32_t dll_count = 0;

    if (g_exec_context != NULL) {
        return false;
    }
    memset(dlls, 0, sizeof(dlls));
    if (!path_resolve(cwd, path, resolved_path, sizeof(resolved_path))) {
        return false;
    }
    /*
     * Native .sys images are driver packages, not user processes. They may
     * be inspected, installed, or registered by a driver-management program,
     * but they must never enter the generic process executor.
     */
    if (exec_path_has_suffix(resolved_path, ".sys")) {
        return false;
    }
    image_size = file_size(resolved_path);
    if (image_size <= 0 || file_is_dir(resolved_path)) {
        return false;
    }

    image = (uint8_t *) kmalloc((uint32_t) image_size);
    if (image == NULL) {
        return false;
    }
    if (file_read(resolved_path, image, (uint32_t) image_size) != image_size) {
        kfree(image);
        return false;
    }

    if (!exec_prepare_image(image, (uint32_t) image_size, resolved_path, &prepared, &image_flags) ||
        !exec_validate_prepared_image(&prepared)) {
        kfree(image);
        return false;
    }
    if (prepared.format == EXEC_IMAGE_FORMAT_PE && prepared.is_dll) {
        kfree(image);
        return false;
    }
    image_flags = exec_flags_for_prepared_image(&prepared, resolved_path, image_flags);
    if (!exec_load_prepared_image(&prepared)) {
        kfree(image);
        return false;
    }
    if (!exec_bind_imports_for_image(&prepared, dlls, &dll_count)) {
        exec_free_loaded_dlls(dlls, dll_count);
        kfree(image);
        return false;
    }
    if ((run_flags & EXEC_RUN_FLAG_CONSOLE_WINDOW) != 0) {
        image_flags |= EXEC_IMAGE_FLAG_CONSOLE;
    }
    /*
     * Shell-launched console programs inherit console 0. A separate
     * graphical console is opt-in for callers that explicitly request it.
     */
    console_window_requested = (run_flags & EXEC_RUN_FLAG_CONSOLE_WINDOW) != 0;
    run_as_r0_driver = (image_flags & EXEC_IMAGE_FLAG_DRIVER) != 0 &&
                       (image_flags & EXEC_IMAGE_FLAG_SIGNED) != 0 &&
                       (image_flags & EXEC_IMAGE_FLAG_NEEDS_R0) != 0 &&
                       (run_flags & EXEC_RUN_FLAG_TRUSTED_R0) != 0;
    privilege_level = run_as_r0_driver ? EXEC_PRIV_R0 :
                      ((run_flags & EXEC_RUN_FLAG_ELEVATED) != 0 ? EXEC_PRIV_R2 : EXEC_PRIV_R3);
    if ((image_flags & EXEC_IMAGE_FLAG_NEEDS_R0) != 0 &&
        (((image_flags & EXEC_IMAGE_FLAG_DRIVER) == 0) || privilege_level != EXEC_PRIV_R0)) {
        exec_free_loaded_dlls(dlls, dll_count);
        kfree(image);
        return false;
    }
    if ((image_flags & EXEC_IMAGE_FLAG_NEEDS_R2) != 0 && privilege_level > EXEC_PRIV_R2) {
        exec_free_loaded_dlls(dlls, dll_count);
        kfree(image);
        return false;
    }

    if (!path_resolve(PATH_ROOT, cwd == NULL ? PATH_ROOT : cwd, cwd_copy, sizeof(cwd_copy))) {
        strcpy(cwd_copy, PATH_ROOT);
    }
    user_launch_info = exec_build_user_launch_info(argc, argv, cwd_copy, resolved_path, env, env_count, image_flags, privilege_level, prepared.subsystem);
    if (user_launch_info == NULL) {
        exec_free_loaded_dlls(dlls, dll_count);
        kfree(image);
        return false;
    }

    kernel_stack = (uint8_t *) kmalloc(EXEC_KERNEL_STACK_SIZE);
    if (kernel_stack == NULL) {
        exec_free_loaded_dlls(dlls, dll_count);
        kfree(image);
        return false;
    }

    pid = pcb_process_start(resolved_path);
    if (pid >= 0 && console_window_requested) {
        console_window_opened = terminal_open_process_console(pid, resolved_path);
    }
    runtime_context.launch_info = user_launch_info;
    runtime_context.image_flags = image_flags;
    runtime_context.privilege_level = privilege_level;
    strlcpy(runtime_context.cwd, cwd_copy, sizeof(runtime_context.cwd));
    strlcpy(runtime_context.program_path, resolved_path, sizeof(runtime_context.program_path));
    runtime_context.exit_code = -1;
    runtime_context.fault_vector = 0;
    runtime_context.fault_error_code = 0;
    runtime_context.completed = false;
    runtime_context.faulted = false;
    runtime_context.kernel_stack = kernel_stack;
    runtime_context.resume_rsp = 0;
    g_exec_context = &runtime_context;
    tss_set_rsp0(exec_kernel_stack_top());

    entry = (exec_entry_t) prepared.entry;
    if (run_as_r0_driver) {
        exec_enter_kernel_mode((uint64_t) entry,
                               (uint64_t) user_launch_info,
                               exec_kernel_stack_top(),
                               (uint64_t) &runtime_context.resume_rsp);
    } else {
        exec_enter_user_mode((uint64_t) entry,
                             exec_user_stack_top(),
                             (uint64_t) user_launch_info,
                             exec_kernel_stack_top(),
                             (uint64_t) &runtime_context.resume_rsp);
    }

    {
        bool completed = runtime_context.completed;
        bool faulted = runtime_context.faulted;
        int32_t final_exit_code = runtime_context.exit_code;
        uint8_t fault_vector = runtime_context.fault_vector;
        uint64_t fault_error_code = runtime_context.fault_error_code;

        g_exec_context = NULL;
        if (exit_code != NULL) {
            *exit_code = final_exit_code;
        }
        if (pid >= 0) {
            if (completed) {
                if (faulted) {
                    pcb_process_fault(pid,
                                      fault_vector,
                                      fault_error_code,
                                      final_exit_code);
                } else {
                    pcb_process_exit(pid, final_exit_code);
                }
            } else {
                pcb_process_abort(pid, final_exit_code);
            }
        }

        if (console_window_opened) {
            terminal_finish_process_console(pid, final_exit_code);
        }
        if (kernel_stack != NULL) {
            kfree(kernel_stack);
        }
        exec_free_loaded_dlls(dlls, dll_count);
        kfree(image);
        return completed;
    }

}

/* ------------------------------------------------------------------ */
/* CGI stdout capture                                                  */
/*                                                                     */
/* When a user process (httpd) asks the kernel to run a CGI child via  */
/* SYS_EXEC_CAPTURE, exec_run_with_flags() unconditionally wipes and  */
/* reloads the whole flat user image region [EXEC_LOAD_BASE,         */
/* EXEC_LOAD_LIMIT). That would destroy the caller's own code, data  */
/* and stack. We snapshot the region before spawning the child and    */
/* restore it afterwards so the caller resumes as if nothing happened. */
/* The child's stdout (SYS_HANDLE_WRITE on stdout/stderr) is          */
/* redirected into a caller-provided kernel buffer.                  */
/* ------------------------------------------------------------------ */
static char *g_exec_capture_buf;
static uint32_t g_exec_capture_cap;
static uint32_t g_exec_capture_len;

static __attribute__((unused)) uint8_t g_exec_image_save[EXEC_LOAD_LIMIT - EXEC_LOAD_BASE];

bool exec_capture_active(void)
{
    return g_exec_capture_buf != NULL;
}

void exec_capture_write(const char *buffer, uint32_t size)
{
    uint32_t i;

    if (g_exec_capture_buf == NULL || buffer == NULL || size == 0U) {
        return;
    }
    for (i = 0U; i < size; i++) {
        if (g_exec_capture_len + 1U >= g_exec_capture_cap) {
            break;
        }
        g_exec_capture_buf[g_exec_capture_len++] = buffer[i];
    }
    g_exec_capture_buf[g_exec_capture_len] = '\0';
}

bool exec_run_capture(const char *path, char *out_buf, uint32_t out_cap, int32_t *exit_code, char *envp[])
{
    exec_runtime_context_t *saved_ctx;
    bool nested;
    /* image_saved removed: per-process AS makes snapshot unnecessary */
    bool ok;
    int32_t child_exit = -1;
    char *child_argv[2];

    if (path == NULL || out_buf == NULL || out_cap == 0U || g_exec_capture_buf != NULL) {
        return false;
    }

    nested = exec_active();
    saved_ctx = g_exec_context;

    if (nested) {
        /* Per-process AS: child has its own user pages, no snapshot needed. */
        g_exec_context = NULL;
    }

    g_exec_capture_buf = out_buf;
    g_exec_capture_cap = out_cap;
    g_exec_capture_len = 0U;
    out_buf[0] = '\0';

    child_argv[0] = (char *) path;
    child_argv[1] = NULL;

    ok = exec_run_with_flags(path, 1U, child_argv, NULL, envp, envp ? 2U : 0U, 0U, &child_exit);

    g_exec_capture_buf = NULL;
    g_exec_capture_cap = 0U;


    /* Restore the caller's execution context and TSS rsp0 so the caller's
     * next syscall lands on its own kernel stack (the child kernel stack
     * was freed by exec_run_with_flags). */
    g_exec_context = saved_ctx;
    if (exec_active()) {
        tss_set_rsp0(exec_kernel_stack_top());
    }

    if (exit_code != NULL) {
        *exit_code = child_exit;
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* fork support: run a forked child PCB to completion.                */
/* Mirrors the nested-exec pattern of exec_run_capture: swap in the   */
/* child's exec_runtime_context + address space + kernel stack, iretq  */
/* into the child at its saved register state, and return here once   */
/* the child exits.                                                    */
/* ------------------------------------------------------------------ */
extern void fork_enter_user_mode(uint64_t entry,
                                 uint64_t user_rsp,
                                 uint64_t rflags,
                                 uint64_t kernel_stack_top,
                                 uint64_t resume_rsp_slot_ptr);

int32_t exec_run_child(pcb_t *child)
{
    exec_runtime_context_t *saved_ctx;
    exec_runtime_context_t *child_ctx;
    pcb_t *parent;
    int32_t code;

    if (child == NULL || !child->addr_space.active || child->kernel_stack == NULL) {
        return -1;
    }

    parent = pcb_get_current();
    saved_ctx = g_exec_context;

    child_ctx = (exec_runtime_context_t *) kmalloc(sizeof(exec_runtime_context_t));
    if (child_ctx == NULL) {
        return -1;
    }
    memset(child_ctx, 0, sizeof(*child_ctx));
    child_ctx->kernel_stack = child->kernel_stack;
    child_ctx->completed = false;

    /* Adopt the child so its syscalls bookkeep against the right PCB. */
    pcb_set_current((int32_t) child->pid);
    g_exec_context = child_ctx;
    vm_switch_to(&child->addr_space);
    tss_set_rsp0((uint64_t) child->kernel_stack + EXEC_KERNEL_STACK_SIZE);

    fork_enter_user_mode(child->reg_rip,
                         child->reg_rsp,
                         child->reg_rflags,
                         (uint64_t) child->kernel_stack + EXEC_KERNEL_STACK_SIZE,
                         (uint64_t) &child_ctx->resume_rsp);

    /* The child has exited via sys_exit / SYS_EXIT_PROCESS. */
    code = child_ctx->exit_code;

    g_exec_context = saved_ctx;
    if (parent != NULL) {
        pcb_set_current((int32_t) parent->pid);
        vm_switch_to(&parent->addr_space);
        if (saved_ctx != NULL) {
            tss_set_rsp0(exec_kernel_stack_top());
        }
    } else {
        vm_switch_to(NULL);
    }
    kfree(child_ctx);
    return code;
}
