# 启动链与镜像格式

本文记录从固件到内核完成初始化这条链路上的**关键契约**。这些契约一旦被
破坏，症状往往是"开机黑屏且串口无任何输出"，极难定位，所以必须显式写下。

## 一、BIOS 启动链

```
BIOS 加载 LBA 0
      │
      ▼
out/boot.bin   (512 字节引导扇区，kernel/arch/boot/boot.asm)
      │  在根目录里找 "LOADER  BIN"
      ▼
out/loader.bin (kernel/arch/boot/loader.asm)
      │  在根目录里找 "KERNEL  EXE"
      │  读入 0x1000:0 的 512 字节 bounce buffer
      │  经 unreal mode 拷贝到物理 0x02000000
      │  建页表、进长模式
      ▼
out/kernel.exe (kernel/arch/kernel.ld，运行基址 2 MiB)
```

loader 会在 COM1 上打进度字符（`DebugPutc` 同时写 `0x3F8` 和 BIOS 电传）：

| 字符 | 含义 |
| --- | --- |
| `R` | loader 开始执行 |
| `K` | **没找到** KERNEL.EXE（失败路径） |
| `P` | 内核文件已读完，准备进长模式 |
| `123` | 长模式切换过程中的阶段性标记 |

内核启动后第一件事就是往 COM1 写 `KERNEL BOOT`。所以：

- 串口**完全没输出** → 引导扇区没找到 LOADER.BIN（死在 `LABEL_NO_LOADERBIN: jmp $`）
- 只有 `R` 没有 `P` → loader 没找到 KERNEL.EXE
- 有 `RFP123` 但没有 `KERNEL BOOT` → 长模式/页表切换阶段出问题

## 二、镜像格式：裸 FAT32 卷，不是分区盘

**`hd.img` / `hd_uefi.img` 是不带分区表的裸 FAT32 卷**：引导扇区本身
就是 VBR，BPB 直接位于 LBA 0。这是刻意的设计 —— BIOS 把 LBA 0 当引导
扇区执行，只有裸卷布局才成立。

由此产生两个必须遵守的约定：

### 2.1 引导头的 BPB 几何必须与 `tools/mkfat32.py` 一致

`tools/mkfat32.py` 会用**自己的**几何覆盖引导扇区里的 BPB：

```
TOTAL_SECTORS = 786432   RESERVED_SECTORS = 32
FAT_COUNT     = 2        FAT_SIZE        = 8192
ROOT_CLUSTER  = 2        → DATA_LBA = 32 + 2*8192 = 16416
```

但 `loader.bin` 读的是它**自己内嵌**的那份 BPB（`mkfat32.py` 不改写
`loader.bin`），所以 `kernel/arch/boot/include/fat32hdr.inc` 里的数值
必须是真实几何：

```
BPB_RsvdSecCnt dw 32     BPB_NumFATs   db 2
BPB_SecPerClus db 1      BPB_FATSz32   dd 8192
BPB_RootClus   dd 2      BPB_TotSec32  dd 786432
```

卷曾长期是 256 MiB（`TotSec32=524288` / `FATSz32=4096`）。扩到 384 MiB
的原因：`assets/` 载荷（字体 19.7 MB、壁纸、音频、信任根 bundle）加起来
超过了 256 MiB 的数据区，而旧版 `mkfat32.py` 在装不下时**不报错**，而是把
内部 `self.data` 越界撑大 —— 镜像文件比 BPB 声明的卷还大，FAT 里记录的
簇位置与实际写入位置从溢出点起全部错位，**末尾几个文件是坏的且无任何告警**。
现在 `mkfat32.py` 会在分配簇越界（含 FAT32 驱动保留的卷尾 512 扇区 WAL
区域）时直接构建失败，把静默损坏变成显式错误。

**历史事故**：这里曾长期写着 `BPB_FATSz32 dd 1576` / `BPB_TotSec32 dd
202752`，与 `mkfat32.py` 的 4096 / 524288 不符。后果是 `boot.asm` 认为
根目录在扇区 3184（实际在 8224），找不到 LOADER.BIN 而进入死循环 ——
开机黑屏、串口零输出、无任何错误信息。

现在有两道防线：

1. `boot.asm` 与 `loader.asm` **在运行时从 BPB 读几何**（`RsvdSecCnt`、
   `NumFATs`、`FATSz32`、`RootClus`、`SecPerClus`、`HiddSec`），不再用
   编译期常量；
2. `mkfat32.py` 的 `write_boot_sector()` 会校验引导头里的 BPB 与自己的
   几何是否一致，不一致直接**构建失败**并指出该改哪个文件，不会静默
   产生一个开不了机的镜像。

### 2.2 VBR 的 446..509 字节不是分区表

MBR 布局里 446..509 是分区表，但裸卷的 VBR 在这段区域放的是**引导代码**
（例如 `B4 42` = `mov ah,42h`、`CD 13` = `int 13h`）。这些字节按分区项
解读经常凑出看似合法的 `type`/`lba`/`sectors`。

**历史事故**：内核的 `file_auto_mount()` 曾无条件相信 MBR 分区表，于是在
裸卷上解析出一个根本不存在的分区（`type=0x7D`, `lba=2112208600`），去挂载
它并失败 → 整个文件系统挂不上 → 驱动管理器读不到 `C:\Monios\kernel.exe`
做签名校验 → **BSOD: KERNEL SIGNATURE INVALID**。

现在 `file_auto_mount()` 在"分区候选全部挂不上"时会**回落到整盘 LBA 0**
（`file_try_mount_raw_volume()`），由各 fs 驱动自己从 BPB 判断。回退放在
分区之后，正常分区盘行为不变，也不会重复挂载同一个卷。

## 三、驱动与内核的签名链

```
assets/cent/*.crt  ──stage_trust_roots.py──▶  out/trust-roots.bin
                                                   │
                                    mkfat32.py 拷入镜像
                                                   ▼
                              /Monios/System/Security/trust.bin
                                                   │
                       kernel boot: trust_store_load() ──▶ x509 信任库
                                                   │
        driver_manager 用该信任库校验 kernel.exe / *.sys 的 Authenticode 签名
```

- `sign_driver_sys.py` 用 `makecert` + `signtool` 给 PE 打 Authenticode 签名，
  并把签名证书导出为 `assets/cent/0000000000000000000000000000000000000000.crt`。
  文件名全零是刻意的，但**装载已经不再依赖这个约定**（见下）。
- `x509_trust_store_t` 的静态容量是 `X509_MAX_TRUSTED_ROOTS = 64`，而
  `assets/cent` 下有 556 个根证书 (`out/trust-roots.bin` 约 676KB)。超出的根
  会被跳过，`trust_store_load()` 会打出跳过数量与告警，避免静默丢根。

**装载顺序（防回归）**：`trust_store_load()` 是**两遍装载**——第一遍只收带
`codeSigning` 扩展密钥用法 (`1.3.6.1.5.5.7.3.3`) 的根，第二遍才用剩余槽位收
普通根。当前 bundle 里 556 张证书中有 8 张带该扩展，签名证书必在其中，因此
签名根是否被装上只取决于它自己的扩展字段，与文件命名、排序、增量证书数量都
无关。启动日志会打：

```
trust-store: records                    ← bundle 里的记录数
trust-store: code-signing roots         ← 第一遍装上几个（为 0 会显式告警）
trust-store: parsed roots               ← 合计装上的根数
trust-store: roots skipped              ← 因容量或解析失败未能装上的数量
```

若哪天 `code-signing roots` 变成 0，日志会出现
`trust-store: warning - no code-signing root loaded`，同时 `driver_manager`
大概率会在验签阶段 BSOD —— 两者一起看即可定位是 bundle 内容问题还是分类逻辑
问题。

签名校验失败会 BSOD，路径会显示它尝试过的内核镜像位置：
`C:\Monios\kernel.exe` → `C:\kernel.exe` → `C:\KERNEL.EXE`。

## 四、怎么验证启动

需要 `qemu-system-x86_64`，并把内核控制台（COM1）落到文件：

```bash
# BIOS
qemu-system-x86_64 -machine pc -m 512M -display none -monitor none \
  -serial file:boot_bios.log -drive file=hd.img,format=raw -no-reboot

# UEFI（需要 edk2-x86_64-code.fd）
qemu-system-x86_64 -machine pc -m 512M -display none -monitor none \
  -serial file:boot_uefi.log \
  -drive if=pflash,format=raw,readonly=on,file="C:/Program Files/qemu/share/edk2-x86_64-code.fd" \
  -drive file=hd_uefi.img,format=raw -no-reboot
```

> QEMU 是原生 Windows 程序，`-serial file:` 后面的路径要写 Windows 风格
> （`boot_bios.log` 或 `D:/...`）。写成 `/tmp/xxx` 会让 QEMU 静默不落日志，
> 排查时容易误判成"串口零输出"。
>
> `-machine q35`（磁盘挂 AHCI）现在也可以用了 —— 文件系统驱动已改走块设备
> 层，见 `docs/known_limitations.md` 的「已修复 / 存储栈」。

启动成功的判据，按出现顺序：

```
RFP123KERNEL BOOT                       ← BIOS 引导链 + 内核入口
AHK123KERNEL BOOT                       ← UEFI 引导链（monios.efi）的等价序列
[..] boot: filesystem mounted           ← FAT32 挂载成功
[..] font: loaded msyh.ttc bytes=...    ← 能从卷里读出 19 MB 字体
[..] trust-store: roots loaded
[..] driver-manager: kernel signature verified
[..] ide: ATA disks ready
[..] boot: init session
[..] desktop_ready                      ← 桌面初始化完成，启动流程走完
```

出现 `BSOD:` 或 `boot: filesystem mount failed` 即为失败。挂载相关路径的
关键判据都会打到日志里（`fs:` / `fat32:` 前缀），包括底层扇区读是否成功、
引导签名是否存在、BPB 各字段值、是否走了裸卷回退。

日志末尾还会有一段启动耗时剖面（`boot_tick` 输出），例如：

```
drivers                             71890537
tasks_shell                         20071986124
desktop_ready                       7059670773
TOTAL cycles: 47098569688
TOTAL approx ms: 1650
```

## 五、当前验证状态

在 QEMU 上实测：

| 镜像 | 机型 / 固件 | 磁盘控制器 | 额外设备 | 结果 |
| --- | --- | --- | --- | --- |
| `hd.img`（BIOS） | `-machine pc` | IDE | — | ✅ `desktop_ready` |
| `hd.img`（BIOS） | `-machine q35` | AHCI | — | ✅ `desktop_ready` |
| `hd.img`（BIOS） | `-machine q35` | AHCI | `qemu-xhci` + `usb-kbd` | ✅ `desktop_ready` |
| `hd_uefi.img`（UEFI + OVMF） | `-machine pc` + pflash | IDE | — | ✅ `desktop_ready` |
| `hd_uefi.img`（UEFI + OVMF） | `-machine q35` + pflash | AHCI | — | ✅ `desktop_ready`，约 1.6 s |
| `hd_uefi.img`（UEFI + OVMF） | `-machine q35` + pflash | AHCI | `qemu-xhci` + `usb-kbd` | ✅ `desktop_ready` |

`-machine q35` 一度会停在 `boot: filesystem mount failed` →
`BSOD: KERNEL SIGNATURE INVALID`：q35 没有 IDE 控制器，磁盘挂在 AHCI 上，
而文件系统驱动当时直连 legacy ATA PIO。现在驱动已改走块设备层
（`drivers/storage/blockdev.c`），两种控制器都能起来。细节见
`docs/known_limitations.md` 的「已修复 / 存储栈」。

最后一行（UEFI + q35 + `qemu-xhci`）现在必须过，而且它同时守着两条容易
踩空的路：

- OVMF 把 xHCI 的 **64 位 BAR 放到 `0xC000000000`**（4 GiB 以上）。只读
  `bar0` 会把它当成 0；只做 64 位拼接而不给页表映射加上界检查，则会因为
  `mmu_map_identity_flags()` 越界写而直接崩机（实测会重启）。
- 因此该格同时覆盖「64 位 BAR 拼接」与「BAR 超出映射窗口时安全拒绝」，
  预期日志是：

```
[..] xhci: mmio bar outside mapped window   ← 安全拒绝，且继续启动
[..] xhci: bar lo 0x00000004
[..] xhci: bar hi 0x000000C0
```

### xHCI 控制器状态判据

启动日志里 xHCI 只有三种可能的状态行，含义互不相同：

```
[..] xhci: controller running                                  ← 可用
[..] xhci: operational registers not responding (reads zero)   ← 见 docs/known_limitations.md P0 第 1 条
[..] xhci: mmio bar outside mapped window / mmio bar missing   ← BAR 不可用
```

后两种状态下 `xhci_roothub_poll()` 直接返回 0，不会白等端口的超时。
出现 `controller running` 之后，失败路径会打印完成码与原始状态字，例如：

```
[..] xhci: address device failed: context state error
[..] xhci: raw completion status 0x13000000
[..] xhci: configure endpoint failed: parameter error
```

启动日志里与本项目其它修复相关的新增判据：

```
[..] trust-store: records               ← bundle 记录数
[..] trust-store: code-signing roots    ← 第一遍装上的签名根数（为 0 会显式告警）
[..] trust-store: parsed roots          ← 合计装上的根数
[..] trust-store: roots skipped         ← 因容量或解析失败未装上的数量
[..] fat32: blockdev index              ← 卷实际是从哪块设备读的
                                           （IDE 下是 ata0，AHCI 下是 ahci0）
```
