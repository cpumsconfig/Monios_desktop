#ifndef _PROC_MGMT_H_
#define _PROC_MGMT_H_

#include "stdbool.h"
#include "stdint.h"

/* Process management syscall handlers (called from syscall.c dispatch). */
int32_t  sys_fork(void *frame);
int32_t  sys_execv(const char *path, char *argv[]);
int32_t  sys_waitpid(int32_t pid, int32_t *exit_code);
int32_t  sys_getpid(void);
int32_t  sys_getppid(void);
void     sys_exit(int32_t exit_code);

/* Initialise the process-management subsystem. */
void proc_mgmt_init(void);

#endif /* _PROC_MGMT_H_ */
