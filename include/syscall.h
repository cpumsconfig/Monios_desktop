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
#define SYS_AUDIO_PLAY_PCM       21
#define SYS_OPEN_CUBE3D_WINDOW   22
#define SYS_SOCKET_CALL          23
#define SYS_FUTEX_CALL           24
#define SYS_IPC_CALL             25
#define SYS_SIGNAL_CALL          26
#define SYS_REQUEST_R0           27
#define SYS_REQUEST_R2           28
#define SYS_REGISTRY_GET         29
#define SYS_REGISTRY_SET         30
#define SYS_DEFAULT_APP_GET      31
#define SYS_DEFAULT_APP_SET      32
#define SYS_EXEC_DEFER           33
#define SYS_INSTALLER_CALL       34
#define SYS_GRAPHICS_DRAW_TEXT   35
#define SYS_CONSOLE_SET_TITLE    36
#define SYS_OPEN_NOTEPAD_WINDOW  37
#define SYS_GRAPHICS_GET_WIDTH   38
#define SYS_GRAPHICS_GET_HEIGHT  39
#define SYS_DRIVER_LOAD          40
#define SYS_DRIVER_UNLOAD        41
#define SYS_DRIVER_QUERY         42
#define SYS_HTTP_GET_URL         43

/* New syscalls - batch expansion */
#define SYS_CLIPBOARD_SET        44
#define SYS_CLIPBOARD_GET        45
#define SYS_CLIPBOARD_HISTORY    46
#define SYS_NOTIFICATION_POST    47
#define SYS_THEME_SET            48
#define SYS_WALLPAPER_SET        49
#define SYS_DISPLAY_MODE         50
#define SYS_PROCESS_ENUM         51
#define SYS_PROCESS_TERMINATE    52
#define SYS_PROCESS_STATS        53
#define SYS_POWER_SET_STATE      54
#define SYS_AUDIO_MIXER_CTL      55
#define SYS_TIME_NTP_SYNC        56
#define SYS_PRINTER_CTL          57
#define SYS_FS_CACHE_CTL         58
#define SYS_MEM_COMPRESS_CTL     59
#define SYS_IOSCHED_CTL          60
#define SYS_GPU_ACCEL_CTL        61
#define SYS_FIREWALL_CTL         62
#define SYS_EFS_CTL              63
#define SYS_UAC_ELEVATE          64
#define SYS_USB_MOUNT_CTL        65
#define SYS_DOS_EXEC             66
#define SYS_DEBUG_CTL            67
#define SYS_PROFILE_CTL          68
#define SYS_PACKAGE_CTL          69

/* Verification-task syscalls (nettest / audiotest / gfxtest).
 * Allocate strictly after the previous maximum (69) to avoid collisions. */
#define SYS_NET_PING             70
#define SYS_NET_RESOLVE          71

/* Task 10/12: drain the raw keyboard event queue (special keys such as
 * arrows / F-keys / Esc are not delivered via stdin, only printable chars).
 * rbx = user buffer of 8 bytes: uint32 type, char ch, uint8 mods, pad[2].
 * Returns 1 when an event was copied, 0 when the queue is empty, -1 on error. */
#define SYS_KEYBOARD_READ_EVENT  72

/* Task 21/22: read pixels out of the composited framebuffer so user apps
 * can implement screen capture / recording.
 * rbx = user destination buffer (receives width*height uint32 pixels in
 *       the native 0x00RRGGBB format, row-major, pitch = width*4 bytes);
 * rcx = packed (x<<48)|(y<<32)|(width<<16)|height.
 * Returns the number of bytes copied (width*height*4, clipped to the
 * visible screen) or (uint64_t)-1 on error. */
#define SYS_GRAPHICS_READ_FRAMEBUFFER 73

/* Task 19: music-player control. rbx = pointer to audio_player_ctl_request_t. */
#define SYS_AUDIO_PLAYER_CTL     74

/* Task 21/22: read the wall-clock RTC so user apps can stamp output files.
 * rbx = user buffer of 8 bytes: uint16 year, uint8 month, day, hour,
 *       minute, second, uint8 pad[2].
 * Returns 0 on success, -1 on error. */
#define SYS_GET_RTC_TIME          75
#define SYS_HTTP_RESPONSE_MAX    (32U * 1024U)

/* Task 25: CPU 性能采样（火焰图）。
 *   76 SYS_PROFILER_START  rbx=divider(0=默认) -> 0/-1
 *   77 SYS_PROFILER_STOP   无参 -> 0
 *   78 SYS_PROFILER_GET_DATA rbx=用户缓冲区, rcx=缓冲区字节数 -> 拷贝字节数/-1
 * Task 26: 细粒度内存统计。
 *   79 SYS_MEMORY_STATS   rbx=用户缓冲区(memstats_snapshot_t), rcx=大小 -> 0/-1
 */
#define SYS_PROFILER_START        76
#define SYS_PROFILER_STOP         77
#define SYS_PROFILER_GET_DATA     78
#define SYS_MEMORY_STATS          79

/* CGI: run a .exe program and capture its stdout into a caller buffer.
 * rbx = user path (NUL-terminated), rcx = user output buffer,
 * rdx = output capacity in bytes, rsi = optional user int32 exit-code ptr.
 * Returns captured byte count (>=0) or (uint64_t)-1 on error. */
#define SYS_EXEC_CAPTURE          80

/* Multi-user / multi-process syscalls (81-95) */
#define SYS_FORK                  81
#define SYS_EXECV                 82
#define SYS_WAITPID               83
#define SYS_GETUID                84
#define SYS_SETUID                85
#define SYS_GETGID                86
#define SYS_SETGID                87
#define SYS_CHOWN                 88
#define SYS_CHMOD                 89
#define SYS_GETPID                90
#define SYS_GETPPID               91
#define SYS_GETEUID               92
#define SYS_GETEGID               93
#define SYS_SCHED_YIELD           94
#define SYS_SET_PRIORITY          95

/* One-click raw partition backup (backup.exe). op in rbx: see kernel/syscall.c. */
#define SYS_BACKUP_CTL            96

/* User-mode blit: copy a pixel buffer (BGRA8888, little-endian, matching the
 * framebuffer 0x00RRGGBB layout, row-major) into the kernel backbuffer at an
 * (x,y) origin, clipped to the visible screen.
 *   rbx = user source pixel buffer (width*height uint32 pixels);
 *   rcx = packed (x<<48)|(y<<32)|(width<<16)|height;
 *   rdx = flags (bit0 = enable source-over alpha blend; 0 = opaque overwrite).
 * Returns the number of pixels actually written (clipped) or (uint64_t)-1. */
#define SYS_GRAPHICS_BLIT         97

/* Microphone recording control. rbx = audio_record_request_t*.
 * (Note: 97 was already taken by GRAPHICS_BLIT, new syscalls start at 98.) */
#define SYS_AUDIO_REC_CTL         98

/* GPU/video decode control. rbx = video_ctl_request_t*. */
#define SYS_VIDEO_CTL              99

/* Low-level parallel-port printer channel. rbx = lpt_ctl_request_t*. */
#define SYS_LPT_CTL               100

/* Disk defragmentation control (defrag.exe / fs/defrag.c). */
#define SYS_DEFRAG_CTL            101
/* Memory diagnostic control (memtest.exe / kernel/mm/memtest.c). */
#define SYS_MEMTEST_CTL           102

/* Multi-process IPC / process-group extensions (103-107). */
/* SYS_PIPE: rbx = user int32[2] array {read_fd, write_fd}; rcx ignored.
 * Returns 0 on success, -1 on error. */
#define SYS_PIPE                  103
/* SYS_SHM_CALL: rbx = sub-operation (SHM_OP_*), rcx/rdx/rsi = args.
 *   create:  rcx=key(uint32), rdx=size(uint32) -> shmid
 *   attach:  rcx=shmid -> user vaddr (uint64)
 *   detach:  rcx=user addr -> 0/-1
 *   destroy: rcx=shmid -> 0/-1 */
#define SYS_SHM_CALL              104
/* SYS_SETPGID: rbx=pid, rcx=pgid -> 0/-1 */
#define SYS_SETPGID               105
/* SYS_GETPGID: rbx=pid -> pgid (caller's own when rbx==0) */
#define SYS_GETPGID               106
/* SYS_SETSID: no args -> new sid */
#define SYS_SETSID                107

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
static inline uint64_t syscall4(uint64_t nr, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    uint64_t ret;
    asm volatile ("int $0x80" : "=a" (ret) : "a" (nr), "b" (arg0), "c" (arg1), "d" (arg2), "S" (arg3) : "memory");
    return ret;
}

static inline uint64_t syscall5(uint64_t nr, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4)
{
    uint64_t ret;
    asm volatile ("int $0x80" : "=a" (ret) : "a" (nr), "b" (arg0), "c" (arg1), "d" (arg2), "S" (arg3), "D" (arg4) : "memory");
    return ret;
}

#endif
