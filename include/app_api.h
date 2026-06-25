#ifndef _APP_API_H_
#define _APP_API_H_

#include "exec.h"
#include "mouse.h"
#include "string.h"
#include "system_status.h"
#include "syscall.h"

extern const exec_launch_info_t *g_app_launch_info;

static inline const exec_launch_info_t *app_launch_info(void)
{
    return g_app_launch_info;
}

static inline int32_t app_handle_write(uint64_t handle, const char *buffer, uint32_t size)
{
    return (int32_t) syscall3(SYS_HANDLE_WRITE, handle, (uint64_t) buffer, size);
}

static inline int32_t app_handle_read(uint64_t handle, char *buffer, uint32_t size)
{
    return (int32_t) syscall3(SYS_HANDLE_READ, handle, (uint64_t) buffer, size);
}

static inline int32_t app_write_string(uint64_t handle, const char *text)
{
    return app_handle_write(handle, text, (uint32_t) strlen(text));
}

static inline int32_t app_get_cwd(char *buffer, uint32_t size)
{
    return (int32_t) syscall2(SYS_GET_CWD, (uint64_t) buffer, size);
}

static inline int32_t app_get_mouse(mouse_snapshot_t *snapshot)
{
    return (int32_t) syscall2(SYS_MOUSE_GET_STATE, (uint64_t) snapshot, sizeof(*snapshot));
}

static inline int32_t app_get_system_status(system_status_t *status)
{
    return (int32_t) syscall2(SYS_SYSTEM_STATUS, (uint64_t) status, sizeof(*status));
}

static inline void app_enter_graphics_mode(void)
{
    (void) syscall0(SYS_ENTER_GRAPHICS_MODE);
}

static inline int32_t app_graphics_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color)
{
    uint64_t pos = ((uint64_t) x << 48) | ((uint64_t) y << 32) | ((uint64_t) width << 16) | height;

    return (int32_t) syscall2(SYS_GRAPHICS_FILL_RECT, pos, color);
}

static inline void app_graphics_present(void)
{
    (void) syscall0(SYS_GRAPHICS_PRESENT);
}

static inline bool app_request_r2(const char *reason)
{
    return syscall1(SYS_REQUEST_R2, (uint64_t) reason) == 0;
}

static inline bool app_request_r0(const char *reason)
{
    return syscall1(SYS_REQUEST_R0, (uint64_t) reason) == 0;
}

static inline void app_exit(int32_t code)
{
    (void) syscall1(SYS_EXIT_PROCESS, (uint64_t) code);
    for (;;) {
    }
}

#endif
