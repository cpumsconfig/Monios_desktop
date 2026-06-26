# monios_x64

`monios_x64` 是一个 x86_64 操作系统内核实验项目。项目包含 BIOS/UEFI 启动、长模式内核、内存管理、文件系统、网络栈、设备驱动、图形桌面、应用运行时和示例用户程序。

## 当前版本

V0.0.2B: 目录重构与功能扩展

- 目录结构重构：`drivers/`、`include/`、`lib/`、`fs/` 已移动到项目根目录，内核主体按 `arch/`、`mm/`、`sched/`、`ipc/`、`syscall/`、`ui/`、`net/`、`platform/`、`debug/` 拆分。
- 文件系统扩展：保留 FAT16/FAT32，加入 ISO9660、NTFS、extfs；extfs 支持文件/目录创建、删除、写入、块分配、inode 分配和符号链接。
- 新增驱动/框架：I2C、I3C、SPI、TPM、MCB、MD、OPP、OD、xHCI、USB 扩展、HID、蓝牙 HCI、WiFi 探测、GPU shim、CD-ROM/ATAPI。
- 密码学与网络：MD5/SHA-1/SHA-256、HMAC、AES-128/192/256、RSA、X.509、TLS/SSL、HTTP/HTTPS 状态框架。
- 内存管理扩展：页管理、大页、CMA、GUP、VMA、lazyalloc、buddy、bitmap、pool、zsmalloc。
- Bootloader 增强：FAT16/FAT32 动态检测、BIOS E820 内存探测、动态加载 kernel；同时提供 UEFI PE 镜像和 UEFI 安装 ISO 构建入口。
- 图形与桌面：framebuffer 桌面、窗口管理器、登录窗口、文件窗口、终端窗口、播放器、记事本、任务管理器、Cube3D 示例和 UAC 弹窗。
- 权限与会话：默认用户、`pwd.txt` 密码文件、`login`/`su`/`sudo`、R3/R2/R0 状态显示。
- 启动动画已调整为只显示进度条，不再显示 logo。
- 已添加 `hd_uefi.img` 镜像构建目标和当前镜像文件。

总代码行数约 47000+。

V0.0.1B: 初代版本

- 框架基本搭建。

## 当前重点

- 启动加载：BIOS 路径由 `kernel/arch/boot/boot.asm` 和 `kernel/arch/boot/loader.asm` 负责，loader 将内核运行镜像复制到 `0x00200000`，即 2 MiB 位置运行；早期长模式页表映射扩展到 16 MiB，避免清 BSS 和早期栈访问踩到未映射区域。
- UEFI 支持：`tools/build_uefi.py` 生成 `out/monios.efi`，`tools/build_uefi_iso.py` 生成 `out/monios_uefi_installer.iso`，`hd_uefi.img` 将 BIOS loader、kernel 和 `EFI/BOOT/BOOTX64.EFI` 放入同一个 FAT32 镜像。
- 内核布局：链接脚本位于 `kernel/arch/kernel.ld`，内核运行基址保持在 2 MiB。
- 文件系统：FAT16/FAT32 作为默认镜像读写路径；extfs 已有写入能力；NTFS 当前偏只读解析；ISO9660 已支持 PVD、目录遍历和文件读取，但不支持写入。
- 网络：已有 IPv4、DNS、DHCP、UDP、socket、TCP shim、lwIP shim；IPv6 提供地址解析、压缩输出、前缀匹配、链路本地、ULA 和 global-unicast 工具；TLS/SSL、HTTP/HTTPS、WiFi 和浏览器为可探测/可状态输出的逐步实现。
- 图形：framebuffer/桌面/窗口管理器保留，GUI 状态接口、GPU shim、窗口计数、焦点状态和高分辨率/色深信息已暴露。
- 设备：IDE/AHCI/NVMe/storage inventory、ATAPI CD-ROM、xHCI/USB 扩展、HID、蓝牙 HCI、GOP/Bochs BGA、音频、SMBus、TPM 等模块已接入状态或探测入口。
- 内核运行时：IPC 支持 create/send/recv/peek/broadcast/close；调度器保留 RR 并暴露 EEVDF/MUQSS 状态；多用户会话、ACPI/电源管理、SMP 状态和虚拟内存状态均可通过 shell 查询。

## 项目结构

```text
drivers/                 PCI、存储、网络、USB、GPU、音频、SMBus、I2C/I3C/SPI、TPM 等驱动
fs/                      FAT16/FAT32/ISO9660/NTFS/extfs 文件系统模块
include/                 公共头文件
kernel/arch/             长模式入口、内核主入口、MMU、链接脚本、中断和 BIOS bootloader
kernel/mm/               heap、frame、buddy、bitmap、VMA、lazyalloc、page、hugetlb、CMA、GUP、zsmalloc
kernel/sched/            task、scheduler、EEVDF、MUQSS、PCB、futex、signal、BSOD
kernel/ipc/              IPC 队列和进程/系统状态
kernel/syscall/          syscall 和用户程序执行入口
kernel/ui/               图形、字体、窗口管理、shell、GUI、浏览器状态、会话
kernel/net/              IPv4/IPv6、DNS、socket、TCP、TLS、HTTP、WiFi、密码学后端
kernel/platform/         ACPI、BIOS、GOP、RTC、CPU、SMP、power、device、driver manager、OPP/OD
kernel/debug/            registry、GDB stub、crash dump、ftrace
lib/                     内核/公共工具库
user/apps/               用户态示例程序：demo、explorar、monilogon、player、notepad、taskmgr、square、cube3d、rzdrv、setup
user/lib/                用户态运行时和 syscall wrapper
tools/                   镜像、UEFI、ISO、字体、音频和 RZS 打包脚本
Makefile                 构建、打包和运行入口
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

Windows 环境可以使用 MSYS2、WSL/WSL2，或直接安装对应交叉编译工具链。当前 `Makefile` 默认使用 `x86_64-elf-*` 前缀，并包含若干 Windows/cmd 风格命令。图形字体默认取 `C:/Windows/Fonts/msyh.ttc`，可以通过 `UI_FONT_SOURCE` 或 `UEFI_FONT_SOURCE` 覆盖。

## 构建与运行

BIOS/FAT32 镜像：

```bash
make
make hd.img
make run
make run_debug
```

UEFI 相关目标：

```bash
make uefi
make uefi_iso
make hd_uefi.img
make run_uefi_iso
make run_uefi_iso_debug
make run_uefi_q35
make run_uefi_q35_debug
```

VMware VMDK：

```bash
make hd.vmdk
make run_vmware
```

默认 `run_debug` 使用串口输出，成功启动后可以看到类似：

```text
BLJRFP
mmu: _text_start_pa=0x00200000
[R3] / $
graphics: desktop drawn
```

## 主要命令

内核 shell 中可以使用以下命令查看或操作模块。

基础和文本处理：

```text
help
ver
clear
which <command>
pwd
cd <path>
ls [path]
cat <path>
echo [text]
wc [path]
upper [text]
lower [text]
grep [-i] <pattern> [file]
head [-n <count>] [file]
tail [-n <count>] [file]
```

文件、权限和用户：

```text
mkdir <path>
touch <path>
write <path> <text>
rm <path>
rm -rf <path>
rmdir <path>
run <program> [args]
taskmgr
setup
whoami
users
login <user>
su
sudo <command>
env
set KEY=VALUE
unset KEY
exit
shutdown poweroff
```

网络和协议：

```text
net
dhcp
dns
ipv4
ipv6 [addr]
socket [open [port]|send|recv|close]
udp send <ip> <port> <text>
ping <ip>
tls [server]
ssl [server]
http get <host> [path]
https <url>
wifi
bluetooth
browser [url]
```

文件系统、设备和驱动状态：

```text
ide
ahci
nvme
cdrom
storagex
hda
aac
pcnet
lwip
xhci
usbext
hid
ntfs
iso9660
extfs
fscache [clear]
iic
i2c [scan|probe|speed]
i3c [scan|probe|freq]
spi [setup]
tpm [selftest]
mcb
md [array]
opp [domain]
od [on|off|domain]
bios
gop
rtc
cpu
fpu
cpuid
tcb
gui
wm
gpu
browser
power
term
smp
dev list
dev read \\.\fscache
dev read \\.\tls
dev read \\.\http
dev read \\.\wifi
dev read \\.\usbext
dev read \\.\bth
dev read \\.\cdrom
dev read \\.\iso9660
dev read \\.\i2c
dev read \\.\i3c
dev read \\.\spi
dev read \\.\tpm
dev read \\.\mcb
dev read \\.\md
dev read \\.\opp
dev read \\.\od
dev read \\.\storagex
dev read \\.\gpu
dev read \\.\gui
dev read \\.\power
dev read \\.\browser
```

内存、调度和 IPC：

```text
heap
frame
vma
lazyalloc
vmext
bitmap
buddy
pool
eevdf
muqss
scheduler
scheduler set [rr|eevdf|muqss]
schedopt
pcb
prsys
futex
futex wait <addr> <expected> [timeout]
futex wake <addr> [count]
ipc create <name>
ipc send <port> <text>
ipc recv <port>
ipc peek <port>
ipc broadcast <text>
ipc close <port>
signal
signal send <pid> <signo>
signal take <pid>
```

默认用户包括：

```text
root
guest
dev
```

默认镜像中的密码文件是 `pwd.txt`，图形登录和 shell 登录状态共用会话用户表。

## 当前边界

这是测试性内核。下面列出的模块已经有接口、探测、状态输出或部分读写能力，但仍不是完整生产级实现。

### TLS/SSL

已具备：

- TLS 1.0/1.1/1.2 常量、record 类型、handshake 类型和状态机结构。
- ClientHello 构造、record 构造/解析、ServerHello/Certificate/ServerHelloDone 解析骨架。
- AES、RSA、HMAC、SHA 系列和 X.509 证书链解析/验证接口。
- PRF、master secret/key block 派生、基础 socket 集成 API：`tls_connect`、`tls_write`、`tls_read`、`tls_poll`。
- shell 状态命令：`tls`、`ssl`、`dev read \\.\tls`。

仍待完善：

- ChangeCipherSpec/Finished 的完整发送、校验和加密状态切换。
- 更完整的证书信任库、主机名校验、证书吊销和错误报告。
- 现代 cipher suites、AEAD、TLS 1.3、重传/超时和更稳健的会话管理。
- 当前 HTTPS 依赖该 TLS 层，复杂站点连接成功率仍有限。

### HTTP/HTTPS

已具备：

- URL 解析、GET/POST 请求构造、HTTP 响应状态行/响应头解析。
- `Content-Length`、`Transfer-Encoding: chunked`、`Connection` 等基础字段识别。
- 基于 TCP socket 的 `http_get`，以及通过 TLS 上层封装的 `https_get` 入口。
- shell 状态命令：`http get <host> [path]`、`https <url>`、`browser [url]`、`dev read \\.\http`、`dev read \\.\browser`。

仍待完善：

- HTTPS 的稳定性仍受 TLS 完整握手能力限制。
- chunked 响应、长连接、重定向、压缩、流式读取和大响应缓存还需要增强。
- 浏览器当前是 URL/HTML/HTTP 状态框架，不是完整渲染器。

### WiFi/蓝牙

已具备：

- WiFi 通过 PCI class/subclass 探测无线控制器并记录 vendor/device/bus/slot/function/irq。
- 蓝牙暴露 USB transport/HCI 框架状态，能判断是否具备 USB native/xHCI 传输基础。
- shell 状态命令：`wifi`、`bluetooth`、`dev read \\.\wifi`、`dev read \\.\bth`。

仍待完善：

- WiFi 尚未实现扫描、认证、关联、加密和数据面收发。
- 蓝牙尚未实现控制器枚举、HCI command/event 流、配对、GATT/音频等协议层。

### GPU/图形

已具备：

- framebuffer 桌面、窗口管理器、鼠标光标、任务栏、登录/文件/终端/关于/播放器/记事本/任务管理器/Cube3D 等窗口。
- GOP/BGA/VMware SVGA 相关状态和 framebuffer 后端信息。
- GPU shim 暴露 width/height/bpp/pitch、submit/present 计数和 backend 名称。
- shell 状态命令：`gui`、`wm`、`gpu`、`dev read \\.\gpu`、`dev read \\.\gui`。

仍待完善：

- 当前 GPU 是 framebuffer/BGA/SVGA shim，不是完整厂商显卡驱动。
- 硬件队列、DMA command buffer、显存管理、2D/3D 加速和多显示器支持仍待实现。
- GUI 控件系统仍偏演示性质，布局、输入法、字体回退和应用间通信还需要继续完善。

### xHCI/HID/USB

已具备：

- xHCI PCI 探测、MMIO BAR 映射、capability register 读取、root port/max slot 状态输出。
- USB legacy/native host 状态、USB 扩展状态、HID 连接到 PS/2 键盘鼠标和 xHCI/legacy 桥接状态。
- shell 状态命令：`xhci`、`usbext`、`hid`、`dev read \\.\usbext`。

仍待完善：

- xHCI 还没有完整 command ring/event ring/transfer ring 调度。
- USB 设备枚举、配置描述符解析、endpoint 调度、热插拔和通用类驱动仍待实现。
- HID 目前以键盘鼠标状态桥接为主，尚未完成 USB HID report descriptor 解析。

### ACPI/电源管理

已具备：

- ACPI 状态、SCI IRQ、PM1 控制寄存器、S5 poweroff 和 power button hook 状态。
- `power` 模块整合 ACPI、CPU 频率检测和设备电源管理状态。
- shell 状态命令：`power`、`shutdown poweroff`、`dev read \\.\power`。

仍待完善：

- ACPI 依赖固件表能否在早期映射范围内被正确解析；QEMU 或部分真机可能退回 fallback 电源路径。
- AML 解释器、完整设备电源状态、睡眠/唤醒、热管理、电池和更细粒度的 CPU 电源策略仍待实现。

### I2C/I3C/SPI/TPM/MCB/MD/OPP/OD

已具备：

- I2C：bus/speed/transfer/scan/read/write API 和状态统计。
- I3C：CCC、DAA、设备信息、scan/read/write API 和状态统计。
- SPI：mode/speed/chip-select/transfer/read/write API。
- TPM：TIS/CRB/SPI/I2C 接口常量、TPM 1.2/2.0 识别、command、random、PCR、capability API。
- MCB：内存控制器/DIMM/频率/ECC/thermal 状态接口。
- MD：软件 RAID array/disk 管理、read/write API 和 resync 状态。
- OPP/OD：性能点、频率/电压、governor、功耗/温度/超频限制接口。
- shell 直接命令和 `dev read` 状态入口已经补齐，便于快速查看和探测。

仍待完善：

- 这些模块目前多为框架级或有限探测级实现，真实硬件覆盖面还需要逐设备补齐。
- 需要继续完善 PCI/ACPI/SMBus/固件表绑定、错误恢复、中断/DMA、并发访问和长期稳定性测试。
- shell 当前主要通过设备状态和相关子系统命令间接观察这些模块，后续可补独立管理命令。

### ISO9660/CD-ROM

已具备：

- ATAPI CD-ROM `TEST UNIT READY`、`INQUIRY`、`READ CAPACITY`、`READ` 基础流程。
- ISO9660 Primary Volume Descriptor 解析、卷标/发布者/应用信息、root extent/root size。
- 目录遍历、路径解析、文件存在判断、文件大小查询、文件读取和 root 列表。

仍待完善：

- ISO9660 是只读文件系统，`write/delete/mkdir/rmdir` 当前返回失败。
- Joliet、Rock Ridge、多区段、多盘、复杂文件名、权限/时间戳等扩展尚未实现。
- CD-ROM 路径仍需要更多 QEMU/真机环境验证。

### NTFS/extfs/FAT

已具备：

- FAT16/FAT32 是默认磁盘镜像的主要读写路径。
- extfs 已支持文件/目录创建、删除、写入、inode/块分配和符号链接。
- NTFS 已支持 boot metadata、MFT record、index root/allocation、路径解析、目录列出和文件读取。

仍待完善：

- NTFS 当前保持只读，`write/delete/mkdir/rmdir` 返回失败。
- extfs 仍需更多一致性、崩溃恢复、fsck 和边界场景测试。
- 统一 VFS、缓存一致性、权限和跨文件系统挂载模型仍需继续推进。

### 权限、会话和用户态

已具备：

- 默认用户 `root`、`guest`、`dev`，密码文件来自 `pwd.txt`。
- shell 支持 `login`、`su`、`sudo`，并显示 R3/R2/R0 状态。
- 用户程序可通过 `run` 或直接执行 `.elf`、`.exe`、`.rzs`，镜像内包含桌面示例应用和 RZS 驱动示例。

仍待完善：

- 权限模型仍偏演示性质，不是完整多用户隔离。
- 用户态 ABI、进程隔离、信号/IPC 与 GUI 应用生命周期还需要继续收敛。
