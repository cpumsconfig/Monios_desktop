# MoniOS App API

MoniOS user applications should include `appsys.h` and link with the user runtime objects plus `console.dll`, `windows.dll`, and `monios.dll` import libraries from the Makefile. User images are PE32+ files (`.exe`, `.dll`, `.sys`). The runtime starts applications in the app address window (`0x04000000` to `0x04400000`) and passes an `app_launch_info_t` with argv, environment, cwd, user, privilege, subsystem, image flags, and standard handles.

The stable app ABI is `APP_ABI_VERSION` (currently `5`). Rebuild apps when this value changes.

Core helpers:

- `app_launch_info()` returns process metadata.
- `stdio.h` provides Console-backed `putchar()`, `puts()`, `printf()` and `vprintf()`.
- `console_set_title()` changes the current Console process window title.
- `app_ticks()`, `app_sleep_ticks()` and `app_log()` provide basic runtime services.
- `app_getcwd()`, `app_get_mouse()` and `app_get_system_status()` read system state.
- `app_file_read()`, `app_file_write()`, `app_file_size()`, `app_file_exists()`, `app_file_mkdir()`, `app_file_delete()` and `app_file_list_dir()` wrap filesystem calls through `monios.dll`.
- `app_graphics_fill_rect()` and `app_graphics_present()` provide a minimal graphics API.
- `app_socket_*()`, `app_ipc_*()`, `app_futex_*()` and `app_signal_*()` provide networking and coordination primitives.
- `app_registry_get()` and `app_registry_set()` read and write small system registry keys.
- `app_default_app_get()` and `app_default_app_set()` manage file-extension default apps, for example `.sys -> C:\Monios\Apps\sysinst.exe`.
- `app_defer_exec()` asks the shell to run another app after the current elevated installer exits. It is intended for app installers, not kernel drivers.

Default apps:

- The registry stores file associations under `assoc.<extension>`, for example `assoc.sys`.
- `.sys` defaults to `C:\Monios\Apps\sysinst.exe`; double-clicking a driver opens the installer first.
- `.wav` and `.m4a` default to `C:\Monios\Apps\player.exe`.
- `.txt` defaults to `C:\Monios\Apps\notepad.exe`.
- Shell commands `assoc .sys`, `assoc .sys C:\Monios\Apps\sysinst.exe`, `reg get <key>` and `reg set <key> <value>` are available for developers.

Driver packages:

- `.sys` files are PE32+ AMD64 Native subsystem images and are not launched as normal apps.
- Build output signs drivers with `tools/sign_driver_sys.py`, using `D:/qm/makecert.exe` and `D:/qm/signtool.exe` to create an Authenticode-style PE Security Directory.
- Signed drivers are stored under `C:\Monios\driver`, for example `C:\Monios\driver\rzdrv.sys`; they are no longer copied into `C:\Monios\Apps`.
- The standalone kernel image is the signed PE32+ Native image `C:\Monios\kernel.exe`. Boot driver loading is controlled by `drivers.boot.count` and `drivers.boot.N` registry keys. The default boot driver is `C:\Monios\driver\rzdrv.sys`.
- The kernel verifies the `.sys` PE type, `WIN_CERTIFICATE` table, and Authenticode SHA-256 digest before registering a boot driver. Missing, malformed, tampered, or unsigned drivers call the BSOD path.
- The kernel verifies its own PE signature before boot-driver registration and compares the signer id with every registered boot `.sys`; an invalid kernel signature or signer mismatch calls the BSOD path.

PE subsystems:

- `APP_SUBSYSTEM_WINDOWS` marks windowed apps.
- `APP_SUBSYSTEM_CONSOLE` marks terminal apps.
- `APP_SUBSYSTEM_NATIVE` marks native system images such as `.sys` drivers.
- `APP_IMAGE_FLAG_CERT_PRESENT` is set when the PE Security Directory is present; full certificate validation is reserved for the signing phase.
- `APP_IMAGE_FLAG_RESOURCE_TABLE`, `APP_IMAGE_FLAG_ICON_RESOURCE`, and `APP_IMAGE_FLAG_MANIFEST_RESOURCE` are set when the kernel PE loader validates a resource table and finds icon or manifest resources. Manifests containing `requireAdministrator` or `highestAvailable` map to `APP_IMAGE_FLAG_NEEDS_R2`.
- `setup.exe` and `sysinst.exe` are elevated tools and request R2/UAC before launch. Signed `.sys` files are driver packages, not user processes; they must be installed or loaded by driver-management software.

DLL layers:

- `monios.dll` is the lowest user-mode DLL under `C:\Monios\System\Lib` and exposes raw syscall, handle, file, and process helpers.
- `console.dll` is the console layer and imports `monios.dll` for stdin/stdout/stderr helpers.
- `windows.dll` is the window/graphics layer and imports `monios.dll` for graphics syscalls.
- The kernel PE loader binds each app's Import Directory before entry, loads these DLLs from `C:\Monios\System\Lib`, and patches the IAT by exported symbol name.

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
`appdev` also prints the current image flags, including resource table, icon resource, manifest resource, and R2/UAC markers.

SMP status is exposed through the system status API and the `smp` shell command. It separates logical processors, firmware-enumerated processors, firmware-enabled processors, and processors currently online. The current kernel enumerates ACPI MADT topology while AP startup and cross-CPU scheduling remain pending.

## Console SDK

The Console application layer is provided by `console.dll`. The user runtime routes
`stdio.h` output to the process stdout handle, so a normal C program can use
`puts()` or `printf()` without calling kernel Console functions directly.

The repository includes MoniOS compiler and linker drivers:

```text
tools/monios-gcc.py
tools/monios-ld.py
```

Build the Hello World sample manually:

```bash
make app-runtime
python tools/monios-gcc.py -c examples/hello.c -o out/hello.o
python tools/monios-ld.py --subsystem console -o out/hello.exe out/hello.o
```

The linker driver adds the MoniOS startup object, user runtime objects,
`console.dll`, `windows.dll`, and `monios.dll`. The normal project target is:

```bash
make hello
make hd.img
```

Then run it from the MoniOS shell:

```text
run C:\Monios\Apps\hello.exe
```

On Windows, `tools/monios-gcc.cmd` and `tools/monios-ld.cmd` are command
wrappers for the same Python drivers. The linker accepts `console`, `windows`,
and `native` through `--subsystem`; `examples/hello.c` uses the Console path.
