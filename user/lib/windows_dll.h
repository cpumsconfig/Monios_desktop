#ifndef _WINDOWS_DLL_H_
#define _WINDOWS_DLL_H_

#include "stdint.h"

#define WINDOWS_DLL_ABI_VERSION 1U

#if !defined(WINDOWS_DLL_BUILD) && defined(__GNUC__)
#define WINDOWS_DLL_API __attribute__((dllimport))
#else
#define WINDOWS_DLL_API
#endif

WINDOWS_DLL_API uint32_t windows_abi_version(void);
WINDOWS_DLL_API void windows_enter_graphics_mode(void);
WINDOWS_DLL_API int32_t windows_fill_rect(uint16_t x, uint16_t y, uint16_t width,
                                           uint16_t height, uint32_t color);
WINDOWS_DLL_API int32_t windows_draw_text(uint16_t x, uint16_t y, const char *text, uint32_t color);
WINDOWS_DLL_API void windows_present(void);
WINDOWS_DLL_API int32_t windows_open_cube3d_window(void);
WINDOWS_DLL_API int32_t windows_open_notepad_window(void);

#endif
