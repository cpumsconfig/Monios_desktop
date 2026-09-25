#ifndef _EXEC_H_
#define _EXEC_H_

#include "stdbool.h"
#include "stdint.h"
#include "pcb.h"

#define EXEC_ABI_VERSION      5U
#define EXEC_HANDLE_STDIN     0U
#define EXEC_HANDLE_STDOUT    1U
#define EXEC_HANDLE_STDERR    2U

#define EXEC_SUBSYSTEM_UNKNOWN 0U
#define EXEC_SUBSYSTEM_NATIVE  1U
#define EXEC_SUBSYSTEM_WINDOWS 2U
#define EXEC_SUBSYSTEM_CONSOLE 3U

#define EXEC_IMAGE_FLAG_CONSOLE      0x00000001U
#define EXEC_IMAGE_FLAG_GUI          0x00000002U
#define EXEC_IMAGE_FLAG_DRIVER       0x00000004U
#define EXEC_IMAGE_FLAG_SIGNED       0x00000008U
#define EXEC_IMAGE_FLAG_NEEDS_R0     0x00000010U
#define EXEC_IMAGE_FLAG_NEEDS_R2     0x00000020U
#define EXEC_IMAGE_FLAG_CERT_PRESENT 0x00000040U
#define EXEC_IMAGE_FLAG_CERT_VALID   0x00000080U
#define EXEC_IMAGE_FLAG_CERT_REQUIRED 0x00000100U
#define EXEC_IMAGE_FLAG_RESOURCE_TABLE 0x00000200U
#define EXEC_IMAGE_FLAG_ICON_RESOURCE 0x00000400U
#define EXEC_IMAGE_FLAG_MANIFEST_RESOURCE 0x00000800U

#define EXEC_RUN_FLAG_CONSOLE_WINDOW 0x00000001U
#define EXEC_RUN_FLAG_ADMIN          0x00000002U
#define EXEC_RUN_FLAG_ELEVATED       EXEC_RUN_FLAG_ADMIN
#define EXEC_RUN_FLAG_R0_DRIVER      0x00000004U
#define EXEC_RUN_FLAG_TRUSTED_R0     0x80000000U

#define EXEC_PRIV_R0 0U
#define EXEC_PRIV_R2 2U
#define EXEC_PRIV_R3 3U

#define EXEC_LOAD_BASE        0x04000000ULL
#define EXEC_LOAD_LIMIT       0x04400000ULL
#define EXEC_IMAGE_LIMIT      0x043C0000ULL
#define EXEC_USER_DATA_BASE   0x043C0000ULL
#define EXEC_USER_DATA_LIMIT  0x043F0000ULL
#define EXEC_USER_STACK_TOP   0x04400000ULL
#define EXEC_USER_ADDRESS_MIN EXEC_LOAD_BASE
#define EXEC_USER_ADDRESS_MAX EXEC_LOAD_LIMIT
#define EXEC_KERNEL_STACK_SIZE 65536U

typedef struct {
    uint32_t abi_version;
    uint32_t image_flags;
    uint32_t privilege_level;
    uint32_t subsystem;
    uint32_t argc;
    char **argv;
    char **env;
    uint32_t env_count;
    const char *cwd;
    const char *program_path;
    const char *user_name;
    uint64_t stdin_handle;
    uint64_t stdout_handle;
    uint64_t stderr_handle;
} exec_launch_info_t;

bool exec_run(const char *path, uint32_t argc, char *argv[], const char *cwd, char *env[], uint32_t env_count, int32_t *exit_code);
bool exec_run_with_flags(const char *path, uint32_t argc, char *argv[], const char *cwd, char *env[], uint32_t env_count, uint32_t run_flags, int32_t *exit_code);

/*
 * CGI support: run the .exe at `path` and redirect its stdout into the
 * kernel buffer `out_buf` (capacity `out_cap`, NUL-terminated) instead of
 * drawing it to the console. When invoked from inside another user process
 * (e.g. httpd), the caller's flat user image region is snapshot/restored
 * around the child so the caller can resume transparently. Returns true if
 * the child ran to completion.
 */
bool exec_capture_active(void);
void exec_capture_write(const char *buffer, uint32_t size);
bool exec_run_capture(const char *path, char *out_buf, uint32_t out_cap, int32_t *exit_code, char *envp[]);
bool exec_resolve_path(const char *path, char *output, uint32_t output_size);
uint32_t exec_image_flags_for_path(const char *path);
bool exec_signature_status_for_path(const char *path,
                                    bool *signed_present,
                                    bool *signature_valid,
                                    bool *publisher_trusted);
bool exec_extract_icon_bitmap(const char *path,
                              uint32_t *pixels,
                              uint16_t width,
                              uint16_t height);
const char *exec_current_cwd(void);
const char *exec_current_program_path(void);
uint32_t exec_current_image_flags(void);
uint32_t exec_current_privilege_level(void);
bool exec_grant_current_privilege(uint32_t privilege_level);
bool exec_active(void);
bool exec_address_in_active_image(uint64_t address);
bool exec_user_range_valid(const void *ptr, uint64_t size);
const exec_launch_info_t *exec_current_launch_info(void);
void exec_complete_from_syscall(int32_t exit_code);
void exec_abort_from_exception(uint8_t vector, uint64_t error_code);
void exec_shutdown_active(void);
uint64_t exec_kernel_stack_top(void);
uint64_t exec_user_stack_top(void);
bool exec_process_completed(void);
uint64_t exec_resume_stack_pointer(void);

/* Run a forked child PCB to completion on its own address space; returns the
 * child's exit code. Used by sys_waitpid(). */
int32_t exec_run_child(pcb_t *child);

#endif
