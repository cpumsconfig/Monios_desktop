# monios_desktop

为轻量级 x86_64 内核。

**项目简介**
- 这是一个轻量级的 x86_64 内核项目，包含内核、驱动、文件系统、示例应用和构建工具。
- 适合用于操作系统课程实验、驱动开发原型和嵌入式系统学习。

**项目结构（摘要）**
- `kernel/`：内核源码与链接脚本。
- `drivers/`：设备驱动实现（网卡、USB、音频、键鼠等）。
- `fs/`：文件系统实现（FAT16/32）。
- `apps/`：示例用户程序与运行时。
- `include/`：公共头文件。
- `tools/`：辅助脚本（生成镜像、音频转换、字体生成等）。
- 根目录：`Makefile`、磁盘镜像文件模板与示例资源（`hd.img`、`hd.vmdk` 等）。


**先决条件 / 依赖**
- 一个 x86_64 交叉编译工具链：`x86_64-elf-gcc`, `x86_64-elf-ld`, `x86_64-elf-objcopy`。
- `nasm`（编译汇编引导/入口）
- `python3`（用于 `tools/` 下的辅助脚本）
- `qemu-system-x86_64`（运行与调试）和 `qemu-img`（生成 vmdk）
- 在 Windows 上：建议使用 WSL/WSL2 或 MSYS2/MinGW 提供类 Unix 工具链，或在本机安装对应工具（注意路径与权限）。

**快速检查清单**
- 已创建 `out` 目录：`mkdir -p out`
- 安装并可调用 `x86_64-elf-gcc` / `x86_64-elf-ld` / `x86_64-elf-objcopy`（请参考下方链接）
- `nasm`、`python3`、`qemu-system-x86_64` 可用

**示例：在 WSL / Ubuntu 上安装常见依赖**

```bash
sudo apt update
sudo apt install build-essential nasm qemu-system-x86 qemu-utils python3 python3-pip make
# 若仓库/镜像中没有预编译的 x86_64-elf 工具链，需要自行编译或使用上方链接中提供的二进制包。
```

**示例：MSYS2（Windows）**

```bash
# 在 MSYS2 终端下安装常用构建工具（不包含 x86_64-elf 工具链）
pacman -Syu
pacman -S make nasm python
```

注意：MSYS2 包管理器默认提供的是本机/本宿主的编译器（例如 `mingw-w64-x86_64-gcc`），而本项目要求使用带有 `x86_64-elf-` 前缀的交叉工具链（`x86_64-elf-gcc` / `x86_64-elf-ld` / `x86_64-elf-objcopy`）。请从上方“编译工具与下载链接”处获取或自行构建 `x86_64-elf` 工具集，或在 `Makefile` 中调整 `X86_64_CC` / `X86_64_LD` / `X86_64_OBJCOPY` 指向你所安装的工具。

示例（Ubuntu/Debian）：

```bash
sudo apt update
sudo apt install build-essential nasm qemu-system-x86 qemu-utils python3 python3-pip
# 安装交叉编译工具链（包名可能因发行版不同而异）
sudo apt install gcc-x86-64-linux-gnu binutils-multiarch
```

注意：本仓库的 Makefile 期望可用 `x86_64-elf-*` 前缀的交叉工具链；你也可以调整 Makefile 中的 `X86_64_CC` / `X86_64_LD` / `X86_64_OBJCOPY` 变量以匹配本地工具链。

**编译工具与下载链接**
- 官方 NASM（汇编器）： http://nasm.us/
- 交叉工具链（示例发布）： https://github.com/lordmilko/i686-elf-tools/releases
- QEMU 二进制与下载： https://qemu.weilnetz.de/
- Windows 下 GNU Make 二进制（无 Guile）： https://downloads.sourceforge.net/project/ezwinports/make-4.4.1-without-guile-w32-bin.zip

重要提示：请确保下载并安装的是带有 `x86_64-elf-` 前缀的交叉工具集（即 x86_64-elf-gcc / x86_64-elf-ld / x86_64-elf-objcopy）。链接中有时会出现 `i686-elf`（32 位）或其它前缀的二进制，**这些不是本项目需要的目标工具链**。若找不到现成的 `x86_64-elf` 二进制包，可考虑自行编译交叉编译器或在发行版包管理器中安装并调整 Makefile 前缀以匹配可用工具。

**快速开始（构建与运行）**
1. 在项目根目录创建输出目录（如果不存在）：

```bash
mkdir -p out
```

2. 构建磁盘镜像（构建内核、应用并打包为 `hd.img`）：

```bash
make hd.img
```

3. 在 QEMU 中运行（Makefile 会自动在需要时构建镜像）：

```bash
make run
```

4. 调试运行（串口输出至控制台且不自动重启）：

```bash
make run_debug
```

5. 生成 VMware 使用的 VMDK（需要 `qemu-img`）：

```bash
make hd.vmdk
```

也可以单独构建某个目标，例如只编译内核并生成二进制：

```bash
make out/kernel.bin
```

**常见问题 / 提示**
- 若出现找不到 `x86_64-elf-gcc` 等交叉工具，请安装或构建相应交叉工具链，或修改 Makefile 以使用本机可用的编译器前缀。
- Windows 用户在本机 cmd 下直接运行 `make` 可能遇到工具缺失或路径问题，建议使用 WSL/WSL2 或 MSYS2 环境。
- 若某些 tools 脚本报错，确保使用 `python3` 执行并且所需 Python 包已安装。

**开发与贡献**
- 阅读源代码并在本地运行测试镜像是最快的上手方式。
- 若你要贡献：
  - Fork 本仓库并在 feature 分支中实现改动。
  - 提交清晰的 PR，说明改动目的与测试步骤。

**参考文件**
- 构建脚本和规则定义在 [Makefile](Makefile).
- 生成/打包脚本位于 [tools/](tools/).

**支持的硬件（概要）**
- 网卡：Intel e1000（驱动位于 `drivers/net/e1000.c`）。
- 声卡/音频：ES1371 / AC97 驱动（位于 `drivers/music/` 与 `kernel/` 的音频子系统）。
- 输入设备：PS/2/AT 风格键盘与鼠标（`drivers/keyboard.c`, `drivers/mouse.c`），以及 USB 基本支持（`drivers/usb.c`）。
- 总线与控制器：PCI、DMA、SMBus（对应 `drivers/pci/`, `drivers/dma/`, `drivers/smbus/`）。
- 图形：基础帧缓冲/图形支持（`kernel/graphics.c`）。
- 存储：通过 FAT16/32 支持文件系统（`fs/` 中实现）。

以上为本项目中已有驱动/子系统支持的常见硬件类型；实际兼容性取决于宿主平台（QEMU/VMware）所模拟的设备以及所加载的驱动。

**默认密码**
- 镜像内默认密码为：`123456`（默认文件 `pwd.txt` 已包含在镜像中）。