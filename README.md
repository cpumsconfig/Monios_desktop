# monios_x64

`monios_x64` 是一个 x86_64 操作系统内核。项目包含启动代码、长模式内核、内存管理、文件系统、网络栈、设备驱动、图形桌面、应用运行时和示例用户程序。

## 当前版本

V0.0.1B: 初代版本

## 当前重点

- 启动加载：`loader` 将内核运行镜像复制到 `0x00200000`，即 1 MiB 之后的 2 MiB 位置运行；早期长模式页表扩展映射到 16 MiB，避免清 BSS 和早期栈访问踩到未映射区域。
- 内核布局：`kernel/kernel.ld` 已同步到 2 MiB 链接基址，`.kconfig` 改为 4 个 `QUAD`，避免按 `uint64_t` 写配置时覆盖后续页表区。
- 文件系统：保留 FAT16/FAT32/ISO9660，扩展 NTFS 启动扇区、cluster、MFT record、index record、MFT0 探测元数据；新增统一文件系统读缓存 `fscache`。
- 网络：已有 IPv4、DNS、DHCP、UDP、socket、TCP shim、lwIP shim；IPv6 增加地址解析、压缩输出、前缀匹配、链路本地、ULA、global-unicast 等工具；新增 TLS/SSL、HTTP/HTTPS、WiFi 和基础浏览器状态框架。
- 图形：保留 framebuffer/桌面/窗口管理器，新增 GUI 应用框架状态、控件注册状态、GPU/硬件加速 shim 状态，暴露更高分辨率/色深相关信息。
- 设备：扩展 IDE/AHCI/NVMe/storage inventory、xHCI/USB 扩展、HID、蓝牙 HCI 框架、显卡/GOP/Bochs BGA 状态。
- 内核运行时：扩展 IPC 队列、peek/broadcast/close；增加 VM/VMA/lazyalloc 状态、SMP 调度优化状态、多用户会话、ACPI/CPU 频率/设备电源管理状态。

## 主要命令

内核 shell 中可以使用这些命令查看或操作新增模块：

```text
ipv6 [addr]
tls [server]
ssl [server]
http get <host> [path]
https <url>
wifi
bluetooth
browser [url]
storagex
usbext
fscache [clear]
ntfs
gui
gpu
power
vmext
schedopt
users
login <user>
ipc create|send|recv|peek|broadcast|close
dev list
dev read \\.\fscache
dev read \\.\tls
dev read \\.\http
dev read \\.\wifi
dev read \\.\usbext
dev read \\.\bth
dev read \\.\storagex
dev read \\.\gpu
dev read \\.\gui
dev read \\.\power
dev read \\.\browser
```

默认用户包括：

```text
root
guest
dev
```

默认镜像中的密码文件是 `pwd.txt`，图形登录和 shell 登录状态共用会话用户表。

## 项目结构

```text
apps/              用户态示例程序
boot/              boot sector、loader 和实模式/保护模式引导头
drivers/           PCI、存储、网络、USB、GPU、音频、SMBus 等驱动
fs/                FAT16/FAT32/ISO9660/NTFS/extfs 文件系统模块
include/           公共头文件
kernel/            内核主体、调度、内存、shell、图形、系统调用
lib/               用户态运行时和 syscall wrapper
tools/             镜像、字体、音频转换等辅助脚本
Makefile           构建、打包和运行入口
```

## 构建依赖

需要以下工具可在 `PATH` 中调用：

```text
x86_64-elf-gcc
x86_64-elf-ld
x86_64-elf-objcopy
nasm
python
make
qemu-system-x86_64
qemu-img
```

Windows 环境可以使用 MSYS2、WSL/WSL2，或直接安装对应交叉编译工具链。`Makefile` 默认使用 `x86_64-elf-*` 前缀。

## 构建与运行

```bash
mkdir -p out
make out/kernel.elf out/kernel.bin
make hd.img
make run_debug
```

普通 QEMU 运行：

```bash
make run
```

生成 VMware VMDK：

```bash
make hd.vmdk
```

默认 `run_debug` 使用串口输出，成功启动后可以看到类似：

```text
BLJRFP
mmu: _text_start_pa=0x00200000
[R3] A:/ $
graphics: desktop drawn
```

## 当前边界

这是测试性内核。以下模块已经有接口、探测和状态输出，但还不是完整生产级实现：

- NTFS/extfs 仍以元数据和只读探测为主，尚未实现完整目录遍历、日志恢复和读写。
- TLS/SSL 已有 record/x509/handshake 状态框架，密码学后端和证书链验证仍待接入。
- HTTP/HTTPS 目前能构造请求和记录 URL 状态，完整 TCP client、响应解析和 HTTPS 传输仍需继续实现。
- WiFi/蓝牙以控制器/传输探测和 HCI 框架为主，尚未实现扫描、认证、配对和数据面。
- GPU/硬件加速当前是 framebuffer/BGA 加速 shim 和状态接口，不是完整厂商显卡驱动。
- xHCI/HID/USB 扩展已经暴露 root hub/legacy/native 状态，完整 USB 枚举和 endpoint 调度仍待实现。
- ACPI 电源管理依赖固件表是否能在当前早期映射范围内被解析；QEMU 中可能退回 fallback 电源路径。
- 网络连接可能会失败
