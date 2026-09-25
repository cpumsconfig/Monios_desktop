# Monios x64 冒烟测试报告 (SMOKE_REPORT.md)

测试日期：2026-09-24
测试环境：Windows，MinGW gcc 15.2.0，QEMU `qemu-system-x86_64`（`C:\Program Files\qemu\`），Python 3。
镜像：`hd.img`（raw），内核 `out/kernel.exe`。

---

## 一、Host 端回归测试（纯逻辑，无需内核环境）

入口：`tests/build_all_tests.bat`（一次性编译并运行全部 4 个套件）。
每个套件均通过 `#include` 真实内核源码（同一编译单元以访问 `static` 符号），依赖由
`tests/stubs.c`（既有，未改动）与新增 `tests/stubs_ext.c` 提供桩。编译统一带 `-D__HOST_TEST__`。

| 套件 | 文件 | 用例数 | 结果 |
|------|------|-------:|------|
| VFS 回归（既有） | `test_vfs.c` | 119 | PASS |
| 网络协议 | `test_net.c` | 84 | PASS |
| 文件系统 (FAT32/GPT/EXT2/NTFS) | `test_fs.c` | 76 | PASS |
| GUI / 窗口管理 | `test_gui.c` | 54 | PASS |
| **合计** | | **333** | **全部通过** |

```
[1/4] test_vfs   RESULT: 119 passed, 0 failed
[2/4] test_net   RESULT:  84 passed, 0 failed
[3/4] test_fs    RESULT:  76 passed, 0 failed
[4/4] test_gui   RESULT:  54 passed, 0 failed
ALL HOST SUITES PASSED
```

既有 `cd tests && build_test.bat`（VFS）仍为 119/119，无回归。

---

## 二、E2E 启动冒烟（tests/smoke_test.py）

QEMU 命令（对齐 `Makefile` 的 `run_debug`）：
```
qemu-system-x86_64 -monitor none -serial stdio -display none -m 256M \
  -netdev user,id=net0 -device e1000,netdev=net0 \
  -device isa-debug-exit,iobase=0x501,iosize=0x02 \
  -drive file=hd.img,format=raw -no-reboot
```

期望里程碑（来自 `kernel/arch/kernel.c`）：
1. 内核启动标志 —— `KERNEL BOOT`
2. 内存/MMU 初始化 —— `init paging` / `mmu:`
3. 文件系统挂载 —— `boot: filesystem mounted`（C: 盘可用）
4. Shell/UI 启动 —— `boot: init shell`

### 实测结果

| 里程碑 | 结果 |
|--------|------|
| 1. 内核 sign-on（KERNEL BOOT） | ✅ PASS |
| 2. 内存/MMU 初始化（mmu: install cr3 / post-cr3 continue） | ✅ PASS |
| 3. 文件系统挂载（filesystem mounted） | ❌ 未到达 |
| 4. Shell / UI 启动（init shell） | ❌ 未到达 |

**结论：FAIL —— 内核在引导中途崩溃，仅到达 2/4 里程碑。**

崩溃现场（串口 crash dump）：
```
[boot: start startup animation]
MONIOS KERNEL CRASH DUMP
RIP=0x0000000002045CE8  vector=0x0000000000000006  error_code=0x0
CR0=0x80000011 CR3=0x020BC000
ERROR TEXT: invalid opcode   (#UD, 异常 6)
```
崩溃位置：`kernel/arch/kernel.c:766-769`，紧随 `boot: start startup animation` 之后、
`boot: detect cpu` / `cpu_enable_fpu_sse()` 之前，即 `graphics_set_boot_animation_mode()`
→ `graphics_enter_mode()` → `graphics_boot_animation()` 图形初始化阶段。

### 已知限制 / 阻塞项
- 这是**内核侧**在当前 QEMU 配置下的真实崩溃（#UD），不是测试脚本缺陷。脚本已正确
  启动内核、捕获里程碑并识别崩溃转储。
- 已尝试的规避（均未改变结果）：
  - 去掉 e1000 网卡 → 更早崩溃；
  - 加 `-display none` / 用默认显示（弹窗）→ 同样崩于 startup animation；
  - `-cpu host` → QEMU 报错 `CPU model 'host' requires KVM or HVF`，本机不可用。
- 怀疑方向（供内核修复参考）：`graphics_enter_mode()` 在 `cpu_enable_fpu_sse()`
  之前执行，可能在 FPU/SSE 尚未使能时触发了 SSE 指令 → #UD；或 VBE/帧缓冲探测在
  此 QEMU 版本上落到了非法取指。修复后本脚本无需改动即可自动转 PASS。
- COM1 串口是 **GDB stub**（见启动横幅 `GDB stub active (COM1 115200)`），
  当前没有交互式串口 shell，因此无法从 host 向 shell 下发命令。

---

## 三、文件持久化测试（tests/persistence_test.py + user/apps/smoke_test.c）

设计：
- 理想流程：两次启动 QEMU，第一次在 shell 里 `echo ... > C:\smoke_test.txt`，
  第二次启动验证文件仍在。
- 因 COM1 为 GDB stub、无串口 shell，无法从 host 驱动 shell；按任务约定改为提供
  **内核侧测试程序** `user/apps/smoke_test.c`：写 `C:\smoke_test.txt` → 读回校验 →
  在串口/日志打印 `SMOKE_PERSIST_RESULT=PASS/=FAIL`。`persistence_test.py` 启动两次
  并在串口日志中匹配该标记。

实测结果：**SKIP（环境限制，退出码 0）**
```
[boot 1] shell milestone seen : False, command sent to serial: False
[boot 2] ...
RESULT: SKIP - no persistence marker observed.
```
原因：(a) 内核尚未提供串口 shell；(b) `user/apps/smoke_test.c` 尚未接入启动镜像自动运行
（按约束未改 Makefile）。一旦把该 app 编译进镜像并在挂载 C: 后自动启动，第二次启动的
串口日志出现 `SMOKE_PERSIST_RESULT=PASS` 即代表持久化通过。

---

## 四、运行方式

```bat
cd tests
build_all_tests.bat        :: 编译并运行全部 host 套件（333 项）
build_test.bat             :: 仅既有 VFS 回归（119 项）
```
```
python tests/smoke_test.py          :: QEMU 启动里程碑（超时默认 60s，可调 --timeout）
python tests/persistence_test.py    :: 两次启动测持久化（当前 SKIP）
```
QEMU 未安装时两个 Python 脚本会优雅降级，打印提示并以退出码 0 结束（不报错）。
