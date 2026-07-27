#include "unistd.h"
#include "console_dll.h"

int read(uint64_t handle, void *buffer, uint32_t size)
{
    return (int) console_read_handle(handle, buffer, size);
}

int write(uint64_t handle, const void *buffer, uint32_t size)
{
    return (int) console_write_handle(handle, buffer, size);
}
