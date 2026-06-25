#ifndef _SYSCALL_H_
#define _SYSCALL_H_

#include "stdint.h"

#define SYS_GET_TICKS            0
#define SYS_LOG_STRING           1
#define SYS_FILE_ROOT_COUNT      2
#define SYS_FILE_READ            3
#define SYS_ENTER_GRAPHICS_MODE  4
#define SYS_FILE_WRITE           5
#define SYS_FILE_DELETE          6
#define SYS_FILE_MKDIR           7
#define SYS_FILE_RMDIR           8
#define SYS_FILE_LIST_DIR        9
#define SYS_HANDLE_WRITE         10
#define SYS_HANDLE_READ          11
#define SYS_GET_CWD              12
#define SYS_FILE_EXISTS          13
#define SYS_FILE_SIZE            14
#define SYS_MOUSE_GET_STATE      15
#define SYS_FILE_IS_DIR          16
#define SYS_EXIT_PROCESS         17
#define SYS_SYSTEM_STATUS        18
#define SYS_GRAPHICS_FILL_RECT   19
#define SYS_GRAPHICS_PRESENT     20
#define SYS_AUDIO_PLAY_FILE      21
#define SYS_OPEN_CUBE3D_WINDOW   22
#define SYS_SOCKET_CALL          23
#define SYS_FUTEX_CALL           24
#define SYS_IPC_CALL             25
#define SYS_SIGNAL_CALL          26
#define SYS_REQUEST_R0           27
#define SYS_REQUEST_R2           28

void syscall_init(void);
uint64_t syscall_interrupt_dispatch(void *frame_ptr);

static inline uint64_t syscall0(uint64_t nr)
{
    uint64_t ret;
    asm volatile ("int $0x80" : "=a" (ret) : "a" (nr) : "memory");
    return ret;
}

static inline uint64_t syscall1(uint64_t nr, uint64_t arg0)
{
    uint64_t ret;
    asm volatile ("int $0x80" : "=a" (ret) : "a" (nr), "b" (arg0) : "memory");
    return ret;
}

static inline uint64_t syscall2(uint64_t nr, uint64_t arg0, uint64_t arg1)
{
    uint64_t ret;
    asm volatile ("int $0x80" : "=a" (ret) : "a" (nr), "b" (arg0), "c" (arg1) : "memory");
    return ret;
}

static inline uint64_t syscall3(uint64_t nr, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    uint64_t ret;
    asm volatile ("int $0x80" : "=a" (ret) : "a" (nr), "b" (arg0), "c" (arg1), "d" (arg2) : "memory");
    return ret;
}

#endif
