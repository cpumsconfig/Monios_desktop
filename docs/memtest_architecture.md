# 内存诊断测试（memtest）架构说明

## 背景：为什么必须改掉旧实现

旧版 memtest 是**同步阻塞**的：`memtest_run()` 一口气把整个测试窗口跑完
才返回。后果是：

- 从 GUI 里点"开始内存测试"，整个界面卡死到测试结束。
- 窗口内每字节跑 8 种图案 + 64 轮走步 + 地址唯一性，几百 MB 就是几十秒
  起步的完全冻结。
- 没有进度、没有中途取消，用户除了等没有别的选择。

现在的实现是**增量可中断**模型：测试被拆成一小步一小步，由调用方按节奏
推进，UI 每帧推进一点，进度和取消都是实时的。

## 文件分工

| 文件 | 职责 |
| --- | --- |
| `include/memtest.h` | 内核/用户态**共享 ABI**：模式常量、`memtest_result_t`、请求块、内核函数声明 |
| `kernel/mm/memtest.c` | 真正的测试引擎（增量状态机） |
| `kernel/syscall/syscall.c` | `SYS_MEMTEST_CTL`（102）分发：用户内存校验 + 回写 |
| `user/apps/memtest.c` | 用户态前端（GUI/命令行），通过 syscall 驱动 |

### 关键约定：结构体只有一份定义

`include/memtest.h` 是 `memtest_result_t` 的**唯一**定义源，内核和用户态
都必须 `#include` 它，不得各自镜像一份。

这条约定不是洁癖，是修 bug 修出来的：历史上用户态自己定义了一份
`error_list[16]` 的镜像，而内核是 `error_list[32]`，状态快照拷贝时内核
会按 32 条写，**直接写穿应用侧的栈缓冲区**。头文件里的
`error_list_size` 字段就是留着做 ABI 自检的——两侧都填
`MEMTEST_MAX_ERRORS`，不一致时能立刻发现。

## 增量执行模型

内核侧状态机由这几个函数驱动（全部声明在 `memtest.h`）：

```
memtest_start(mode, range_start, range_end)   启动，range 传 0 自动挑窗口
memtest_step(budget_pages)                    推进一轮，返回 1=仍在运行
memtest_cancel()                              请求取消
memtest_get_result()                          取快照（内核地址）
memtest_progress()                            进度 0-100
memtest_elapsed_ms()                          已用毫秒
memtest_run(start, end, mode)                 同步跑完（无 GUI 的批处理路径）
memtest_ctl(req)                              SYS_MEMTEST_CTL 内核入口
```

### `memtest_step()` 的预算语义

调用方传 `budget_pages`，表示"这一轮最多实写多少个空闲页"。这样单次
调用的耗时有上界，UI 每帧调一次就能保持流畅。`memtest_run()` 内部就是
拿一个较大预算反复调 `memtest_step()` 直到跑完，供无 GUI 场景使用。

### 进度与统计

- `cursor` 指向下一个待测物理地址，`range_start`/`range_end` 是窗口边界。
- `total_bytes` 是**窗口内可测字节**（已跳过内核正在使用的物理帧）。
- `tested_bytes` 是已测字节。旧实现里这两个数的口径不一致，会出现
  `tested_bytes > total_bytes` 的荒谬值；现已统一为"单调递增、不重复不遗漏"。
- `elapsed_ms` 由 `start_tick`/`end_tick` 换算，未结束时按当前 tick 现算。

### 唯一的越界防护

`memtest_step()` 里对每个待测物理页只有一道闸门：

```c
if (frame_is_used(addr)) continue;
```

也就是说，凡是内核自己占用的物理帧一律跳过，绝不写入。这是保护内核
不被自己测挂的第一道也是唯一一道防线，改动测试逻辑时**不要**把它拆掉。

## 测试模式

| 模式 | 宏 | 内容 |
| --- | --- | --- |
| 基本 | `MEMTEST_BASIC` (0) | 快速图案翻转，冒烟级 |
| 快速 | `MEMTEST_QUICK` (1) | 多种图案，日常用 |
| 完整 | `MEMTEST_FULL` (2) | 8 种图案 + 64 轮走步 + 地址唯一性 |

完整模式单页代价是快速模式的数十倍，所以窗口**必须封顶**：
`MEMTEST_DEFAULT_BYTES = 64 MiB`。不封顶的话一次 GUI 会话根本跑不完。

## 用户态通道

`SYS_MEMTEST_CTL` 的请求块是 `memtest_ctl_request_t`：

```c
struct {
    uint32_t op;          /* MT_OP_START/STOP/STATUS/INFO */
    uint32_t mode;
    uint64_t range_start;
    uint64_t range_end;
    int32_t  result;      /* 内核填写 */
    uint32_t status_ready;
    memtest_result_t status;
}
```

- `MT_OP_START` — 启动，`range_*` 传 0 自动挑窗口
- `MT_OP_STOP` — 取消
- `MT_OP_STATUS` — **推进一步**并返回快照（这是 UI 每帧调的那个）
- `MT_OP_INFO` — 只看快照，不推进

`kernel/syscall/syscall.c` 里的分发按 `SYS_PRINTER_CTL` 的既有模式：
先用 `app_memory_user_range()` / `app_memory_copy_from_user()` 校验并拷入
请求块，处理完再用 `syscall_copy_to_user_or_abort()` 把结果写回。

### 修掉的两个历史 bug

1. **op 被当成 syscall 号**：旧前端写的是
   `syscall2(MT_OP_START, mode, 0)`，把 `MT_OP_START`(=1) 当成 syscall
   编号传了进去，实际调用的是编号 1 的系统调用。正确写法是
   `syscall1(SYS_MEMTEST_CTL, (uint64_t) req)`。
2. **`SYS_MEMTEST_CTL` 分发缺失**：`include/syscall.h` 里声明了
   `SYS_MEMTEST_CTL = 102`，但 `syscall.c` 的 `switch` 里**没有对应
   case**，调用会落到默认分支。现已补上完整分支。

## 怎么验证

1. 编译：`make` 应生成 `out/memtest.o` 与用户态 `memtest.exe`，零告警。
2. 运行：shell 里启动测试后，界面应保持可交互，进度条持续前进。
3. 取消：测试中途发 `MT_OP_STOP`，`cancelled` 应为 1 且很快停止推进。
4. ABI 自检：确认应用侧读到的 `error_list_size == MEMTEST_MAX_ERRORS (32)`。
5. 边界：故意把 `range_end` 设成越过 RAM 上界，应被夹回而非写坏内存。
