# Debugging MoniOS with GDB

This document describes two ways to debug the MoniOS x64 kernel:

1. **In-kernel GDB stub over COM1** (kernel implements the GDB remote
   serial protocol itself).  Works with `make run_debug` output.
2. **QEMU's built-in gdbserver** (`-gdb tcp::1234 -S`), which lets GDB
   control QEMU at the hardware level.

The in-kernel stub lives in `kernel/debug/gdb_stub.c` and is wired into
IDT vectors 1 (#DB / single-step / hw breakpoints) and 3 (INT3 / sw
breakpoints).  It is **inactive by default**: if no host GDB speaks to it,
exceptions fall through to the normal BSOD path exactly as before.

---

## 1. Build

```sh
make -j2
```

Outputs: `out/kernel.exe`, `out/kernel.elf`, `hd.img`.

The stub object `out/gdb_stub.o` is already compiled into the kernel.

---

## 2. Option A — in-kernel stub over QEMU serial

The stub uses COM1 (`0x3F8`, 115200 8N1).  To give GDB a dedicated serial
socket instead of sharing stdio, launch QEMU like this (a `run_gdb_serial`
Makefile target equivalent):

```sh
qemu-system-x86_64 -monitor none -no-reboot -snapshot \
  -drive file=hd.img,format=raw \
  -serial tcp:127.0.0.1:1235,server,nowait
```

At boot the stub prints a banner on the serial and waits briefly (~1-2 s)
for a host GDB.  Connect within that window to stop at the **initial
breakpoint**; otherwise the kernel continues booting normally and you can
still attach later by causing an INT3 / #DB.

Connect GDB:

```sh
# either directly to QEMU's serial socket:
gdb -ex "set architecture i386:x86-64" \
    -ex "file out/kernel.elf" \
    -ex "target remote localhost:1235"

# or via the relay (lets you pin the GDB-facing port to 1234):
python tools/gdb_connect.py --qemu-port 1235 --gdb-port 1234 &
gdb -x .gdbinit        # .gdbinit already does: target remote localhost:1234
```

### Useful GDB commands

```
info registers            # dump rax..r15 rip rflags cs ss ...
p $rip / p $rax           # read a single register
set $rax = 0x1234         # write a single register
x/20xg $rsp               # hex dump the kernel stack
x/10xb main               # memory inspection
break *0x2001000+0x1234   # software (INT3) breakpoint
hbreak *kmain             # hardware breakpoint (DR0..DR3)
continue
stepi / nexti             # single-step (uses #DB)
bt                        # stack trace
```

The stub supports: `g/G`, `p/P`, `m/M` (with RLE), `c/s` (optional resume
address), `Z0/z0` (INT3 sw breakpoints, up to 16), `Z1..Z4` (DR0..DR3 hw
breakpoints/watchpoints), `?`, `k`, `qSupported`, `qAttached`, `qOffsets`,
`qSymbol::`, `vCont?`, `QStartNoAckMode`.

---

## 3. Option B — QEMU built-in gdbserver

No in-kernel stub required.  Start QEMU paused and expose a gdb port:

```sh
qemu-system-x86_64 -S -gdb tcp::1234 -no-reboot -drive file=hd.img,format=raw
```

Then from GDB:

```sh
gdb -ex "file out/kernel.elf" -ex "target remote localhost:1234"
```

This stops the CPU at the first instruction (the boot sector).  Use
`continue` to run, `break *0x2001000+...` etc.

---

## 4. Boot flow & ordering

`kernel_main()` calls, in order:

1. `serial_init()`        — COM1 up at 115200
2. `crash_dump_init()`    — re-inits COM1 for the crash logger
3. `gdb_stub_init()`      — re-inits COM1 for the stub, prints banner,
                            briefly waits for a GDB connection
4. `ftrace_init()`

Because the stub runs *after* serial init, the ordering is correct.

---

## 5. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| GDB connects but immediately gets garbage | Baud mismatch — stub is fixed at 115200. Make sure QEMU serial / pyserial uses 115200. |
| `Remote connection closed` right after connect | You missed the ~1-2 s boot wait. Rebuild/reset QEMU and connect faster, or add an intentional `int3` in early code. |
| `continue` makes the kernel triple-fault | You set a software breakpoint inside unmapped/early code before the IDT is up. Wait until after `init_idt` to use INT3 breakpoints. |
| Single-step doesn't stop | #DB must route through the stub; vector 1 is wired in `kernel_entry.asm`. Ensure `gdb_attached` is true (send `?` once). |
| Kernel still BSODs with no GDB prompt | That's expected: the stub only talks when a debugger is attached. Without GDB, exceptions go to `bsod_panic`. |

---

## 6. Panic memory dump (companion feature)

On a crash the kernel now writes two files to the FAT volume:

- `C:\PANIC.LOG`  — human-readable text (registers, stack trace, log tail)
- `C:\panic.dmp`   — binary physical-memory snapshot

Analyze the binary dump on the host with:

```sh
python tools/analyze_dump.py C:\panic.dmp                 # header summary
python tools/analyze_dump.py panic.dmp -s "some string"   # search pages
python tools/analyze_dump.py panic.dmp -x 0x2001000       # hexdump at phys addr
python tools/analyze_dump.py panic.dmp --strings          # printable strings
python tools/analyze_dump.py panic.dmp --extract ram.bin  # all pages -> raw
```

See the header in `include/memdump.h` for the on-disk format.
