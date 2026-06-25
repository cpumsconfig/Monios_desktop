#include "appsys.h"
#include "stdio.h"
#include "string.h"
#include "syscall.h"

#define SETUP_COPY_BUFFER_SIZE (1024U * 1024U)

static unsigned char g_copy_buffer[SETUP_COPY_BUFFER_SIZE];

static void setup_line(const char *text)
{
    fputs(text);
    fputs("\r\n");
}

static int32_t setup_file_size(const char *path)
{
    return (int32_t) syscall1(SYS_FILE_SIZE, (uint64_t) path);
}

static bool setup_file_exists(const char *path)
{
    return syscall1(SYS_FILE_EXISTS, (uint64_t) path) != 0;
}

static bool setup_mkdir(const char *path)
{
    if (setup_file_exists(path)) {
        return true;
    }
    return syscall1(SYS_FILE_MKDIR, (uint64_t) path) == 0;
}

static bool setup_copy_file(const char *src, const char *dst)
{
    int32_t size = setup_file_size(src);

    if (size < 0) {
        setup_line(src);
        setup_line("  source missing");
        return false;
    }
    if ((uint32_t) size > sizeof(g_copy_buffer)) {
        setup_line(src);
        setup_line("  source too large for setup buffer");
        return false;
    }
    if ((int32_t) syscall3(SYS_FILE_READ, (uint64_t) src, (uint64_t) g_copy_buffer, (uint32_t) size) != size) {
        setup_line(src);
        setup_line("  read failed");
        return false;
    }
    if ((int32_t) syscall3(SYS_FILE_WRITE, (uint64_t) dst, (uint64_t) g_copy_buffer, (uint32_t) size) != size) {
        setup_line(dst);
        setup_line("  write failed");
        return false;
    }
    setup_line(dst);
    setup_line("  installed");
    return true;
}

int main(int argc, char **argv)
{
    const app_launch_info_t *info = app_launch_info();
    bool ok = true;

    (void) argc;
    (void) argv;

    setup_line("MoniOS UEFI Setup");
    setup_line("-----------------");
    if (setup_file_exists("/INSTALL.FLG")) {
        setup_line("installer media: yes");
    } else {
        setup_line("installer media: no");
    }

    if (info == 0 || info->privilege_level > APP_PRIV_R2) {
        if (!app_request_r2("setup installs UEFI boot files")) {
            setup_line("R2 denied");
            return 1;
        }
    }

    if (!setup_mkdir("/EFI") || !setup_mkdir("/EFI/BOOT") || !setup_mkdir("/MONIOS")) {
        setup_line("target filesystem is read-only or not mounted");
        return 1;
    }

    ok = setup_copy_file("/BOOTX64.EFI", "/EFI/BOOT/BOOTX64.EFI") && ok;
    ok = setup_copy_file("/KERNEL.BIN", "/MONIOS/KERNEL.BIN") && ok;
    if (setup_file_exists("/INSTALL.TXT")) {
        ok = setup_copy_file("/INSTALL.TXT", "/MONIOS/INSTALL.TXT") && ok;
    }

    setup_line(ok ? "setup complete" : "setup completed with errors");
    return ok ? 0 : 1;
}
