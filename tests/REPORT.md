# VFS 回归测试报告（host 端）

- 项目：`D:\11\monios\monios_x64`
- 被测代码：`lib/path.c`、`lib/file.c`（未修改任何被测源码）
- 测试位置：`tests/`
- 编译器：MinGW-W64 gcc 15.2.0（`D:\Program Files (x86)\mingw64\bin\gcc.exe`）
- 运行方式：`tests\build_test.bat`（或手动 `gcc -std=gnu17 -fno-builtin -I ../include -I . -o test_vfs.exe test_vfs.c stubs.c`）

## 结果

```
RESULT: 119 passed, 0 failed
```

退出码 0。编译无 error（仅有 `-Wbuiltin-declaration-mismatch` 提示，已用 `-fno-builtin` 消除；被测代码未改动）。

## 文件清单

| 文件 | 作用 |
|------|------|
| `tests/test_vfs.c` | 测试主体，`#include "../lib/path.c"` 与 `"../lib/file.c"` 直接拉入同一编译单元，因此可直接调用 file.c 的 static 函数（`find_mount_point`、`file_build_cache_key` 等） |
| `tests/stubs.c` | host 桩：x86 I/O 口（inb/outb/inw…）、块设备、fs_cache、各 FS 驱动（fat32/fat16/iso9660/ntfs/extfs）、GPT 分区表、以及 Monios 自带 string.h ABI 的字符串函数 |
| `tests/stub_values.h` | 桩返回的固定容量常量，供测试断言精确匹配 |
| `tests/build_test.bat` | 一键编译 + 运行脚本 |

## 覆盖的功能点

### 路径解析 `path_resolve` / `path_is_absolute`
- 基本绝对路径 `C:\foo\bar` 原样保留
- 盘符小写 `c:\foo` → 大写 `C:\foo`
- 正斜杠输入（`C:/foo`）被拒绝（返回 false）——确认了 `path_resolve` 当前**不支持** `/`，输入或 base 含 `/` 都返回 false
- 相对路径拼接：base=`C:\dir`, input=`file.txt` → `C:\dir\file.txt`
- `.` 归一：`C:\foo\.\bar` → `C:\foo\bar`
- `..` 回退：`C:\foo\..\bar` → `C:\bar`
- 根目录 `..` 不越界：`C:\..\foo` → `C:\foo`
- 多盘符：`D:\test` → `D:\test`
- 末尾反斜杠清理：`C:\foo\` → `C:\foo`
- 空输入 → 根 `C:\`；NULL 输入 / 过小输出缓冲（<4）→ false
- `path_is_absolute`：`C:\x`、`\x` 为绝对，`x`、NULL 非绝对

### 挂载表管理
- `file_init()` 后计数为 0
- `file_mount("C:\","fat32",0)` 成功，计数 1
- 重复挂载同路径失败，计数不变
- `file_mount("D:\","ntfs",100)` 成功，计数 2
- `file_get_mount_info(0/1)` 正确返回 C:fat32(partition=0)、D:ntfs(partition=100)；越界 index 与 NULL info 返回 false
- `file_umount("D:\")` 成功计数回 1；重复卸载、卸载不存在路径（Z:）均失败

### 挂载点容量上限
- 连续挂载 C:…J: 共 8 个填满 `MAX_MOUNT_POINTS=8`
- 第 9 个 `K:\` 挂载失败

### 盘符路由 / 最长前缀匹配（直接测 static `find_mount_point`）
- 挂载 C:、D:、`C:\subdir` 后：
  - `C:\file.txt` → C: 根挂载点
  - `D:\dir\file.txt` → D:
  - `E:\file.txt` → -1（未挂载盘符路由失败）
  - `C:\subdir\file` → 匹配更长的 `C:\subdir`（最长前缀生效）
  - `C:\other\file` → 仍回退到 C: 根
  - NULL / 空串 → -1

### 磁盘容量 `file_disk_space`
- 空表时 `E:\` 返回 false
- C: fat32 挂载后，total/free/cluster 与桩常量精确相等（1 GiB / 128 MiB / 4096）
- `C:\dir\file.txt` 子路径同样路由到 C: 并返回正确容量
- D: ntfs 挂载后，total = total_sectors×bytes_per_sector（4 MiB）、free=0、cluster=4096
- 未挂载 E: 仍 false；NULL info 拒绝

### 缓存 key `file_build_cache_key`（直接测 static 函数）
- 无挂载时 `g_current_mount_path` 为空 → 返回 false
- C: 挂载后：key = `"C:\" + 0x1F + rel_path`（逐字节校验盘符、`0x1F` 分隔符、相对路径）
- 输出缓冲过小 → false
- 切换到 D: 挂载后 key 前缀变为 `"D:\"，验证不同挂载点生成不同 key

### 盘符查询 API
- `file_get_drive_info('C')` → mounted、fs_type=FAT32、total 正确；小写 `'d'` 归一为 `D`、fs_type=NTFS
- `'Z'` 未挂载 → false；NULL info → false
- `file_drive_mounted('C'/'c'/'D')` true，`'X'` false
- `file_get_mounted_drives` 返回数量=2 且包含 C、D；NULL/0 容量返回 0

## 未覆盖 / 未测试的功能点及原因

- **`file_auto_mount()`**：依赖 ATA 端口轮询（`file_ata_wait_data_ready` 会空转 1,000,000 次 inb）与真实 MBR/GPT 扇区探测，host 无硬件，桩 `blockdev_read_sector` 返回 false、`gpt_detect` 返回 false，语义上无法在 host 验证分区探测。留待 QEMU/内核环境集成测试。
- **实际文件内容读写**（`file_read_at/write/exists/is_dir/mkdir/rmdir/list_dir/delete/size`）：这些只是把路径转成 backend 路径后转发给具体 FS 驱动（fat32_* 等），桩一律返回 false/0/-1；VFS 层的"resolve→switch→转发"链路在 disk_space / cache_key 时间接覆盖，但未用桩构造具体文件内容断言。
- **`fs_cache_read_at` 真实缓存命中/失效**：桩实现为直接回调 loader（旁路缓存），只验证了 `file_read_at` 能取到 loader，未验证缓存命中计数与 `fs_cache_invalidate_path/all` 的实际 key 匹配行为。
- **`file_mount_partition_hint`**、**`file_backend_name`**：未单独断言（属边角访问器）。
- **EXTFS / ISO9660 的 `file_disk_space` 分支**：fat32 与 ntfs 分支已断言；iso/ext 的容量计算公式未单独跑（桩数据已备好，按需可加用例）。

## 备注
- 被测 `lib/file.c` 当前版本含 GPT 分区探测（`gpt_detect/gpt_read_header/...`），已在 `stubs.c` 提供桩使其可链接；这些桩只影响不被 host 测试触达的 auto-mount 路径。
- 未修改 `lib/file.c`、`lib/path.c`、`include/file.h`、`include/path.h`。
