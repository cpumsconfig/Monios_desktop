#ifndef _APP_API_H_
#define _APP_API_H_

#include "exec.h"
#include "mouse.h"
#include "string.h"
#include "system_status.h"
#include "console_dll.h"
#include "windows_dll.h"

#if defined(__GNUC__)
#define APP_API_DLL_IMPORT __attribute__((dllimport))
#else
#define APP_API_DLL_IMPORT
#endif

APP_API_DLL_IMPORT int32_t monios_getcwd(char *buffer, uint32_t size);
APP_API_DLL_IMPORT int32_t monios_get_mouse(mouse_snapshot_t *snapshot);
APP_API_DLL_IMPORT int32_t monios_get_system_status(system_status_t *status);
APP_API_DLL_IMPORT int32_t monios_request_r2(const char *reason);
APP_API_DLL_IMPORT int32_t monios_request_r0(const char *reason);
APP_API_DLL_IMPORT void monios_exit_process(int32_t code);

extern const exec_launch_info_t *g_app_launch_info;

static inline const exec_launch_info_t *app_launch_info(void)
{
    return g_app_launch_info;
}

static inline int32_t app_handle_write(uint64_t handle, const char *buffer, uint32_t size)
{
    return console_write_handle(handle, buffer, size);
}

static inline int32_t app_handle_read(uint64_t handle, char *buffer, uint32_t size)
{
    return console_read_handle(handle, buffer, size);
}

static inline int32_t app_write_string(uint64_t handle, const char *text)
{
    return app_handle_write(handle, text, (uint32_t) strlen(text));
}

static inline int32_t app_get_cwd(char *buffer, uint32_t size)
{
    return monios_getcwd(buffer, size);
}

static inline int32_t app_get_mouse(mouse_snapshot_t *snapshot)
{
    return monios_get_mouse(snapshot);
}

static inline int32_t app_get_system_status(system_status_t *status)
{
    return monios_get_system_status(status);
}

static inline void app_enter_graphics_mode(void)
{
    windows_enter_graphics_mode();
}

static inline int32_t app_graphics_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color)
{
    return windows_fill_rect(x, y, width, height, color);
}

static inline void app_graphics_present(void)
{
    windows_present();
}

static inline bool app_request_r2(const char *reason)
{
    return monios_request_r2(reason) == 0;
}

static inline bool app_request_r0(const char *reason)
{
    return monios_request_r0(reason) == 0;
}

static inline void app_exit(int32_t code)
{
    monios_exit_process(code);
    for (;;) {
    }
}

#endif
