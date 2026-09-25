# Task 23 & 24 — GDB Remote Debugging + Panic Memory Dump

## 概述

- **Task 23**: 完善内核内置 GDB remote stub（COM1 串口），补全协议命令、把 #DB/INT3 接入完整寄存器上下文、加启动期等待连接。
- **Task 24**: 在已有 `C:\PANIC.LOG` 文本日志之外，新增二进制物理内存转储 `C:\panic.dmp` 及离线分析工具。

---

## Task 23 — GDB 远程调试

### 改动文件

| 文件 | 改动 |
|---|---|
| `include/gdb_stub.h` | 重写：新的 3 参异常入口 `gdb_stub_handle_exception(frame, vector, error_code)`，说明帧布局，新增 `gdb_stub_is_attached()`。 |
| `kernel/debug/gdb_stub.c` | 大幅补全（见下）。 |
| `kernel/arch/kernel_entry.asm` | 新增 `GDB_EXCEPTION_ENTRY` 宏，向量 1（#DB）与 3（INT3）都改为：push 全部 15 个 GPR → 调 stub → 返回 1 则 `iretq` 恢复，返回 0 则弹栈并链入 `exception_common`（无 gdb 时走原 BSOD 路径，行为不变）。 |

### 补全的协议命令

- 寄存器：原有 `g/G`；新增 **`p n`**（读单寄存器）、**`P n=v`**（写单寄存器），按 x86_64 GDB 约定 24 个寄存器（16 GPR + rsp/rip + eflags + cs/ss/ds/es/fs/gs）。
- 内存：原有 `m/M`；新增 **RLE**——读内存时对 ≥3 字节相同的连续块按 `*<char><NN>` 压缩发出；写内存/参数解析时支持解 RLE。
- 控制：`c [addr]` / `s [addr]` 现解析可选的恢复地址；新增 `vCont?`/`vCont`、`H`、`T`、`qC`、`qfThreadInfo`/`qsThreadInfo`、`QStartNoAckMode`、`D`、`!`。
- 查询：`qSupported`、**`qAttached`（回 1）**、`qOffsets`、`qSymbol::`、`qOffsets`。
- 断点：`Z0/z0` INT3 软断点（最多 16 个）、`Z1..Z4` 硬件断点（DR0–DR3/DR7，最多 4 个），断点命中时正确回退 rip。

### 异常/中断接入

- 异常入口先把全部 GPR 压栈，C 侧从栈帧读取真实寄存器（不再用调用前被破坏的寄存器快照），正确传入 rip/cs/rflags。
- **只有在 `gdb_attached == true`（收到过主机数据包）时才进入协议循环**；否则立刻返回 0，交回原有 `cpu_exception_dispatch` → BSOD，不破坏崩溃处理流程。
- 单步通过在恢复的 rflags 上置 TF 实现，#DB（向量 1）回传到 stub。

### 串口 / 启动等待

- stub 用 COM1（0x3F8，115200 8N1）；初始化顺序在 `kernel_main` 中为 `serial_init → crash_dump_init → gdb_stub_init`，串口先于 stub。
- `$packet#checksum` 封包、`+/-` ACK、NAK 重发均已实现。
- `gdb_stub_init()` 打印 banner 后用带超时的忙等轮询最多约 1–2 秒等 gdb；超时则继续启动（不无限阻塞）。gdb 在窗口内连入即在早期停住（初始断点）。

### 工具与文档（新增）

- `tools/gdb_connect.py`：在 QEMU 串口 TCP socket 与本地 gdb 端口之间转发（也支持 pyserial 真实 COM 口）。
- `.gdbinit`：加载 `out/kernel.elf`、`target remote localhost:1234` 模板。
- `docs/gdb_debugging.md`：两种调试方式（内核 stub / QEMU `-gdb tcp::1234 -S`）、常用 gdb 命令、排错表。

---

## Task 24 — panic 内存转储

### 改动/新增文件

| 文件 | 说明 |
|---|---|
| `include/memdump.h`（新增） | `memdump_header_t`（魔数 `MONIPDMP`、版本、崩溃向量/错误码、GPR/rip/rsp/rflags/cr2/cr3、内核位置、页数、flags）+ `memdump_page_record_t {u64 phys; u8 data[4096]}`。 |
| `kernel/debug/memdump.c`（新增） | 在静态 2MB BSS  staging 中组装转储，再一次性 `fat16_write_file("/panic.dmp", ...)`。只收集：低 1MB（IVT/BDA）、物理页位图中已分配/保留的帧、内核映像区；扫描上限 256MB，staging 满则置 TRUNCATED 标志。 |
| `include/frame.h` / `kernel/mm/frame.c` | 新增只读访问器 `frame_base_phys()`、`frame_total_frames()`、`frame_is_used(phys)`（崩溃上下文安全，无堆分配）。 |
| `kernel/sched/bsod.c` | `bsod_panic` 与 `bsod_exception_panic` 在写完 `PANIC.LOG` 后调用 `memdump_write()`，并在蓝屏上显示 "writing memory dump..." 进度。 |
| `kernel/arch/kernel.c` | boot 中 `crash_dump_init()` 之后调用 `memdump_init()`。 |
| `tools/analyze_dump.py`（新增） | 解析 panic.dmp：打印头/寄存器、按物理地址 hexdump、全文搜索字符串、导出字符串列表、把页导出为原始 bin 供 gdb `core-file` 用。 |

### 与现有崩溃日志的关系

- `C:\PANIC.LOG`（文本）保持不变：寄存器、栈回溯、日志尾部。
- `C:\panic.dmp`（二进制）新增：先写文本日志，再写二进制转储（中断关闭、静态缓冲、无堆分配）。
- 转储失败/磁盘不足仅记录串口提示，不影响原有蓝屏与重启流程。

### 已验证

- 用合成 dmp 自测 `analyze_dump.py`：头解析、`-x` hexdump、`-s` 搜索、`--extract` 导出均正确。

---

## 需要的 Makefile 改动（按要求未提交，仅报告）

工作区 `Makefile` 的 `KERNEL_DEBUG_OBJS` 末尾需要加入 `out/memdump.o`：

```make
KERNEL_DEBUG_OBJS = out/registry.o out/smbus.o out/intelbus1.o out/amdbus1.o \
                    out/gdb_stub.o out/crash_dump.o out/memdump.o out/ftrace.o
```

`kernel/debug/memdump.c` 会被已有的通用规则 `out/%.o : kernel/debug/%.c` 自动编译，无需新增规则。

> 备注：验证链接时还发现工作区 Makefile 的链接行缺少 `out/profiler.o`（`kernel/debug/profiler.c`）与 `out/memstats.o`（`kernel/mm/memstats.c`），但 `kernel.c`/`syscall.c` 引用了它们，导致裸链接报 undefined。这是工作区既有状态（与本任务无关），临时加入后才能完成链接验证；请一并确认这两个对象是否应各自加入 `KERNEL_DEBUG_OBJS` / `KERNEL_MEM_OBJS`。

## 构建验证

- 工具链：`x86_64-elf-gcc 7.1.0` + `nasm 3.01`。
- `out/gdb_stub.o`、`out/memdump.o`、`out/frame.o`、`out/crash_dump.o`、`out/bsod.o`、`out/kernel_entry.o` 全部零警告编译通过。
- 完整 `make -j2` 退出码 0，产出 `out/kernel.exe`（1,126,096 B）与 `hd.img`（103,809,024 B）。
- 新符号 `gdb_stub_handle_exception`、`memdump_write`、`memdump_init`、`frame_is_used` 均已进入目标文件。

## 未做 / 限制

- 未做 QEMU 实机启动验证（仅编译+链接+宿主侧 dmp 解析自测）；GDB 交互行为建议按 `docs/gdb_debugging.md` 实跑确认。
- 转储 staging 为静态 2MB，超出部分截断并在 header 置位（可调 `DUMP_STAGING_SIZE`）。
