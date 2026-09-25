# Monios 驱动模板开发指南（drivers/template/）

本目录是开发新 Monios 驱动的脚手架。复制本目录并改名即可起步。

## 目录内容

| 文件 | 作用 |
|------|------|
| `template.h` | 设备上下文结构、日志宏、版本号 |
| `template_char.c` | **字符设备**驱动骨架（DriverEntry/DriverTick/DriverUnload） |
| `template_block.c` | **块设备**驱动骨架（额外演示扇区读写 IO） |
| `Makefile.fragment` | 新驱动接入构建系统需要追加的 Makefile 片段 |
| `README.md` | 本文件 |

## 快速开始

```
# 用脚本一键生成（推荐）：
python tools/new_driver.py --name myuart --type char --vendor "MyCompany"

# 或手动：
cp -r drivers/template drivers/mydriver
# 编辑 drivers/mydriver/mydriver.c / mydriver.h，按注释 TODO 填写硬件逻辑
# 按 Makefile.fragment 把驱动接入 Makefile
make -j2 out/mydriver.sys
```

## 驱动模型要点

- 编译产物是 PE 格式的 `.sys` 文件，用 `x86_64-w64-mingw32-gcc` 编译，入口符号为 `DriverEntry`。
- 驱动必须导出三个函数：
  - `bool DriverEntry(const monios_driver_runtime_t *runtime)` — 加载时调用，返回 `false` 表示加载失败。
  - `void DriverUnload(void)` — 卸载时调用，必须释放全部资源。
  - `void DriverTick(uint64_t now_ticks)` — 可选，周期性心跳回调。
- `runtime` 提供 `log/alloc/free` 三个回调；驱动里不要直接 `kmalloc`，用 `runtime->alloc`。
- 驱动加载前必须用 `tools/sign_driver_sys.py` 签名，否则 `driver_manager` 会拒绝加载。

## 两类模板的选择

- **字符设备**（串口、传感器、输入设备等流式接口）→ 改 `template_char.c`。
- **块设备**（磁盘、SSD、虚拟盘等按扇区寻址）→ 改 `template_block.c`。

更完整的真实示例：
- 字符/输入：`drivers/usb/hid.c`、`drivers/input/`
- 块存储：`drivers/storage/ide.c`、`ahci.c`、`nvme.c`
- 网络：`drivers/net/e1000.c`、`pcnet.c`
- 音频：`drivers/audio/hda.c`、`es1371.c`
