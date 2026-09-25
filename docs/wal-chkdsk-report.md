# FAT32 写前日志 (WAL) 与开机自动 chkdsk — 实现报告

## 1. 改动文件清单

| 文件 | 改动 |
|------|------|
| `include/fat32.h` | 新增 `fat32_wal_mark_clean()`、`fat32_wal_dirty_at_mount()` 声明 |
| `fs/fat32.c` | 实现 WAL 核心；改写 3 处元数据写路径；`fat32_init` 挂载时恢复；增强 `fat32_chkdsk`（交叉链接检测 + FOUND.000 恢复）；`FAT32_CHK_MAX_CLUSTERS` 131072→262144 |
| `include/chkdsk.h` | 新建：`chkdsk_auto_on_boot()` 声明 |
| `fs/chkdsk.c` | 新建：开机自动检查编排（脏标志判定 + 调用 fat32_chkdsk + 串口输出） |
| `lib/file.c` | `file_unmount_all()` 中 `file_sync_all()` 之后调用 `fat32_wal_mark_clean()` 设置干净标志 |
| `kernel/arch/kernel.c` | 新增 `#include "chkdsk.h"`；`file_auto_mount()` 成功后、`fs_perm_init()` 前调用 `chkdsk_auto_on_boot()` |
| `Makefile` | `KERNEL_FS_OBJS` 加入 `out/chkdsk.o` |
| `include/firewall.h` | 仅修复一行注释笔误（`FW_DIR_*/...` 内 `*/` 提前闭合块注释，阻塞全量编译），与本任务无关 |

## 2. WAL 设计（简化版 redo/undo 日志）

**日志位置**：占用卷末尾 `FAT32_WAL_SECTORS = 512` 个扇区（256KB）。在 `fat32_init` 中从 `g_cluster_count` 里扣掉这 512 扇区，因此没有任何数据簇会被分配到日志区，与正常文件数据零冲突。卷太小时自动关闭 WAL，行为与改动前完全一致。

**扇区布局**：
- 日志头（日志区第 0 扇区）：魔数 `"MWLG"`(4) + version(4) + clean_flag(4) + next_index(4) + max_records(4)，其余补零。
- 每条记录 3 个扇区：
  - 记录头：魔数 `"WREC"`(4) + seq(4) + target_lba(4) + checksum(4) + status(4)
  - 旧数据扇区（512B）
  - 新数据扇区（512B）
  - status：`FREE=0 / IN_PROGRESS=1 / COMMITTED=2`

**写路径**（`fat32_logged_write_sector`，仅拦截元数据写）：
1. WAL 关或目标在日志区内 → 直接 `ata_write_sector`（不递归）。
2. 先读出目标扇区当前内容作为 old。
3. 追加记录：写记录头(IN_PROGRESS) → 写 old → 写 new。
4. 真正写目标扇区。
5. 把记录头翻成 COMMITTED，更新日志头 clean=DIRTY、next_index。

接入点（全部走 PIO 轮询 `ata_write_sector`，与现有 FAT32 一致）：
- `fat32_set_fat_entry()`：FAT 表项更新（含跨扇区 2 扇区情形，逐扇区记录）。
- `fat32_write_slot()`：目录项更新。
- `fat32_write_dir_sector()`：目录扇区（含 `.`/`..` 项）更新。
- 文件数据簇写（`fat32_write_file` / append）**不记日志**——只保护元数据；数据扇区即使撕裂，目录项/FAT 仍可事务性地回滚或提交，残余损坏由 chkdsk 兜底。

**挂载恢复**（`wal_recover`，在 `g_wal_on=false` 下执行，恢复写本身不入日志）：
- 日志头魔数不对 → 视为空日志，写 CLEAN 头，`dirty_boot=false`。
- 否则按 next_index 顺序回放 `[0, limit)`：
  - IN_PROGRESS → **undo**：把 old 写回目标；
  - COMMITTED → **redo**：把 new 写回目标。
- 按写入顺序回放保证一致性：每条记录的 old 记录的是前序已提交记录之后的磁盘态。
- 回放后把日志头重置为 CLEAN。

**干净卸载标志**：`file_unmount_all()` → `fat32_wal_mark_clean()` 把日志头置 CLEAN、next=0。下次挂载 `dirty_boot=false`，跳过恢复与 chkdsk。断电后日志头仍为 DIRTY，`dirty_boot=true`。

**Checkpoint**：记录写满（next ≥ max≈170）时就地把环形日志回卷到槽 0；旧记录已全部落盘，安全。

## 3. chkdsk 检查项

在原有基础上增强 `fat32_chkdsk()`：
1. **FAT 表一致性**：FAT1 与 FAT0 逐簇比对，不一致则以 FAT0 为准修复（原有）。
2. **目录项有效性**：起始簇号范围、文件大小合理性（原有）。
3. **环检测**：同链重复访问同簇（原有）。
4. **交叉链接**（新增）：用 `g_chk_ref[]` 引用计数，>1 的簇即为两个目录链共享，计入错误。
5. **丢失簇恢复**（增强）：FAT 已用但无目录项指向的孤立簇，原实现直接释放；现改为按链追踪、读入缓冲区、写为 `/FOUND.000/FILE00N.CHK`（自动建 FOUND.000 目录），再释放原孤立链。单链上限 64 簇（32KB 静态缓冲），超长链截断。
6. 结果经 `fs/chkdsk.c` 输出到串口：errors / fixed / files。

`chkdsk_auto_on_boot()` 仅在上次非正常关机（`fat32_wal_dirty_at_mount()` 为真）时运行；正常开机直接返回。

## 4. 验证方式与结果

- 单独编译：`fat32.o`、`chkdsk.o`、`file.o`、`kernel.o` 均 `exit=0`，无 warning/error。
- 链接：`make out/kernel.unsigned.exe` 成功，`out/chkdsk.o`、`out/fat32.o` 入链；`nm` 确认 `chkdsk_auto_on_boot`(T)、`fat32_wal_mark_clean`(T)、`fat32_wal_dirty_at_mount`(T) 符号解析正常。
- 运行期行为：首次挂载现有 hd.img 时，卷尾日志区为全零 → 魔数校验失败 → 记 CLEAN、`dirty_boot=false`，不触发误 chkdsk，原有读写不受影响。
- 端到端建议（需在 QEMU 中验证）：正常关机后启动串口应无 chkdsk 输出；运行中直接断电重启，则应见 `fat32-wal: recovered undone=.. redo=..` 与 `chkdsk: dirty shutdown detected...`。

## 5. 已知局限 / 设计取舍

- 文件数据簇写不记 WAL（仅元数据记日志）——性能与复杂度取舍，残余数据损坏由 chkdsk 兜底。
- 恢复写自身不入日志（`g_wal_on=false`），正确。
- 运行时 `fat32_format()` 不会主动清空旧日志区；全新 mkfat32 镜像尾扇区为零，首挂载安全。若运行时格式化后立即断电，理论上可能回放旧记录——本任务未覆盖该路径。
- 完整 `make -j2` 仍被**与本任务无关**的并发改动阻塞：`user/apps/mingw/gcc.c`（另一子代理 WIP 示例 app）与 Windows 驱动签名证书（环境缺失）。内核本体（含本改动）已独立编译链接通过。
