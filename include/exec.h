#ifndef _EXEC_H_
#define _EXEC_H_

#include "stdbool.h"
#include "stdint.h"

#define EXEC_ABI_VERSION      1U
#define EXEC_HANDLE_STDIN     0U
#define EXEC_HANDLE_STDOUT    1U
#define EXEC_HANDLE_STDERR    2U

typedef struct {
    uint32_t abi_version;
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
bool exec_resolve_path(const char *path, char *output, uint32_t output_size);
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
