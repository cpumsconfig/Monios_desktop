# Monios 驱动开发指南

本指南面向要为 Monios x64 编写新驱动的开发者，基于项目实际代码整理。

---

## 1. Monios 驱动模型概述

Monios 驱动是 **PE 格式的 `.sys` 文件**，由 `x86_64-w64-mingw32-gcc` 编译，加载到内核地址空间执行。

### 1.1 文件格式
- 链接脚本：`user/apps/driver.ld`，入口符号 `DriverEntry`，默认加载基址 `0x05000000`。
- 编译/链接示例见根 Makefile 中 `out/%.sys` 与 `out/monios_%.unsigned.sys` 规则。
- **必须签名**：未签名的 `.sys` 由 `tools/sign_driver_sys.py` 签名后才能被 `driver_manager` 加载。

### 1.2 入口与生命周期
驱动必须导出：

| 函数 | 签名 | 说明 |
|------|------|------|
| `DriverEntry` | `bool (const monios_driver_runtime_t *runtime)` | 加载时调用；返回 `false` 表示失败 |
| `DriverUnload` | `void (void)` | 卸载时调用，释放全部资源 |
| `DriverTick` | `void (uint64_t now_ticks)` | 可选，周期性心跳 |

最小示例见 `drivers/monios/sysstub.c`。

### 1.3 签名机制
```
python tools/sign_driver_sys.py out/mydrv.unsigned.sys out/mydrv.sys \
        --qm-dir D:/qm --subject "MoniOS Driver Signing"
```
签名后的 `.sys` 放到镜像 `/Monios/driver/<name>.sys`，由 `driver_manager` 枚举加载。

---

## 2. 驱动与内核的接口

### 2.1 `include/driver_api.h`
```c
typedef struct {
    uint32_t abi_version;     /* 必须 == MONIOS_DRIVER_ABI_VERSION */
    uint32_t flags;           /* CRITICAL/PERIODIC/HOTPLUG */
    const char *name;
    const char *path;
    monios_driver_log_fn_t log;   /* 日志输出 */
    monios_driver_alloc_fn_t alloc; /* 驱动内部分配内存 */
    monios_driver_free_fn_t free;
} monios_driver_runtime_t;
```
驱动应保存 `runtime->log/alloc/free`，**不要直接调用内核 `kmalloc`**。

### 2.2 `include/driver_status.h`
`driver_status_snapshot_t` 描述已加载驱动的状态（名字、标志、是否 loaded/scheduled），供 `SYS_DRIVER_QUERY(42)` 查询。

---

## 3. 常见驱动类型开发要点

### 3.1 字符设备
- 参考模板：`drivers/template/template_char.c`
- 真实示例：`drivers/usb/hid.c`、`drivers/input/keyboard.c`、`mouse.c`
- 要点：实现打开/读/写/中断处理；流式数据，无扇区寻址。

### 3.2 块设备
- 参考模板：`drivers/template/template_block.c`
- 真实示例：`drivers/storage/ide.c`、`ahci.c`、`nvme.c`、`virtio/virtio_blk.c`
- 要点：按 LBA 扇区寻址；处理 IO 请求队列、DMA、完成中断；向块层注册磁盘容量。

### 3.3 网络设备
- 示例：`drivers/net/e1000.c`、`pcnet.c`、`rtl8139.c`、`virtio/virtio_net.c`
- 要点：发送/接收描述符环、中断收包、与内核 `net/` 协议栈对接。

### 3.4 USB 设备
- 示例：`drivers/usb/xhci.c`、`usb_ext.c`、`hid.c`
- 要点：先枚举 xHCI 控制器，再在其上挂 USB 设备类驱动。

### 3.5 音频设备
- 示例：`drivers/audio/hda.c`、`es1371.c`、`aac.c`
- 要点：PCM 流、混合器控制（`SYS_AUDIO_MIXER_CTL(55)`）。

---

## 4. 编译、签名、安装、调试

### 4.1 快速生成骨架
```
python tools/new_driver.py --name myuart --type char --vendor "MyCompany"
```

### 4.2 接入 Makefile
复制 `drivers/template/Makefile.fragment` 中的片段，按提示加进根 `Makefile`：
1. 编译 `.pe.o`
2. 链接 `.unsigned.sys`
3. 签名为 `.sys`
4. 把 `.sys` 加进 `hd.img` 的 `--copy` 列表（或加入 `DRIVER_PACKAGE_NAMES` 自动打包）

### 4.3 构建
```
make -j2                 # 全量，产出 out/kernel.exe 与 hd.img
make -j2 out/myuart.sys  # 单独构建一个驱动
```

### 4.4 调试
- 用 `run_debug`/`run_uefi_debug` 把串口打到 stdio，看 `runtime->log` 输出。
- 驱动 `runtime->log` 的输出会进内核日志。
- BSOD 时 `crash_dump` 会把寄存器与栈回溯写到 `C:\PANIC.LOG`。

---

## 5. 现有驱动代码索引

| 目录/文件 | 功能 |
|-----------|------|
| `drivers/monios/sysstub.c` | 最小 .sys 骨架（DriverEntry/Unload） |
| `drivers/monios/rzdrv.c` | 兼容驱动示例（含 DriverTick 周期回调） |
| `drivers/pci/pci.c` | PCI 总线枚举/配置空间访问 |
| `drivers/dma/dma.c` | DMA 映射/缓冲区分配 |
| `drivers/storage/ide.c` | IDE/PATA 磁盘驱动 |
| `drivers/storage/ahci.c` | AHCI/SATA 控制器 |
| `drivers/storage/nvme.c` | NVMe 块设备 |
| `drivers/storage/blockdev.c` | 块设备层抽象 |
| `drivers/storage/cdrom.c` | 光盘/ISO9660 |
| `drivers/net/e1000.c` | Intel e1000 网卡 |
| `drivers/net/pcnet.c` | AMD PCnet 网卡 |
| `drivers/net/rtl8139.c` | Realtek RTL8139 网卡 |
| `drivers/usb/xhci.c` | xHCI USB 3 主控 |
| `drivers/usb/usb_ext.c` | USB 核心栈 |
| `drivers/usb/hid.c` | USB HID 键盘鼠标 |
| `drivers/audio/hda.c` | HD Audio 驱动 |
| `drivers/audio/es1371.c` | Ensoniq ES1371 音频 |
| `drivers/audio/aac.c` | AC97 音频编码 |
| `drivers/gpu/gpu.c` | GPU 抽象层 |
| `drivers/gpu/igpu.c` | 内核显卡后端 |
| `drivers/virtio/virtio_blk.c` | virtio 块设备 |
| `drivers/virtio/virtio_net.c` | virtio 网卡 |
| `drivers/smbus/` | SMBus/ACP I2C 主机 |
| `drivers/i2c/i2c.c`、`i3c/`、`spi/` | 串行总线 |
| `drivers/tpm/tpm.c` | TPM 安全芯片 |
| `drivers/mcb/mcb.c`、`md/md.c` | 杂项/软 RAID |
| `drivers/bluetooth/bluetooth.c` | 蓝牙栈 |

---

## 6. 常见问题与调试技巧

1. **驱动加载被拒**：先检查是否忘了签名；`DriverEntry` 返回 `false` 也会被拒。
2. **ABI 不匹配**：确认 `runtime->abi_version == MONIOS_DRIVER_ABI_VERSION`。
3. **中断风暴**：确认在中断处理末尾 `pic_send_eoi` 或对应控制器 EOI。
4. **内存泄漏**：`DriverUnload` 里必须把 `runtime->alloc` 分配的全部内存 `runtime->free`。
5. **死机**：先在 `QEMU` debug 串口跑，用 `runtime->log` 二分定位；BSOD 后看 `C:\PANIC.LOG`。
6. **不要在内核态直接访问用户指针**：通过 `app_memory_*` 系列安全拷贝。
