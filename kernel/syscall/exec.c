#include "common.h"
#include "exec.h"
#include "file.h"
#include "hash.h"
#include "memory.h"
#include "mmu.h"
#include "path.h"
#include "pcb.h"
#include "session.h"

#define ELF_MAGIC_0 0x7F
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'
#define ELF_CLASS_64 2
#define ELF_DATA_LSB 1
#define ELF_TYPE_EXEC 2
#define ELF_MACHINE_X86_64 0x3E
#define ELF_PT_LOAD 1

#define RZS_MAGIC_0 'R'
#define RZS_MAGIC_1 'Z'
#define RZS_MAGIC_2 'S'
#define RZS_MAGIC_3 '1'
#define RZS_VERSION 1U
#define RZS_SIGNATURE_SIZE 64U

#define EXEC_LOAD_BASE   0x04000000ULL
#define EXEC_LOAD_LIMIT  0x04400000ULL
#define EXEC_IMAGE_LIMIT 0x043C0000ULL
#define EXEC_USER_DATA_BASE 0x043C0000ULL
#define EXEC_USER_DATA_LIMIT 0x043F0000ULL
#define EXEC_USER_STACK_TOP 0x04400000ULL

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
    uint8_t magic[4];
    uint16_t version;
    uint16_t header_size;
    uint32_t image_size;
    uint32_t image_flags;
    uint32_t signature_size;
    uint8_t image_sha256[HASH_SHA256_DIGEST_SIZE];
    uint8_t signature[RZS_SIGNATURE_SIZE];
} __attribute__((packed)) rzs_header_t;

typedef int32_t (*exec_entry_t)(const exec_launch_info_t *info);

typedef struct {
    const exec_launch_info_t *launch_info;
    int32_t exit_code;
    bool completed;
    uint8_t *kernel_stack;
    uint64_t resume_rsp;
} exec_runtime_context_t;

static exec_runtime_context_t *g_exec_context;
static const uint8_t g_rzs_signature_key[] = {
    'M','O','N','I','O','S','-','R','Z','S','-','D','E','V','-','K','E','Y'
};

extern void exec_enter_user_mode(uint64_t entry,
                                 uint64_t user_stack_top,
                                 uint64_t launch_info_ptr,
                                 uint64_t kernel_resume_stack_top,
                                 uint64_t resume_rsp_slot_ptr);

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
    uint64_t aligned = (address + (align - 1)) & ~(uint64_t) (align - 1);
    uint8_t *result = (uint8_t *) aligned;

    if (result + size > limit) {
        return NULL;
    }
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

static exec_launch_info_t *exec_build_user_launch_info(uint32_t argc, char *argv[], const char *cwd, const char *program_path, char *env[], uint32_t env_count, uint32_t image_flags, uint32_t privilege_level)
{
    uint8_t *cursor = (uint8_t *) EXEC_USER_DATA_BASE;
    uint8_t *limit = (uint8_t *) EXEC_USER_DATA_LIMIT;
    exec_launch_info_t *info = (exec_launch_info_t *) exec_user_alloc(&cursor, sizeof(exec_launch_info_t), 16, limit);
    char **user_argv = NULL;
    char **user_env = NULL;

    if (info == NULL) {
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
    info->reserved = 0;
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
        if (*path == '/' || *path == '\\') {
            base = path + 1;
        }
        path++;
    }
    return strcasecmp(base, name) == 0;
}

static void exec_compute_rzs_signature(const uint8_t digest[HASH_SHA256_DIGEST_SIZE], uint8_t signature[RZS_SIGNATURE_SIZE])
{
    sha256_ctx_t ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, g_rzs_signature_key, sizeof(g_rzs_signature_key));
    sha256_update(&ctx, digest, HASH_SHA256_DIGEST_SIZE);
    sha256_update(&ctx, g_rzs_signature_key, sizeof(g_rzs_signature_key));
    sha256_final(&ctx, signature);

    sha256_init(&ctx);
    sha256_update(&ctx, digest, HASH_SHA256_DIGEST_SIZE);
    sha256_update(&ctx, g_rzs_signature_key, sizeof(g_rzs_signature_key));
    sha256_update(&ctx, signature, HASH_SHA256_DIGEST_SIZE);
    sha256_final(&ctx, signature + HASH_SHA256_DIGEST_SIZE);
}

static bool exec_verify_rzs_header(const uint8_t *image, uint32_t image_size, const uint8_t **elf_image, uint32_t *elf_size, uint32_t *image_flags)
{
    const rzs_header_t *header;
    uint8_t digest[HASH_SHA256_DIGEST_SIZE];
    uint8_t expected_signature[RZS_SIGNATURE_SIZE];

    if (image == NULL || elf_image == NULL || elf_size == NULL || image_flags == NULL) {
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
    if (header->signature_size != RZS_SIGNATURE_SIZE) {
        return false;
    }
    if ((uint32_t) header->header_size + header->image_size > image_size) {
        return false;
    }

    *elf_image = image + header->header_size;
    *elf_size = header->image_size;
    hash_sha256(*elf_image, *elf_size, digest);
    if (memcmp(digest, header->image_sha256, sizeof(digest)) != 0) {
        return false;
    }
    exec_compute_rzs_signature(digest, expected_signature);
    if (memcmp(expected_signature, header->signature, sizeof(expected_signature)) != 0) {
        return false;
    }
    *image_flags = header->image_flags | EXEC_IMAGE_FLAG_SIGNED;
    return true;
}

static bool exec_prepare_image(const uint8_t *image, uint32_t image_size, const char *resolved_path, const elf64_ehdr_t **header, const uint8_t **elf_image, uint32_t *elf_size, uint32_t *image_flags)
{
    uint32_t flags = 0;

    if (image == NULL || header == NULL || elf_image == NULL || elf_size == NULL || image_flags == NULL) {
        return false;
    }
    if (exec_path_has_suffix(resolved_path, ".rzs")) {
        if (!exec_verify_rzs_header(image, image_size, elf_image, elf_size, &flags)) {
            return false;
        }
    } else {
        *elf_image = image;
        *elf_size = image_size;
    }

    if (exec_path_has_suffix(resolved_path, ".rzs")) {
        flags |= EXEC_IMAGE_FLAG_DRIVER | EXEC_IMAGE_FLAG_NEEDS_R2;
    } else if (exec_path_basename_equals(resolved_path, "player.elf") ||
               exec_path_basename_equals(resolved_path, "notepad.elf") ||
               exec_path_basename_equals(resolved_path, "taskmgr.elf") ||
               exec_path_basename_equals(resolved_path, "cube3d.elf") ||
               exec_path_has_suffix(resolved_path, ".exe")) {
        flags |= EXEC_IMAGE_FLAG_GUI;
    } else {
        flags |= EXEC_IMAGE_FLAG_CONSOLE;
    }

    *header = (const elf64_ehdr_t *) *elf_image;
    *image_flags = flags;
    return true;
}

static bool exec_validate_header(const elf64_ehdr_t *header, uint32_t image_size)
{
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
    if (header->e_phoff + (uint64_t) header->e_phnum * sizeof(elf64_phdr_t) > image_size) {
        return false;
    }
    if (!exec_range_valid(header->e_entry, 1)) {
        return false;
    }
    return true;
}

static bool exec_load_segments(const uint8_t *image, uint32_t image_size, const elf64_ehdr_t *header)
{
    const elf64_phdr_t *program_headers = (const elf64_phdr_t *) (image + header->e_phoff);

    memset((void *) EXEC_LOAD_BASE, 0, EXEC_LOAD_LIMIT - EXEC_LOAD_BASE);

    for (uint16_t i = 0; i < header->e_phnum; i++) {
        const elf64_phdr_t *program_header = &program_headers[i];

        if (program_header->p_type != ELF_PT_LOAD) {
            continue;
        }
        if (program_header->p_memsz < program_header->p_filesz) {
            return false;
        }
        if (program_header->p_offset + program_header->p_filesz > image_size) {
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

bool exec_resolve_path(const char *path, char *output, uint32_t output_size)
{
    const char *base = "/";

    if (g_exec_context != NULL && g_exec_context->launch_info != NULL && g_exec_context->launch_info->cwd != NULL) {
        base = g_exec_context->launch_info->cwd;
    }
    return path_resolve(base, path, output, output_size);
}

uint32_t exec_image_flags_for_path(const char *path)
{
    char resolved_path[PATH_MAX_LEN];

    if (path == NULL || path[0] == '\0') {
        return EXEC_IMAGE_FLAG_CONSOLE;
    }
    if (path_resolve(exec_current_cwd(), path, resolved_path, sizeof(resolved_path))) {
        if (exec_path_has_suffix(resolved_path, ".rzs")) {
            return EXEC_IMAGE_FLAG_DRIVER | EXEC_IMAGE_FLAG_SIGNED | EXEC_IMAGE_FLAG_NEEDS_R2;
        }
        if (exec_path_basename_equals(resolved_path, "player.elf") ||
            exec_path_basename_equals(resolved_path, "notepad.elf") ||
            exec_path_basename_equals(resolved_path, "taskmgr.elf") ||
            exec_path_basename_equals(resolved_path, "cube3d.elf") ||
            exec_path_has_suffix(resolved_path, ".exe")) {
            return EXEC_IMAGE_FLAG_GUI;
        }
    }
    return EXEC_IMAGE_FLAG_CONSOLE;
}

const char *exec_current_cwd(void)
{
    if (g_exec_context != NULL && g_exec_context->launch_info != NULL && g_exec_context->launch_info->cwd != NULL) {
        return g_exec_context->launch_info->cwd;
    }
    return "/";
}

bool exec_active(void)
{
    return g_exec_context != NULL;
}

const exec_launch_info_t *exec_current_launch_info(void)
{
    if (g_exec_context == NULL) {
        return NULL;
    }
    return g_exec_context->launch_info;
}

void exec_complete_from_syscall(int32_t exit_code)
{
    if (g_exec_context == NULL) {
        return;
    }
    g_exec_context->completed = true;
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
    g_exec_context->exit_code = exit_code;
}

void exec_shutdown_active(void)
{
    if (g_exec_context == NULL) {
        return;
    }
    g_exec_context->completed = true;
    g_exec_context->exit_code = -1;
}

uint64_t exec_kernel_stack_top(void)
{
    if (g_exec_context == NULL || g_exec_context->kernel_stack == NULL) {
        return 0;
    }
    return (uint64_t) (g_exec_context->kernel_stack + 8192);
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
    const elf64_ehdr_t *header;
    const uint8_t *elf_image;
    uint32_t elf_size;
    uint32_t image_flags;
    exec_launch_info_t *user_launch_info;
    exec_runtime_context_t runtime_context;
    exec_entry_t entry;
    uint8_t *kernel_stack = NULL;
    int32_t pid = -1;
    uint32_t privilege_level = (run_flags & EXEC_RUN_FLAG_ELEVATED) != 0 ? EXEC_PRIV_R2 : EXEC_PRIV_R3;

    if (g_exec_context != NULL) {
        return false;
    }
    if (!path_resolve(cwd, path, resolved_path, sizeof(resolved_path))) {
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

    if (!exec_prepare_image(image, (uint32_t) image_size, resolved_path, &header, &elf_image, &elf_size, &image_flags) ||
        !exec_validate_header(header, elf_size) ||
        !exec_load_segments(elf_image, elf_size, header)) {
        kfree(image);
        return false;
    }
    if ((image_flags & EXEC_IMAGE_FLAG_NEEDS_R0) != 0 &&
        (((image_flags & EXEC_IMAGE_FLAG_DRIVER) == 0) || privilege_level > EXEC_PRIV_R2)) {
        kfree(image);
        return false;
    }
    if ((image_flags & EXEC_IMAGE_FLAG_NEEDS_R2) != 0 && privilege_level > EXEC_PRIV_R2) {
        kfree(image);
        return false;
    }
    if ((run_flags & EXEC_RUN_FLAG_CONSOLE_WINDOW) != 0) {
        image_flags |= EXEC_IMAGE_FLAG_CONSOLE;
    }

    if (!path_resolve("/", cwd == NULL ? "/" : cwd, cwd_copy, sizeof(cwd_copy))) {
        cwd_copy[0] = '/';
        cwd_copy[1] = '\0';
    }
    user_launch_info = exec_build_user_launch_info(argc, argv, cwd_copy, resolved_path, env, env_count, image_flags, privilege_level);
    if (user_launch_info == NULL) {
        kfree(image);
        return false;
    }

    kernel_stack = (uint8_t *) kmalloc(8192);
    if (kernel_stack == NULL) {
        kfree(image);
        return false;
    }

    pid = pcb_process_start(resolved_path);
    runtime_context.launch_info = user_launch_info;
    runtime_context.exit_code = -1;
    runtime_context.completed = false;
    runtime_context.kernel_stack = kernel_stack;
    runtime_context.resume_rsp = 0;
    g_exec_context = &runtime_context;
    tss_set_rsp0(exec_kernel_stack_top());

    entry = (exec_entry_t) (uint64_t) header->e_entry;
    exec_enter_user_mode((uint64_t) entry,
                         exec_user_stack_top(),
                         (uint64_t) user_launch_info,
                         exec_kernel_stack_top(),
                         (uint64_t) &runtime_context.resume_rsp);

    g_exec_context = NULL;
    if (exit_code != NULL) {
        *exit_code = runtime_context.exit_code;
    }
    if (pid >= 0) {
        if (runtime_context.completed) {
            pcb_process_exit(pid, runtime_context.exit_code);
        } else {
            pcb_process_abort(pid, runtime_context.exit_code);
        }
    }

    if (kernel_stack != NULL) {
        kfree(kernel_stack);
    }
    kfree(image);
    return runtime_context.completed;
}
