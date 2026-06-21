#include "appsys.h"
#include "syscall.h"

static const app_launch_info_t *g_launch_info;

const app_launch_info_t *app_launch_info(void)
{
    return g_launch_info;
}

void app_runtime_set_launch_info(const app_launch_info_t *info)
{
    g_launch_info = info;
}

int app_getcwd(char *buffer, uint32_t size)
{
    return (int) syscall2(SYS_GET_CWD, (uint64_t) buffer, size);
}

int app_get_mouse(app_mouse_snapshot_t *snapshot)
{
    return (int) syscall2(SYS_MOUSE_GET_STATE, (uint64_t) snapshot, sizeof(*snapshot));
}

int app_get_system_status(app_system_status_t *status)
{
    return (int) syscall2(SYS_SYSTEM_STATUS, (uint64_t) status, sizeof(*status));
}

void app_enter_graphics_mode(void)
{
    (void) syscall0(SYS_ENTER_GRAPHICS_MODE);
}

void app_open_cube3d_window(void)
{
    (void) syscall0(SYS_OPEN_CUBE3D_WINDOW);
}

int app_audio_play_file(const char *path)
{
    return (int) syscall1(SYS_AUDIO_PLAY_FILE, (uint64_t) path);
}

int app_graphics_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color)
{
    uint64_t pos = ((uint64_t) x << 48) | ((uint64_t) y << 32) | ((uint64_t) width << 16) | height;

    return (int) syscall2(SYS_GRAPHICS_FILL_RECT, pos, color);
}

void app_graphics_present(void)
{
    (void) syscall0(SYS_GRAPHICS_PRESENT);
}

void app_exit(int code)
{
    (void) syscall1(SYS_EXIT_PROCESS, (uint64_t) code);
    for (;;) {
    }
}
