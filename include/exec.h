#ifndef _EXEC_H_
#define _EXEC_H_

#include "stdbool.h"
#include "stdint.h"

#define EXEC_ABI_VERSION      1U
#define EXEC_HANDLE_STDIN     0U
#define EXEC_HANDLE_STDOUT    1U
#define EXEC_HANDLE_STDERR    2U

#define EXEC_IMAGE_FLAG_CONSOLE      0x00000001U
#define EXEC_IMAGE_FLAG_GUI          0x00000002U
#define EXEC_IMAGE_FLAG_DRIVER       0x00000004U
#define EXEC_IMAGE_FLAG_SIGNED       0x00000008U
#define EXEC_IMAGE_FLAG_NEEDS_R0     0x00000010U
#define EXEC_IMAGE_FLAG_NEEDS_R2     0x00000020U

#define EXEC_RUN_FLAG_CONSOLE_WINDOW 0x00000001U
#define EXEC_RUN_FLAG_ADMIN          0x00000002U
#define EXEC_RUN_FLAG_ELEVATED       EXEC_RUN_FLAG_ADMIN

#define EXEC_PRIV_R0 0U
#define EXEC_PRIV_R2 2U
#define EXEC_PRIV_R3 3U

typedef struct {
    uint32_t abi_version;
    uint32_t image_flags;
    uint32_t privilege_level;
    uint32_t reserved;
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
bool exec_resolve_path(const char *path, char *output, uint32_t output_size);
uint32_t exec_image_flags_for_path(const char *path);
const char *exec_current_cwd(void);
bool exec_active(void);
const exec_launch_info_t *exec_current_launch_info(void);
void exec_complete_from_syscall(int32_t exit_code);
void exec_abort_from_exception(uint8_t vector, uint64_t error_code);
void exec_shutdown_active(void);
uint64_t exec_kernel_stack_top(void);
uint64_t exec_user_stack_top(void);
bool exec_process_completed(void);
uint64_t exec_resume_stack_pointer(void);

#endif
