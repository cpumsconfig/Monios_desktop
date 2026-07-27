#define WINDOWS_DLL_BUILD 1
#include "stdint.h"
#include "syscall.h"
#include "monios_dll.h"
#include "windows_dll.h"

#define WINDOWS_DLL_EXPORT __attribute__((dllexport))

int DllMainCRTStartup(void *module, uint32_t reason, void *reserved)
{
    (void) module;
    (void) reason;
    (void) reserved;
    return 1;
}

WINDOWS_DLL_EXPORT
uint32_t windows_abi_version(void)
{
    return WINDOWS_DLL_ABI_VERSION;
}

WINDOWS_DLL_EXPORT
void windows_enter_graphics_mode(void)
{
    (void) monios_syscall0(SYS_ENTER_GRAPHICS_MODE);
}

WINDOWS_DLL_EXPORT
int32_t windows_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color)
{
    uint64_t pos = ((uint64_t) x << 48) | ((uint64_t) y << 32) | ((uint64_t) width << 16) | height;

    return (int32_t) monios_syscall2(SYS_GRAPHICS_FILL_RECT, pos, color);
}

WINDOWS_DLL_EXPORT
int32_t windows_draw_text(uint16_t x, uint16_t y, const char *text, uint32_t color)
{
    uint64_t pos = ((uint64_t) x << 48) | ((uint64_t) y << 32);

    return (int32_t) monios_syscall3(SYS_GRAPHICS_DRAW_TEXT, pos, (uint64_t) text, color);
}

WINDOWS_DLL_EXPORT
void windows_present(void)
{
    (void) monios_syscall0(SYS_GRAPHICS_PRESENT);
}

WINDOWS_DLL_EXPORT
int32_t windows_open_cube3d_window(void)
{
    return (int32_t) monios_syscall0(SYS_OPEN_CUBE3D_WINDOW);
}

WINDOWS_DLL_EXPORT
int32_t windows_open_notepad_window(void)
{
    return (int32_t) monios_syscall0(SYS_OPEN_NOTEPAD_WINDOW);
}
