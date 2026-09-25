# MoniOS x64 — Compatibility & Developer Tools Group 交付报告

覆盖功能 20 / 23 / 29 / 30 / 31 / 32 / 33。
构建：`make -j2 out/kernel.exe` —— **链接+签名成功**（`Successfully signed: out\kernel.exe`）。

---

## 一、文件清单

### 修改（已在构建中，重新编译并入内核）
| 文件 | 功能 | 说明 |
|------|------|------|
| `drivers/usb/usb_ext.c` | 20 | MSC 设备表、盘符自动分配、打印机基础、`usb_mount_ctl()` |
| `include/usb_ext.h` | 20 | 新增 MSC/打印机记录结构、类码、`SYS_USB_MOUNT_CTL` 子命令 |
| `kernel/debug/gdb_stub.c` | 29 | 多软件断点表、DR0–DR3 硬件断点、TF 单步、停止原因、`debug_ctl()` |
| `include/gdb_stub.h` | 29 | 新增断点计数 API、`debug_ctl()` 与子命令常量 |
| `kernel/debug/ftrace.c` | 30 | 每函数计数/RDTSC 耗时/影子调用栈/采样/排序报告、`profile_ctl()` |
| `include/ftrace.h` | 30 | 新增 `ftrace_profile_t`、采样/控制 API |
| `kernel/syscall/exec.c` | 23 | `EXEC_IMAGE_FORMAT_DOS` 枚举 + 默认 NULL 的 DOS 路由钩子 |

### 新建（不在 Makefile 中，见第三节“需追加的构建行”）
| 文件 | 功能 |
|------|------|
| `kernel/compat/dos.c` | 23 NTVDM 风格 16 位解释器（MZ/COM、INT 21h 子集） |
| `include/dos_compat.h` | 23 兼容层 ABI |
| `docs/syscall_reference.md` | 31 全部 0–69 系统调用文档 |
| `examples/graphics_demo.c` | 32 矩形/线条/文字绘图 |
| `examples/net_demo.c` | 32 HTTP GET + UDP |
| `examples/file_demo.c` | 32 建目录/读写/列目录 |
| `user/apps/pkgmgr.c` | 33 包管理器（install/update/remove/list/query） |

未触碰：`syscall.c` dispatch、Makefile、`graphics.c`/`shell.c`/`file.c`/`fs/**`。

---

## 二、实现要点

### 20 — USB MSC 自动挂载 + 打印机
- `usbdev_hotplug(slot, devclass, ifclass, vid, pid, attached)` 按 USB-IF 类码分流：
  `0x08`→MSC、`0x07`→打印机、`0x03`→HID、`0x09`→Hub。
- MSC 表 `g_msc[8]`，插入即记录；`usb_msc_mount_all()` 按 **D:、E:…** 顺序分配盘符
  （A:/B: 保留软驱，C: 系统盘）。`usb_msc_eject(letter)` 卸载。
- 打印机：`usb_printer_poll_status()` 发 `GET_PORT_STATUS` 类控制请求；
  `usb_printer_write()` 写 bulk-out。
- **块设备集成点（blockdev.c 归性能组）**：blockdev 初始化时遍历
  `usb_msc_count()` / `usb_msc_get(i)`，对每个已挂载 MSC 构造 `blockdev_t`，
  其 read/write 扇区函数前端接 USB BBB Bulk-Only 传输。本文件只暴露访问器，
  不直接调用 blockdev.c，避免跨组依赖。
- `usb_mount_ctl(sub,arg1,arg2)` 实现 6 个子命令（见 syscall_reference 65）。

### 23 — DOS 兼容层（NTVDM 风格）
- 长模式无 VM86，故实现**最小 16 位指令解释器**（mov/push/pop/int/jmp/call/ret/loop/nop/hlt）。
- 解析 MZ 头：`e_cparhdr`、`e_cs/e_ip`、`e_ss/e_sp`，并应用段重定位表；
  COM 文件载入 `0x0100`、PSP 建在 `0x0000`。
- INT 21h 子集：`01h` 读键、`02h` 显字符、`09h` `$` 串、`40h` 写句柄(stdout)、`4Ch` 退出；INT 20h 退出。
- **exec.c 集成**：`exec_prepare_image()` 在“无 PE 签名”分支后，先查
  `exec_is_pure_dos_mz()`（MZ 魔数 + 无 `PE\0\0`），若钩子 `g_dos_compat_run` 非空则
  标记 `EXEC_IMAGE_FORMAT_DOS` 并接受。**钩子默认 NULL**，未链接 dos.o 时行为与原来逐字节一致。

### 29 — 调试器
- **软件断点**：`g_sw_bp[16]` 表（地址 + 保存字节），Z0/z0 插入/移除 INT3(0xCC)；
  命中时 rip 回退 1，恢复前 unplug/恢复后 replug。
- **硬件断点**：`g_hw_bp[4]` → DR0–DR3，重建 DR7（RWn：执行/写/读写）。
- **单步**：`gdb_step_pending` 在 `s`/`vCont;s` 时置位，异常返回前 `pushfq; or $0x100; popfq` 置 TF。
- GDB 协议：`Z0–Z4`、`g/G`、`m/M`、`vCont`、T 停止回复带 `swbreak:/hwbreak:`。
- `debug_ctl(op,…)` 暴露 11 个子命令（断点/单步/读写寄存器/读写内存/状态）。

### 30 — 性能分析器
- 在 ftrace 环形缓冲基础上加 `g_profile[]`：每函数 call_count、total_cycles、max_cycles、sample_count。
- 影子调用栈 `g_fstack[64]`：entry 压 TSC，exit 弹栈累加 RDTSC 周期。
- `ftrace_sample_pc(pc)` 供定时器中断采样 PC。
- `ftrace_profile_report()` 按总周期选择排序后输出 Top-40。
- `profile_ctl(op,…)`：start/stop/reset/report/get-count/get-cycles/sample-on-off/status。

### 31 — 系统调用文档
- `docs/syscall_reference.md`：调用约定（rax=号，rbx/rcx/rdx=arg0/1/2），
  0–43 现有调用逐表列出参数与返回值，44–64 保留项，65–69 新增项详细子命令表。

### 32 — 示例程序集
- `graphics_demo.c`：进图形模式、填矩形、细线画横竖线、文字、present。
- `net_demo.c`：打印网络状态、`app_http_get_url()`、UDP sendto/recvfrom。
- `file_demo.c`：mkdir/write/read/list_dir/delete 全流程。
- 均基于 `user/lib/appsys.h` 的 `app_graphics_*` / `app_http_get_url` / `app_file_*`。

### 33 — 包管理器
- `user/apps/pkgmgr.c`：`list/install/update/remove/query`。
- 包=tar 风格（manifest 行 + payload）；仓库 URL 在
  `C:\Monios\System\Config\pkgrepo.cfg`；装到 `C:\Monios\Apps\<name>\`；
  已装库 `C:\Monios\System\Config\packages.db`（制表符分隔记录）。
- 用 `app_http_get_url()` 下载，`app_file_*` 落盘/注销。

---

## 三、需追加的 Makefile 行（按约束未修改 Makefile）

新文件要进内核/镜像时，追加（性能/集成组接入）：

```make
# kernel/compat/dos.o 加入 KERNEL_OBJS，并加规则：
out/%.o : kernel/compat/%.c
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c $< -o $@

# user/apps/pkgmgr.c 已符合 out/app_%.pe.o : user/apps/%.c，加入 APP_USER_OBJS 即可。
# examples/*.c 不进镜像，单独用 mingw 编译（见各文件头注释）。
```

接入后在 `exec.c` 启动早期调用：
```c
extern void dos_compat_init(void);
extern void exec_set_dos_entry(int(*)(const uint8_t*,uint32_t,int32_t*));
exec_set_dos_entry(dos_exec_image);   /* 打开 DOS 路由 */
```
否则钩子保持 NULL，DOS 文件按原逻辑被拒（无回归）。

---

## 四、验证方法

1. **构建**：`make -j2 out/kernel.exe` → ld 链接 + `Successfully signed`，退出码 0。
2. **单文件编译**：`usb_ext.c`/`gdb_stub.c`/`ftrace.c`/`exec.c` 均用内核 CFLAGS 独立编译 EXIT=0；
   `dos.c`、`pkgmgr.c`、示例用对应工具链 `-c` 语法检查通过。
3. **运行时验证（待真机/模拟器）**：
   - U 盘插入 → `usb_mount_ctl(1)` 返回 1，盘符 D:；`(3,'D')` 返回 `'D'`。
   - 串口 `target remote \\.\COM1`，GDB `break main` → `Z0`，`continue`→命中 T05 swbreak。
   - `profile_ctl(0)` 跑一段后 `profile_ctl(3)` 看排序表。
   - `pkgmgr list` 列出 packages.db。
4. **回归**：未改 fs/file/graphics/shell，exec.c 改动由 NULL 门控，预期不影响 119 项 VFS 测试。
