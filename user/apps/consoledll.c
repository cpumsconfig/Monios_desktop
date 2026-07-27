#define CONSOLE_DLL_BUILD 1
#include "appsys.h"
#include "console_dll.h"
#include "monios_dll.h"

#define CONSOLE_DLL_EXPORT __attribute__((dllexport))

static uint32_t console_strlen(const char *text)
{
    uint32_t len = 0;

    if (text == 0) {
        return 0;
    }
    while (text[len] != '\0') {
        len++;
    }
    return len;
}

int DllMainCRTStartup(void *module, uint32_t reason, void *reserved)
{
    (void) module;
    (void) reason;
    (void) reserved;
    return 1;
}

CONSOLE_DLL_EXPORT
uint32_t console_abi_version(void)
{
    return CONSOLE_DLL_ABI_VERSION;
}

CONSOLE_DLL_EXPORT
int32_t console_set_title(const char *title)
{
    return monios_console_set_title(title);
}

CONSOLE_DLL_EXPORT
int32_t console_write(const char *text)
{
    return monios_handle_write(STDOUT_FILENO, text, console_strlen(text));
}

CONSOLE_DLL_EXPORT
int32_t console_write_buffer(const void *buffer, uint32_t size)
{
    return monios_handle_write(STDOUT_FILENO, buffer, size);
}

CONSOLE_DLL_EXPORT
int32_t console_writeln(const char *text)
{
    int32_t written = console_write(text);

    if (written < 0) {
        return written;
    }
    if (monios_handle_write(STDOUT_FILENO, "\r\n", 2) != 2) {
        return -1;
    }
    return written + 2;
}

CONSOLE_DLL_EXPORT
int32_t console_error(const char *text)
{
    return monios_handle_write(STDERR_FILENO, text, console_strlen(text));
}

CONSOLE_DLL_EXPORT
int32_t console_read(void *buffer, uint32_t size)
{
    return monios_handle_read(STDIN_FILENO, buffer, size);
}

CONSOLE_DLL_EXPORT
int32_t console_write_handle(uint64_t handle, const void *buffer, uint32_t size)
{
    return monios_handle_write(handle, buffer, size);
}

CONSOLE_DLL_EXPORT
int32_t console_read_handle(uint64_t handle, void *buffer, uint32_t size)
{
    return monios_handle_read(handle, buffer, size);
}
