# MoniOS x64 System Call Reference

Last updated: feature-20/23/29/30/33 compatibility & dev-tools drop.

## Calling convention

MoniOS uses a dedicated `syscall` instruction (see `kernel/arch/syscall_entry.asm`).
User mode passes:

| Register | Role |
|----------|------|
| `rax`    | system call number (`SYS_*`, see below) |
| `rbx`    | argument 0 (arg0) |
| `rcx`    | argument 1 (arg1) |
| `rdx`    | argument 2 (arg2) |

The return value is placed in `rax`.  Negative small values are `-errno`.
On return, `rcx`/`r11` are clobbered by the hardware `syscall`/`sysret` pair.

Files that take a path use Windows drive-letter paths (`C:\`, `D:\`, …);
there is no POSIX `VFS` abstraction — paths go straight to the fs layer.

---

## Table of contents

| #   | Name                   | Category        |
|-----|------------------------|-----------------|
| 0   | `SYS_EXIT`             | process         |
| 2   | `SYS_READ`             | file/IO         |
| 3   | `SYS_WRITE`            | file/IO         |
| 4   | `SYS_OPEN`             | file/IO         |
| 5   | `SYS_CLOSE`            | file/IO         |
| 6   | `SYS_EXEC`             | process         |
| 7   | `SYS_WAITPID`          | process         |
| 8   | `SYS_UNLINK`           | fs              |
| 10  | `SYS_MKDIR`            | fs              |
| 11  | `SYS_RMDIR`            | fs              |
| 12  | `SYS_CHDIR`            | fs              |
| 13  | `SYS_GETCWD`           | fs              |
| 16  | `SYS_BRK`              | memory          |
| 17  | `SYS_STAT`             | fs              |
| 18  | `SYS_LSEEK`            | file/IO         |
| 19  | `SYS_GETPID`           | process         |
| 20  | `SYS_MOUNT`            | fs              |
| 21  | `SYS_UMOUNT`           | fs              |
| 26–35 | network sockets      | net             |
| 36–38 | time                 | time            |
| 41–43 | dynamic loading       | dll             |
| 44–47 | clipboard/registry    | services        |
| 48–54 | process/thread/signal  | process        |
| 55  | `SYS_KLOG`             | debug           |
| 56–57 | fs extras             | fs              |
| 58–59 | net extras            | net             |
| 60–63 | UI                    | ui              |
| 64  | `SYS_DRIVER_QUERY`     | driver          |
| 65  | `SYS_USB_MOUNT_CTL`    | **USB (new)**   |
| 66  | `SYS_DOS_EXEC`         | **compat (new)**|
| 67  | `SYS_DEBUG_CTL`        | **debugger(new)**|
| 68  | `SYS_PROFILE_CTL`      | **profiler(new)**|
| 69  | `SYS_PACKAGE_CTL`      | **pkgmgr(new)** |

---

## 0–43 core kernel calls (pre-existing)

| #   | Name            | rbx (arg0) | rcx (arg1) | rdx (arg2) | Returns | Notes |
|-----|-----------------|------------|------------|------------|---------|-------|
| 0   | `EXIT`          | exit_code  | —          | —          | does not return | Terminate current thread/process. |
| 1   | `FORK`          | —          | —          | —          | unused | Reserved; returns `-1`. |
| 2   | `READ`          | fd         | buf ptr    | count      | bytes read / `-errno` | Read `count` bytes from open file descriptor `fd`. |
| 3   | `WRITE`         | fd         | buf ptr    | count      | bytes written / `-errno` | Write to `fd`. `fd==1` is the console. |
| 4   | `OPEN`          | path ptr   | flags      | mode       | fd / `-errno` | Open a Windows-style path. flags: read/write/create. |
| 5   | `CLOSE`         | fd         | —          | —          | 0 / `-errno` | Close a file descriptor. |
| 6   | `EXEC`          | path ptr   | argv ptr   | envp ptr   | pid / `-errno` | Load & run a PE/ELF image (see feature 23 DOS routing). |
| 7   | `WAITPID`       | pid        | status ptr | options    | pid / `-errno` | Wait for child exit. |
| 8   | `UNLINK`        | path ptr   | —          | —          | 0 / `-errno` | Delete a file. |
| 9   | `LINK`          | old ptr    | new ptr    | —          | 0 / `-errno` | (Reserved) hard link. |
| 10  | `MKDIR`         | path ptr   | mode       | —          | 0 / `-errno` | Create directory. |
| 11  | `RMDIR`         | path ptr   | —          | —          | 0 / `-errno` | Remove empty directory. |
| 12  | `CHDIR`         | path ptr   | —          | —          | 0 / `-errno` | Change working directory. |
| 13  | `GETCWD`        | buf ptr    | size       | —          | buf ptr / `-errno` | Read current directory. |
| 14  | `CHMOD`         | path ptr   | mode       | —          | 0 / `-errno` | (Reserved) change permissions. |
| 15  | `CHOWN`         | path ptr   | uid/gid    | —          | 0 / `-errno` | (Reserved). |
| 16  | `BRK`           | new_brk    | —          | —          | current brk | Heap break adjust. |
| 17  | `STAT`          | path ptr   | stat ptr   | —          | 0 / `-errno` | Stat a path into `struct stat`. |
| 18  | `LSEEK`         | fd         | offset     | whence     | new offset | Seek in a file. |
| 19  | `GETPID`        | —          | —          | —          | pid | Current process id. |
| 20  | `MOUNT`         | dev ptr    | target ptr | fstype ptr | 0 / `-errno` | Mount a block device onto a drive letter. |
| 21  | `UMOUNT`        | target ptr | —          | —          | 0 / `-errno` | Unmount. |
| 22  | `PIPE`          | fds[2] ptr | —          | —          | 0 / `-errno` | Create a pipe. |
| 23  | `DUP`           | fd         | —          | —          | new fd | Duplicate fd. |
| 24  | `DUP2`          | oldfd      | newfd      | —          | new fd | Duplicate to a specific fd. |
| 25  | `IOCTL`         | fd         | request    | arg ptr    | 0 / `-errno` | Device control. |
| 26  | `SOCKET`        | domain     | type       | protocol   | sockfd / `-errno` | Create socket. |
| 27  | `BIND`          | sockfd     | addr ptr   | addrlen    | 0 / `-errno` | Bind. |
| 28  | `CONNECT`       | sockfd     | addr ptr   | addrlen    | 0 / `-errno` | Connect to peer. |
| 29  | `LISTEN`        | sockfd     | backlog    | —          | 0 / `-errno` | Listen. |
| 30  | `ACCEPT`        | sockfd     | addr ptr   | addrlen ptr | new sockfd | Accept. |
| 31  | `SEND`          | sockfd     | buf ptr    | len        | bytes / `-errno` | Send on connected socket. |
| 32  | `RECV`          | sockfd     | buf ptr    | len        | bytes / `-errno` | Receive. |
| 33  | `SENDTO`        | sockfd     | buf ptr    | {len,addr} | bytes | Send to address. |
| 34  | `RECVFROM`      | sockfd     | buf ptr    | {len,addr} | bytes | Recv with source addr. |
| 35  | `SHUTDOWN`      | sockfd     | how        | —          | 0 / `-errno` | Shutdown. |
| 36  | `GETTIMEOFDAY`  | timeval ptr| —          | —          | 0 | Wall-clock time. |
| 37  | `NANOSLEEP`     | ns ptr     | rem ptr    | —          | 0 | Sleep. |
| 38  | `FTIME`         | timeb ptr  | —          | —          | 0 | (legacy) time. |
| 39  | `GETHOSTNAME`   | buf ptr    | len        | —          | 0 | Host name. |
| 40  | `SETHOSTNAME`   | name ptr   | len        | —          | 0 | Set host name. |
| 41  | `DL_OPEN`       | path ptr   | flags      | —          | handle / NULL | Load a `.dll`. |
| 42  | `DL_SYM`        | handle     | name ptr   | —          | addr / NULL | Resolve symbol. |
| 43  | `DL_CLOSE`      | handle     | —          | —          | 0 | Unload DLL. |

---

## 44–64 reserved service calls

These numbers are reserved in `include/syscall.h`; the dispatch layer is
being filled in by other feature groups.

| #   | Name              | Purpose |
|-----|-------------------|---------|
| 44  | `CLIPBOARD_SET`   | Copy text to the system clipboard. |
| 45  | `CLIPBOARD_GET`   | Read text from the clipboard. |
| 46  | `REGISTRY_SET`    | Write a registry value (kernel/debug/registry.c). |
| 47  | `REGISTRY_GET`    | Read a registry value. |
| 48  | `PROCESS_CREATE`  | Create a process with explicit attributes. |
| 49  | `PROCESS_EXIT_QUERY` | Query a process's exit status. |
| 50  | `THREAD_CREATE`   | Create a thread. |
| 51  | `THREAD_EXIT`     | Exit the current thread. |
| 52  | `SIGNAL_SEND`     | Send a signal to a pid. |
| 53  | `SIGNAL_HANDLE`   | Register a signal handler. |
| 54  | `ALARM`           | Schedule an alarm signal. |
| 55  | `KLOG`            | Kernel log read/control. |
| 56  | `VFS_STATFS`      | Filesystem statistics. |
| 57  | `VFS_TRUNCATE`    | Truncate a file. |
| 58  | `NET_RESOLVE`     | DNS resolve a name. |
| 59  | `NET_BIND_DEV`    | Bind a socket to a NIC. |
| 60  | `UI_MESSAGE`      | Post a UI message. |
| 61  | `UI_TIMER`        | Arm a UI timer. |
| 62  | `UI_PAINT_BEGIN`  | Begin a paint batch. |
| 63  | `UI_PAINT_END`    | End a paint batch. |
| 64  | `DRIVER_QUERY`    | Query loaded driver info. |

---

## 65 `SYS_USB_MOUNT_CTL` — USB mount control (feature 20)

Implemented in `drivers/usb/usb_ext.c` as `uint64_t usb_mount_ctl(sub, arg1, arg2)`.
Dispatch case **not** added to `syscall.c` yet (per group contract).

| rbx sub | Name               | rcx (arg1)      | rdx (arg2)            | Returns |
|---------|--------------------|-----------------|-----------------------|---------|
| 0 | `ENUM_MSC`     | —               | —                     | # of mass-storage devices present |
| 1 | `MOUNT_ALL`    | —               | —                     | # newly mounted (drive letters D:, E:, …) |
| 2 | `EJECT`        | drive letter    | —                     | 0 / `-1` |
| 3 | `GET_DRIVE`    | MSC index       | —                     | drive letter char (e.g. `'D'`), or 0 |
| 4 | `ENUM_PRINTER` | —               | —                     | # of USB printers |
| 5 | `PRINTER_STATUS`| printer index  | —                     | port-status byte (0x18 = selected/online) |
| 6 | `PRINTER_WRITE`| printer index   | ptr to `{void*data, uint32_t len}` | bytes accepted / `-errno` |

U 盘 insertion calls `usbdev_hotplug()` from the roothub layer; the first
free letter starts at `D:` (`A:`/`B:` reserved, `C:` is the system volume).
Block-device integration: `blockdev.c` (perf group) iterates
`usb_msc_count()` / `usb_msc_get(i)` and registers a `blockdev_t` per mounted
MSC fronted by a USB Bulk-Only transfer.

## 66 `SYS_DOS_EXEC` — run a 16-bit DOS program (feature 23)

| rbx (arg0) | rcx (arg1) | rdx (arg2) | Returns |
|------------|------------|------------|---------|
| path ptr (`C:\...\HELLO.COM`) | reserved | reserved | exit code / `-errno` |

Implemented in `kernel/compat/dos.c`. The loader (`exec.c`) automatically
routes any MZ file that lacks a `PE\0\0` signature to the NTVDM-style layer.
The interpreter supports INT 21h functions 01h/02h/09h/40h/4Ch and INT 20h,
plus COM loading at `0x0100`.

## 67 `SYS_DEBUG_CTL` — kernel debugger (feature 29)

Implemented in `kernel/debug/gdb_stub.c` as `debug_ctl(op, arg1, arg2)`.

| rbx op | Name            | rcx arg1        | rdx arg2 | Returns |
|--------|-----------------|-----------------|----------|---------|
| 0  | `SWBP_SET`      | address         | —        | bp id (≥0) / `-1` |
| 1  | `SWBP_CLEAR`    | address         | —        | 0 / `-1` |
| 2  | `SWBP_CLEAR_ALL`| —               | —        | count cleared |
| 3  | `HWBP_SET`      | address         | type(0 exec,1 write,2 rw) | slot 0..3 / `-1` |
| 4  | `HWBP_CLEAR`    | slot            | —        | 0 / `-1` |
| 5  | `STEP_ON`       | —               | —        | arms RFLAGS.TF |
| 6  | `CONTINUE`      | —               | —        | disarms single-step |
| 7  | `READ_REGS`     | user buf (192 B)| —        | # regs (24) |
| 8  | `READ_MEM`      | address         | ptr `{buf,len}` | bytes |
| 9  | `WRITE_MEM`     | address         | ptr `{buf,len}` | bytes |
| 10 | `STATUS`        | —               | —        | bitmap: swbps, hwbps, step |

Hardware breakpoints are programmed into DR0–DR3/DR7. The GDB stub speaks
RSP over COM1 (115200 8N1) and supports `Z0`–`Z4` (sw + hw breakpoints /
watchpoints), `g`/`G` (registers), `m`/`M` (memory), `s`/`vCont;s` (step).

## 68 `SYS_PROFILE_CTL` — performance profiler (feature 30)

Implemented in `kernel/debug/ftrace.c` as `profile_ctl(op, arg1, arg2)`.

| rbx op | Name          | rcx arg1        | Returns |
|--------|---------------|-----------------|---------|
| 0  | `START`     | —            | reset & begin (rdtsc timing + shadow call stack) |
| 1  | `STOP`      | —            | stop aggregation |
| 2  | `RESET`     | —            | zero counters |
| 3  | `REPORT`    | —            | print sorted report to serial |
| 4  | `GET_COUNT` | func addr    | call count / `-1` |
| 5  | `GET_CYCLES`| func addr    | total on-CPU cycles / `-1` |
| 6  | `SAMPLE_ON` | —            | timer-PC sampling on |
| 7  | `SAMPLE_OFF`| —            | sampling off |
| 8  | `STATUS`    | —            | bit0=on, bit1=sampling |

## 69 `SYS_PACKAGE_CTL` — package manager (feature 33)

Implemented in `user/apps/pkgmgr.c` (user app; kernel side not yet wired).

| rbx sub | Name        | arg1            | arg2           | Returns |
|---------|-------------|-----------------|----------------|---------|
| 0 | `INSTALL`  | package name    | —              | 0 / `-errno` |
| 1 | `UPDATE`    | package name    | —              | 0 / `-errno` |
| 2 | `UNINSTALL`| package name    | —              | 0 / `-errno` |
| 3 | `LIST`     | buf ptr         | cap            | count listed |
| 4 | `QUERY`    | package name    | info ptr       | 0 / `-1` |
| 5 | `REPO_SET` | url ptr         | —              | 0 |

Packages are tar-style archives with a `manifest`, installed under
`C:\Monios\Apps\<name>\`, registered in
`C:\Monios\System\Config\packages.db`, and fetched from the repo in
`C:\Monios\System\Config\pkgrepo.cfg` via `kernel/net/http.c`.
