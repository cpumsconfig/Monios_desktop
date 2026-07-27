#ifndef _CONSOLE_DLL_H_
#define _CONSOLE_DLL_H_

#include "stdint.h"

#define CONSOLE_DLL_ABI_VERSION 1U

#if !defined(CONSOLE_DLL_BUILD) && defined(__GNUC__)
#define CONSOLE_DLL_API __attribute__((dllimport))
#else
#define CONSOLE_DLL_API
#endif

CONSOLE_DLL_API uint32_t console_abi_version(void);
CONSOLE_DLL_API int32_t console_set_title(const char *title);
CONSOLE_DLL_API int32_t console_write(const char *text);
CONSOLE_DLL_API int32_t console_write_buffer(const void *buffer, uint32_t size);
CONSOLE_DLL_API int32_t console_writeln(const char *text);
CONSOLE_DLL_API int32_t console_error(const char *text);
CONSOLE_DLL_API int32_t console_read(void *buffer, uint32_t size);
CONSOLE_DLL_API int32_t console_write_handle(uint64_t handle, const void *buffer, uint32_t size);
CONSOLE_DLL_API int32_t console_read_handle(uint64_t handle, void *buffer, uint32_t size);

#endif
