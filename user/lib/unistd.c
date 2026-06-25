#include "unistd.h"
#include "syscall.h"

int read(uint64_t handle, void *buffer, uint32_t size)
{
    return (int) syscall3(SYS_HANDLE_READ, handle, (uint64_t) buffer, size);
}

int write(uint64_t handle, const void *buffer, uint32_t size)
{
    return (int) syscall3(SYS_HANDLE_WRITE, handle, (uint64_t) buffer, size);
}
