# MoniOS App API

MoniOS user applications should include `appsys.h` and link with the user runtime objects from the Makefile. The runtime starts applications in the app address window (`0x04000000` to `0x04400000`) and passes an `app_launch_info_t` with argv, environment, cwd, user, privilege, and standard handles.

The stable app ABI is `APP_ABI_VERSION`. Rebuild apps when this value changes.

Core helpers:

- `app_launch_info()` returns process metadata.
- `app_ticks()`, `app_sleep_ticks()` and `app_log()` provide basic runtime services.
- `app_getcwd()`, `app_get_mouse()` and `app_get_system_status()` read system state.
- `app_file_read()`, `app_file_write()`, `app_file_size()`, `app_file_exists()`, `app_file_mkdir()`, `app_file_delete()` and `app_file_list_dir()` wrap filesystem syscalls.
- `app_graphics_fill_rect()` and `app_graphics_present()` provide a minimal graphics API.
- `app_socket_*()`, `app_ipc_*()`, `app_futex_*()` and `app_signal_*()` provide networking and coordination primitives.
- `app_registry_get()` and `app_registry_set()` read and write small system registry keys.
- `app_default_app_get()` and `app_default_app_set()` manage file-extension default apps, for example `.rzs -> /apps/rzsinst.elf`.
- `app_defer_exec()` asks the shell to run another app after the current elevated installer exits. It is intended for installer flows.

Default apps:

- The registry stores file associations under `assoc.<extension>`, for example `assoc.rzs`.
- `.rzs` defaults to `/apps/rzsinst.elf`; double-clicking an RZS package opens the installer first.
- `.wav` and `.m4a` default to `/apps/player.elf`.
- `.txt` defaults to `/apps/notepad.elf`.
- Shell commands `assoc .rzs`, `assoc .rzs /apps/rzsinst.elf`, `reg get <key>` and `reg set <key> <value>` are available for developers.

Privilege model:

- Normal apps run as `APP_PRIV_R3`.
- Elevated setup tools can request `APP_PRIV_R2` with `app_request_r2()`.
- Only signed driver packages should request `APP_PRIV_R0`; R0 code is trusted kernel extension code.

Example:

```c
#include "appsys.h"
#include "stdio.h"

int main(int argc, char **argv)
{
    char cwd[PATH_MAX_LEN];

    (void) argc;
    (void) argv;
    if (app_getcwd(cwd, sizeof(cwd)) >= 0) {
        fputs("cwd: ");
        fputs(cwd);
        fputs("\r\n");
    }
    app_log("hello from a user app");
    return 0;
}
```

Run `appdev` from the shell to exercise the API. Run `appdev badptr` to verify app fault isolation: the sample intentionally passes an invalid buffer to the kernel and should terminate only that process.
