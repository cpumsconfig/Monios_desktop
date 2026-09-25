# 已知局限与阻塞项

本文件汇总 MoniOS 当前**明确未完成**的部分，按"是否阻塞真实硬件"排序。
README 的「当前边界」一节是面向使用者的简要版，这里是面向开发者的完整版。

分级约定：

- **P0 阻塞** — 不补齐则相关功能在真机/真实硬件上完全不工作。
- **P1 功能缺口** — 能跑，但能力明显不完整。
- **P2 打磨项** — 可用，只是不够健壮或体验不佳。

---

## P0 阻塞真实硬件

### 1. xHCI：模拟器上已全链路通过；真机验证仍缺

USB 主机控制器的软件侧缺陷已全部修掉（见下「USB / xHCI」小节），并且
**已在 QEMU 的 `qemu-xhci` 上取得完整通过证据**，覆盖三种启动形态：

| 用例 | 结果 |
| --- | --- |
| BIOS + q35 + qemu-xhci（usb-kbd + usb-tablet + usb-storage） | `desktop_ready`；三设备全部 Enable Slot → Address Device → Configure Endpoint → 正确分类（hid / hid / mass-storage） |
| UEFI + q35 + qemu-xhci（usb-kbd） | `desktop_ready`；xHCI BAR 位于 `0xC000000000`（768 GiB），经高地址页表池映射成功，键盘完整枚举（接口类 03/01/01） |
| BIOS + pc（无 xHCI） | `desktop_ready` 基线不受影响 |

早先记录的"运算寄存器读回全 0"问题**已不复现** —— 它是当年一串软件缺陷
（`.bss` 64 字节对齐被链接脚本的 `SUBALIGN(16)` 破坏导致 ERSTBA/DCBAAP
低位被硬件截断、64 位寄存器先写高双字导致 CRCR/ERSTBA 锁存到 0、中断器
偏移错位、PORTSC 基址错误）的合并症状；逐一修复后寄存器读回正常，
`xhci: controller running` 稳定出现。

**仍然开放的部分**：以上证据全部来自 QEMU。真实 xHCI 硬件（Intel/AMD/
ASMedia 等）在完成码时序、上下文大小协商、端口训练状态机上与 QEMU 存在
差异，**尚无真机通过证据**。驱动保留了完整的失败路径日志
（`xhci: address device failed: <完成码>` 等），真机调试以这些行第一手定位。

### 2. WiFi：只有探测，没有数据面

WiFi 目前仅通过 PCI class/subclass 探测无线控制器并记录
vendor/device/bus/slot/function/irq。**未实现**扫描、认证、关联、加密
（WPA/WPA2 握手）和数据面收发。

---

## P1 功能缺口

### 网络

- **TLS**：ChangeCipherSpec/Finished 的完整发送与校验、加密状态切换未完成；
  证书信任库、主机名校验、证书吊销不完整；缺现代 cipher suite、AEAD、
  TLS 1.3、重传/超时与稳健会话管理。HTTPS 稳定性直接受此限制。
- **HTTP/HTTPS**：chunked 响应、长连接、重定向、压缩、流式读取、大响应
  缓存都需要增强。
- **浏览器**：目前是 URL/HTML/HTTP 状态框架，**不是**渲染器。

### 音频

- **AAC/M4A/MP4**：只做容器与 ADTS 头解析，无解码器。
- 无流式/环形播放，整段 PCM 一次性提交，受内存上限约束。
- 重采样为线性插值，无混音、多流并发、音量控制与效果器。
- 详见 `docs/audio_pipeline.md`、`docs/mp3_decoder.md`。

### 蓝牙

- 传输层受上面 P0 阻塞（xHCI）。
- 协议层缺配对（Legacy/SC/SSP）、SDP 服务发现、GATT、A2DP/HFP 音频 profile。

### 存储与文件系统

- **块设备层的单次传输上限**：各驱动的 DMA 暂存缓冲是固定大小的
  （AHCI 32 个扇区、virtio-blk 8 个），块设备层现在会按
  `blockdev_t::max_transfer_sectors` 自动切块。新增驱动必须填对这个字段，
  否则大块读写会被驱动直接拒绝。
- **iso9660 走的是 `cdrom_*` 驱动 API**（`iso9660_read_block()` 先调
  `cdrom_read_sectors()`，PIO 仅作兜底），没有接入块设备层的 `cdrom0` 条目。
  两条路径最终都落到 ATAPI，功能等价；但 CD-ROM 上的块设备条目因此没有实际
  使用者。
- **AHCI 写能力**：`ahci_write_sector(s)` 已接到块设备层。写路径与读路径共用
  同一条命令下发实现（`ahci_issue_data_command`，含 TFES 检查），已用
  "镜像启动前后 md5 发生变化" 做过落盘验证，但尚无专门的写入压力测试。
- **NTFS**：只读，`write/delete/mkdir/rmdir` 一律返回失败。
- **ISO9660**：只读；缺 Joliet、Rock Ridge、多区段、多盘、复杂文件名、
  权限/时间戳。
- **extfs**：需要更多一致性、崩溃恢复、fsck 与边界场景测试。
- 统一 VFS、缓存一致性、权限与跨文件系统挂载模型仍在推进。

### ACPI/电源

- 依赖固件表能否在早期映射范围内正确解析；QEMU 或部分真机可能退回
  fallback 电源路径。
- 缺 AML 解释器、完整设备电源状态、睡眠/唤醒、热管理、电池与细粒度
  CPU 电源策略。

### SVM 虚拟化

- 尚未实现完整 VM 切换、NPT 嵌套页表、vCPU 调度与客户机中断注入。
- 当前是框架级准备，距离可运行嵌套客户机较远。

### GPU/图形

- framebuffer/BGA/VMware SVGA shim，**不是**厂商显卡驱动。
- 缺硬件队列、DMA command buffer、显存管理、2D/3D 加速、多显示器。
- GUI 控件系统偏演示性质；布局、输入法、字体回退、应用间通信待完善。

### 总线与外设（I2C/I3C/SPI/TPM/MCB/MD/OPP/OD）

- 多为框架级或有限探测级实现，真实硬件覆盖面需逐设备补齐。
- I3C 的 ENTDAA 协议层已实现（合法动态地址 0x08–0x3D、奇校验位、
  PID/BCR/DCR 解析、后端注册 `i3c_register_bus_ops()`），但**需要控制器
  驱动提供 `daa_read` / `daa_write_addr` / `ccc_broadcast` 才算接通真机**；
  未注册后端时退回 I2C 兼容枚举，且**不会伪造动态地址**（I2C 设备的
  `dynamic_addr` 明确置 0）。
- 需继续完善 PCI/ACPI/SMBus/固件表绑定、错误恢复、中断/DMA、并发访问
  与长期稳定性测试。

### 权限、会话与用户态

- 权限模型偏演示性质，不是完整多用户隔离。
- 用户态 ABI、进程隔离、信号/IPC、GUI 应用生命周期仍在收敛。
- **无动态链接器与共享库机制**，用户程序以静态链接为主。

### 签名与信任链

- **信任根容量只有 64**：`x509_trust_store_t` 的静态容量是
  `X509_MAX_TRUSTED_ROOTS = 64`，而 `assets/cent` 下有 556 个根证书
  （`out/trust-roots.bin` 约 676KB），超出的会被跳过。启动日志会明确打出
  `trust-store: records` / `code-signing roots` / `parsed roots` /
  `roots skipped` 四个数，不再静默丢根。
  占用约为每根 14.5KB（`sizeof(x509_cert_t)`，其中 `raw[4096]` + `tbs[4096]`
  就占 8KB），**装齐 556 个根需要约 8MB 额外 `.bss`** —— 这是"容量 64"这个
  数字背后的真实取舍，动它之前先算这笔账。
  **签名根已经不再依赖文件命名**：`trust_store_load()` 改为两遍装载，
  第一遍只收带 `codeSigning` 扩展密钥用法的根（当前 bundle 里 8 张候选，
  5 张解析成功），签名证书必在其中。详见 `docs/boot_chain.md`。
- 驱动 `*.sys` 与 `kernel.exe` 必须带合法 Authenticode 签名，否则
  `driver_manager` 直接 BSOD。开发环境依赖 `D:/qm` 下的 `makecert` /
  `signtool`，缺任一工具即无法产出可启动镜像。
- 尚无吊销（CRL/OCSP）、时间戳与证书有效期策略。

---

## P2 打磨项

- **CPU 状态与调度**：SMP 状态、EEVDF/MUQSS 可切换但稳定性和验证不足。
- **TLS 性能**：纯软件加密路径无优化。
- **日志与诊断**：需要更结构化的错误上报，便于自动化定位问题。
- **构建**：`-Wall -Wextra` 已清零告警，但尚未接入 CI 强制门禁。

---

## 已修复（记录在案，避免回归）

以下问题是本次整顿中**发现并修复**的真实缺陷，列在这里是为了防止
将来被无意改回去：

| 位置 | 问题 | 修法 |
| --- | --- | --- |
| `xhci_control_transfer()` | Status 阶段方向位写死 IN，control-IN 状态阶段方向错误 | 按数据阶段反方向设置 |
| `xhci_bulk_transfer()` | 门铃号写死 `endpoint*2`，IN 传输应为 `endpoint*2+1` | 按方向加偏移 |
| xHCI 传输环 | 复用命令环的 `g_cmd_ptr`/`g_cmd_cycle`，多设备互相踩踏 | 改按设备独立的 enqueue 指针与 cycle 位，并补 Link TRB |
| `hda_stream_setup()` | BDL 地址写进 `CBL`、`CBL` 从未编程、`LVI` 偏移错误 | 按规范编程 `CBL`/`BDPL`/`BDPU`/`LVI`/`FMT` + 回读自检 |
| `hda_stream_format()` | `SDnFMT` 编码错位 | `(rate<<8)｜(bits<<4)｜(ch-1)`，48k/16/stereo = `0x0011` |
| syscall 分发 | `SYS_MEMTEST_CTL(102)` 在头文件声明但分发器缺 case | 补完整分支 |
| `user/apps/memtest.c` | 把 `MT_OP_START` 当 syscall 号传 | 改为 `syscall1(SYS_MEMTEST_CTL, (uint64_t)req)` |
| memtest ABI | 应用侧 `error_list[16]` vs 内核 `[32]`，拷贝时写穿栈 | 统一 `include/memtest.h` 为唯一定义源 |
| `i3c_do_daa()` | 给 I2C 设备伪造动态地址 | I2C 设备 `dynamic_addr = 0` |
| `i3c_send_ccc()` | 无后端时"模拟成功" | 不可软件完成的 CCC 返回 -1 |
| 蓝牙 L2CAP | 用 `mtu` 字段存远端 DCID | 新增独立的 `remote_cid` 字段 |
| memtest 统计 | `tested_bytes` 重复/漏计，出现 `tested > total` | 统一口径为单调递增、不重复不遗漏 |

### 存储栈（曾导致 AHCI/NVMe 机器完全无法启动）

| 位置 | 问题 | 修法 |
| --- | --- | --- |
| `fs/fat32.c`、`fs/fat16.c`、`fs/ntfs.c`、`fs/extfs.c` | 四个驱动的 `ata_read_sector()` / `ata_read_sectors()` / `ata_write_sector()` / `ata_write_sectors()` 直接读写 **0x1F0 legacy ATA PIO 端口**，完全绕过 `drivers/storage/blockdev.c` 的块设备抽象。在没有 legacy IDE 控制器的机器上（AHCI/NVMe-only，含 QEMU `-machine q35`）端口读回全 0 → BPB 全 0 → `bpb rejected` → 挂载失败 → 读不到 `kernel.exe` → **BSOD: KERNEL SIGNATURE INVALID** | `lib/file.c` 新增块设备提示（`g_probe_blockdev` / `g_pending_blockdev` / `g_current_blockdev` + `file_blockdev_hint()`，与既有的 `partition` hint 机制一一对应，并随挂载点持久化）；四个驱动在 `*_init()` 里取提示，之后所有扇区 I/O 走 `blockdev_raw_read_sectors()` / `blockdev_raw_write_sectors()`，**拿不到提示时回退 PIO**，IDE 路径行为不变 |
| `drivers/storage/blockdev.c` | 块设备层不做传输切块，而各驱动的 DMA 暂存缓冲是固定大小的（`AHCI_MAX_SECTORS_PER_IO=32`、`VIRTIO_BLK_MAX_SECTORS_PER_IO=8`），超过就被驱动拒绝。文件系统读文件时会一次要几百个扇区，**大文件读取会在真机上静默失败** | `blockdev_t` 新增 `max_transfer_sectors`，`blockdev_raw_read/write_sectors()` 按它自动切块下发；两个驱动宏从 `.c` 提到各自头文件，注册时填值 |
| `drivers/storage/blockdev.c` | AHCI 被注册成只读（`blockdev_no_write_sector`），而 `ahci.c` 里其实早有 `ahci_write_sectors()`。结果是 AHCI 机器上文件系统写操作静默失败 | 补 `ahci_write_sector()` 单扇区包装并接到块设备层；FAT 驱动写失败会打一次 `fs: warning - block device rejected write`，不再静默 |
| `lib/file.c` | 切换挂载点的快速路径只比较 `fs_type` 和 `partition`，同一 fs、同一分区号但在**不同块设备**上的两个挂载点会被误判为同一个 | 快速路径加上块设备序号比较；`mount_point_t` 新增 `blockdev_index` 字段 |
| `drivers/storage/blockdev.c` ATA fallback | `blockdev_ata_read/write_sectors()` **每个扇区单独发一条 ATA 命令**（`SECTOR_COUNT=1`）。每条命令的异步完成延迟（QEMU 本机实测 20-45ms）都要付一次：读 19.7 MB 字体 = 38500 条命令，表现为"启动卡死在 font loading"，比 fs 自带 PIO 快路径（一条命令最多 255 扇区）**慢约两个数量级** —— 文件系统改走块设备层后启动从 2 秒退化到不可用 | 改为真正的多扇区 PIO：一条 READ/WRITE SECTORS 命令带满 `BLOCKDEV_ATA_MAX_MULTI_SECTORS=255` 个扇区，每扇区等 DRQ 后传 256 个字，块间在函数内自动续；`ata0` 注册时声明 `max_transfer_sectors=255`。修复后三种启动形态均恢复 `desktop_ready ≤ 2s` |

迁移与验证：

- 判定依据是 QEMU 差分测试 —— 同一镜像 `-machine pc`（磁盘挂 IDE）与
  `-machine q35`（磁盘挂 AHCI）都要启动到 `desktop_ready`；AHCI 写能力另用
  "启动前后 `md5sum hd.img` 发生变化" 验证确实落盘。
- 启动日志新增 `fat32: blockdev index <n>`，可直接看出卷是从哪块设备读的
  （`-machine pc` 下是 `ata0`，`-machine q35` 下是 `ahci0`）。
- 批量读路径由 19.7 MB 字体加载覆盖（64 KB = 128 扇区，超过 AHCI 的 32 扇区
  上限，必须靠切块才能过）。

### USB / xHCI（本轮修复；已在 QEMU 三种启动形态下实地通过，见 P0 第 1 条）

| 位置 | 问题 | 修法 |
| --- | --- | --- |
| `drivers/usb/xhci.c` `xhci_address_device()` | 把 `g_dev_context[slot-1]` 当作 Address Device 命令的**输入上下文**传入。规范要求 TRB 参数指向**独立的 Input Context**，xHC 再从中把 Slot/EP Context 复制进 Device Context。Device Context 当输入用，真机上命令必然失败 | 新增 `g_input_context[XHCI_MAX_DEVICES][264]`（Input Control Context + Slot Context + 31×EP Context）并按规范填充：Input Control Context 的 Add Flags 置 `SLOT｜EP0`；Slot Context 填 Speed / Root Hub Port Number / Context Entries=1 / Device Address=0；EP0 Context 填 State=Running、CErr=3、Type=Control Bidirectional、Max Packet Size（SuperSpeed 512 / High 64 / Full·Low 8）、TR Dequeue Pointer + DCS。TRB 参数改指向它 |
| `drivers/usb/xhci.c` | 非 0 端点完全没有 Configure Endpoint，且 `g_xfer_ring[]` 是**每 slot 一条**环、语义是 EP0 控制环，被 `xhci_bulk_transfer()` 复用。规范要求每端点一条独立环与独立 dequeue 指针，共用会让控制器停在与预期不同的端点上 | 传输环改为按 **DCI**（Device Context Index）索引：`g_xfer_ring[设备][DCI][槽]`，enqueue 指针与 cycle 位同步按 DCI 分开；新增 `xhci_configure_endpoint()`，为每个非 0 端点建环并填写 EP Context（State / Type / MPS / Interval / Max Burst / DCS / Avg TRB Length），Slot Context 的 Context Entries 更新为最高 DCI |
| `drivers/usb/xhci.c` | PORTSC 基址写成 `0x40`，实际应为 **`0x400`**（xHCI 1.1 表 5-26；Linux `struct xhci_op_regs` 里端口寄存器在 0x3C 起 0xF0 个 dword 的保留洞之后）。所有端口寄存器访问都落在运算块的保留区 → 读回 0、写不进去 → 根端口永远"无设备"、端口复位也没反应 | 改为 `0x400`，并把 `portsc_reg()` 的返回类型加宽到 `uint64_t` |
| `drivers/usb/xhci.c` | 中断器 0 寄存器偏移整体错位一格：`ERSTSZ0` 写成 `0x24`（实为 IMOD），`ERSTBA0` 写成 `0x28`（实为 ERSTSZ），**ERSTBA 根本没有被编程**。事件环段表基址为空 → 控制器不可能产生任何事件 → 所有命令与传输等待只能超时 | 按规范改为 `IMAN0=0x20` / `IMOD0=0x24` / `ERSTSZ0=0x28` / `ERDP0=0x30` / `ERSTBA0=0x38` |
| `drivers/usb/xhci.c` | `xhci_hc_reset()` 在复位完成后等待 `USBSTS.HCH == 0`。规范 §4.2 规定复位完成时控制器处于 **Halted** 状态、`HCH` 就是 1，这个等待**永远不可能满足** → `hc_running` 恒为 false → 根端口从不轮询、slot 从不使能、Address Device 与 Configure Endpoint 从不发出，整个 USB 栈是死的 | 删除该等待；复位完成判据改为"HCRESET 自清零"，并接受"控制器已报 Halted"作为第二判据（自清零的读回并非所有实现都可靠）。另用 DCBAAP 回读剔除"寄存器根本不响应"的情况 |
| `drivers/usb/xhci.c` | 复位失败时直接放弃整个 bring-up。写入 HCRESET 后控制器其实已经复位，因为一个不可靠的回读判据而放弃，等于丢掉一个可用的控制器 | 复位判据放宽为"自清零 **或** 已报 Halted"；两者都无才判失败，并把 `usbcmd`/`usbsts` 原始值打出来 |
| `drivers/usb/xhci.c` | 命令与传输只判断"事件有没有来"，完全不看 **Completion Code**（status 位 31:24）。被拒绝的 Address Device 与成功的 Address Device 在日志里长得一模一样 | 新增 `xhci_completion_code()` / `xhci_completion_name()` / `xhci_report_completion()`；`enable slot` / `address device` / `configure endpoint` / 控制传输 / 批量传输全部解码完成码并区分成功、短包与失败 |
| `drivers/usb/xhci.c` | MMIO BAR 用 `mmu_map_identity()` 映射，即**可缓存**。写入可能停在 cache 里到不了控制器，读回则是陈旧数据 | 改用 `mmu_map_device_identity()`（PWT+PCD）。树内其它能工作的驱动（AHCI、virtio、帧缓冲、LAPIC）本来就是这么做的 |
| `drivers/usb/xhci.c` | 只读 `bar0`，忽略 64 位 BAR 的高 32 位。xHCI 的 BAR 就是 64 位的，OVMF 实测会把 xHCI BAR 放到 **`0xC000000000`**（4 GiB 以上）；旧代码会得到 0 并报 `bar missing` | 新增 `xhci_bar_base(bar_lo, bar_hi)`，按 BAR 类型位（bits 2:1 == 0b10）拼出完整 64 位基址；`xhci_info_t.mmio_base` 与寄存器基址变量一并加宽到 64 位 |
| `kernel/arch/mmu.c` `mmu_map_identity_flags()` | `>= 8GiB` 分支**不检查索引上界**。`pd*` 表都是 `uint64_t[512]`，覆盖 0–9 GiB；映射 0xC000000000 会算出索引 406528，向 `pd7b` 之后约 3 MiB 处写入 → **内存破坏**（实测 UEFI+q35+qemu-xhci 下直接崩到重启） | 所有分支统一做上界检查，越界即停止并返回"未完整映射"；`mmu_map_identity()` / `mmu_map_device_identity()` 改为返回 `bool`，调用方可判断是否真的映射成功 |
| `drivers/usb/xhci.c` | 同上：64 位 BAR 落在内核页表覆盖不到的高地址时，没有拒绝路径 | 用 `mmu_map_device_identity()` 的返回值判定，失败则报 `xhci: mmio bar outside mapped window` 并退出（打出 bar 高低 32 位），不再触碰那块地址 |
| `drivers/storage/nvme.c` | 同类问题：NVMe BAR 也用 `mmu_map_identity()` 映射（可缓存），且不检查映射结果。NVMe BAR 同样是 64 位的 | 改用 `mmu_map_device_identity()` 并检查返回值，失败则报 `nvme: mmio bar outside mapped window` |
| `drivers/usb/usb_ext.c` | **`usb_probe()` 从未被调用**。它是"遍历根端口 → 读设备描述符 → Address Device → Configure Endpoint → HID/MSC 热插拔"这条链路的唯一入口，`usb_ext_init()` 只做了 memset 与状态刷新。结果整条 USB 设备栈是死代码：控制器起来了也不会有任何设备被枚举 | 在 `usb_ext_init()` 末尾调用 `usb_probe()` |
| `drivers/usb/usb_ext.c` | 枚举时遍历**全部**可能的 slot id 去发 GET_DESCRIPTOR，对没使能的 slot 也会发；现在每个失败都会报一条"slot not enabled"完成码，把健康启动刷成一片失败 | 改为只遍历 `xhci_device_count()` / `xhci_get_device()` 报告的实际已寻址设备 |
| `drivers/usb/usb_ext.c` | 端点只用写死的 `ep_in=0x81` / `ep_out=0x01` 猜测，从不读配置描述符，也从不发 Configure Endpoint | 新增 `usb_configure_device()`：先读 9 字节配置描述符头拿 `wTotalLength`，再读整块；按 `bLength` 走描述符链收集端点（传输类型直接取自 `bmAttributes` bits 1:0，与 xHCI 编码一致；Max Packet 取 `wMaxPacketSize` bits 10:0，burst 取 bits 12:11）；发 SET_CONFIGURATION；再调 `xhci_configure_endpoint()`；并把真实 `bEndpointAddress` 写回设备表 |
| `drivers/usb/xhci.c` `xhci_bulk_transfer()` | 端点参数语义不统一：蓝牙栈传"端点号 + `is_in`"，HID 栈传**地址字节** `0x81`。后者被当成端点号 129 用，`endpoint*2` 算出垃圾门铃号并挂死 | 参数统一为"低 4 位是端点号，方向 = `is_in` 或地址字节 bit7"；DCI = `端点号*2 + (IN?1:0)`，同时用于选环与敲门铃，两者必须一致。`xhci_read/write()` 强制方向位 |
| `kernel/arch/mmu.c` | 内核页表只覆盖 0-9 GiB，9 GiB 以上的设备 BAR（OVMF 下 xHCI BAR = `0xC000000000`）只能"安全拒绝"，等于放弃控制器 | 新增按需页表池（`MMU_HIGH_PDPT/PD_MAX`）：`mmu_map_identity_flags()` 对 9 GiB-512 GiB 的低半区地址动态分配 PDPT/PD 表并装 2 MiB 大页，供设备映射用；映射失败仍如实返回 false。守卫方向为"拒绝内核高半区（PML4 ≥ 256）"而非低半区 |
| `drivers/usb/usb_ext.c` 枚举 | 设备表 `iface_class` 填的是 `bDeviceSubClass`（设备子类），而非接口描述符的 `bInterfaceClass`。绝大多数 HID / 大容量存储设备 `bDeviceClass == 0`（类放在接口上），于是全部被当成"class 0 的 per-interface 设备"，**类驱动永远不会认领** —— 枚举"成功"但键盘无输入、U 盘无盘符 | `usb_configure_device()` 走描述符链时抓取首个接口描述符的 `bInterfaceClass/SubClass` 并经出参带回；`usb_probe()` 用它填 `iface_class`，日志按"设备类为 0 时优先打接口类"输出；`usbdev_hotplug()` 同步扩展 |
| `drivers/usb/usb_ext.c` MSC 自动挂载 | MSC 注册后立即 `file_mount(<盘符>, "fat32", -1)`，而 USB MSC **尚未接入块设备层**（bulk-only 传输没有 blockdev 桥），`file_mount` 实际探测到的是**内置启动盘** —— 同一 FAT 卷被挂两次、WAL 恢复跑两遍，属于有害写路径 | 挂载结果如实记录：没有 blockdev 桥时打 `usb: mass-storage awaiting blockdev bridge (no drive letter yet)`，不占用盘符、不发"已挂载"通知；`usb_msc_mount_all()` 同样只在真挂上时才推进 `g_next_drive` |
| `drivers/usb/hid.c` `hid_probe()` | 设备种类（键盘/鼠标）用 `product_id & 0x01` **猜**，而 QEMU 的 usb-kbd / usb-tablet 共享同一 vendor:pid，猜错不可避免 | `usb_configure_device()` 抓取接口描述符的 `bInterfaceProtocol`（1=键盘 2=鼠标）与 `bInterfaceSubClass`（1=boot 协议）；协议为 0 的设备（如 usb-tablet）再读报告描述符嗅探 Usage（Mouse→鼠标）。`hid_device_connected()` 按 slot 去重，避免重复探测重复弹通知 |

迁移与验证：

- 判定依据是 QEMU 差分测试（同一镜像 `-machine pc` 与 `-machine q35`，
  各自再叠 `-device qemu-xhci` + `usb-kbd`）都要启动到 `desktop_ready`
  且无 BSOD。`hd.img` / `hd_uefi.img` 两种固件路径都要过。
- UEFI + q35 + `qemu-xhci` 这一格是**必须**的：OVMF 把 xHCI BAR 放到
  `0xC000000000`，同时覆盖"64 位 BAR 拼接"与"高地址页表池按需映射"
  两条路径。旧代码这一格会因越界写而崩机；只做安全拒绝的版本则报
  `mmio bar outside mapped window` 而放弃控制器。
- xHCI 软件栈的实地通过证据见 P0 第 1 条 —— 当前三种启动形态全部
  `desktop_ready`，设备枚举与分类完整。

### 签名与信任链

| 位置 | 问题 | 修法 |
| --- | --- | --- |
| `kernel/net/trust_store.c` | 装载顺序隐含依赖"签名证书文件名全零 ⇒ 排在最前"。容量 64 装满后剩余记录被静默丢弃，一旦命名规则或证书数量变化，签名根可能落在容量之外，导致 `driver_manager` 验签失败并 BSOD | 改两遍装载：第一遍只收带 `codeSigning` 扩展密钥用法 OID 的根，第二遍才用剩余槽位收普通根。签名根是否装上只取决于它自己的扩展字段 |
| `kernel/net/trust_store.c` | 每条记录各做一次 `file_read_at`（556 条 = 上千次路径解析+缓存查找），且无法回头做第二遍筛选 | 整个 bundle 一次读入内存（上限 4MB）后全部在内存里解析 |
| `kernel/net/trust_store.c` | `record_count` 直接来自文件头且未校验，损坏的头会导致超长循环；"加不下"与"记录越界"混在同一段 return 里，误报成 `truncated certificate record` | 用 `(bundle_size - header) / 6` 约束记录数；区分"容量不足"与"解析失败"两种告警；新增 `code-signing roots` 日志（为 0 时显式告警） |

### 构建系统

| 位置 | 问题 | 修法 |
| --- | --- | --- |
| `Makefile:612`、`user/apps/mingw/Makefile:37,50` | 递归调用 `$(MAKE)` 未加引号，而 Windows 下 make 路径含括号（`D:/Program Files (x86)/...`），sh 报 `syntax error near unexpected token '('`，导致 `out/mingw_hello.exe` 构建失败 | 改为 `"$(MAKE)"`（Linux 下 `"make"` 同样正常） |
| `Makefile:752-756`（`clean`） | Windows 分支用 CMD 语法 `if exist out cmd /c del ...`，但配方实际由 POSIX shell 执行（同文件其他配方已用 `mkdir -p`、`[ ! -f ]`），在 Git Bash 下直接语法错误，`out/` 不会被清理 | 统一为 POSIX `rm -rf out` |
| `Makefile:163-169`（`$(OUT_DIR)`） | 同类问题：Windows 分支用 `if not exist ... mkdir ...`，clean 之后首次构建即失败 | 统一为 `mkdir -p` |
| `Makefile:668-674`（`hd.vmdk`） | 同类问题：Windows 分支用 `if exist ... cmd /c del ...` | 统一为 `rm -f` |
| `tools/sign_driver_sys.py`（`export_windows_certificate`） | `make -j` 下多个 `.sys` 并行签名时争抢同一个证书导出目标，`Export-Certificate -Force` 因文件被占用而失败，导致部分 `*.sys` 构建中断 | 导出结果缓存复用（存在即跳过）+ 写进程私有临时文件 + `os.replace` 原子替换；失败时若目标已由其他进程产出则接受 |

### 启动链（曾导致开机黑屏 / BSOD，详见 `docs/boot_chain.md`）

| 位置 | 问题 | 修法 |
| --- | --- | --- |
| `kernel/arch/boot/include/fat32hdr.inc` | BPB 几何写死为 `FATSz=1576` / `TotSec32=202752`，而 `mkfat32.py` 实际生成 `FATSz32=4096` / `TotSec32=524288`。`boot.asm` 于是认为根目录在扇区 3184（实际 8224），找不到 `LOADER.BIN` 而落入 `LABEL_NO_LOADERBIN: jmp $` 死循环 —— **开机黑屏、串口零输出、BIOS 镜像完全无法启动** | ①`boot.asm` / `loader.asm` 改为**运行时从 BPB 读几何**；②头文件数值与 `mkfat32.py` 对齐；③`mkfat32.py` 增加几何一致性校验，不一致直接构建失败 |
| `Makefile`（`$(OUT_DIR)`、`clean`、`hd.vmdk`） | 三处 Windows 分支用 CMD 语法（`if not exist`、`if exist ... cmd /c del`），但配方由 POSIX shell 执行（同文件他处已用 `mkdir -p`、`[ ! -f ]`）。Git Bash 下 `out/` 建不出来、也清理不掉 | 统一为 POSIX（`mkdir -p` / `rm -rf` / `rm -f`） |
| `lib/file.c`（`file_auto_mount`） | 无条件相信 MBR 分区表。裸卷 VBR 的 446..509 字节其实是引导代码，被误读成一个合法分区项（`type=0x7D`、`lba=2112208600`），于是去挂载不存在的分区并失败 → 文件系统完全挂不上 → 读不到 `kernel.exe` 做签名校验 → **BSOD: KERNEL SIGNATURE INVALID** | 分区候选全部挂不上时回落到"整盘 LBA 0"（`file_try_mount_raw_volume`），放在分区之后，分区盘行为不变且不会重复挂载 |
| `lib/file.c`、`fs/fat32.c`、`kernel/net/trust_store.c` | 挂载失败与信任库失败在日志里**完全没有可见性**，只能看到一句 `filesystem mount failed` / `truncated certificate record` | 补齐判据日志（`fs:` / `fat32:` / `trust-store:` 前缀）：底层扇区读是否成功、0xAA55 是否存在、BPB 各字段、解析到第几条记录、信任根装了几个 |
| `tools/mkfat32.py` | `assets/` 载荷（字体 19.7 MB + 壁纸 + 音频 + 信任根）超过 256 MiB 卷容量时**不报错**，把内部 `self.data` 越界撑大：镜像文件比 BPB 声明的卷还大，簇位置从溢出点起全部错位，**末尾文件静默损坏**；另有数据区末端没有为 FAT32 驱动保留卷尾 512 扇区 WAL 的问题 | ①卷扩容到 384 MiB（`TotSec32=786432`、`FATSz32=8192`，`fat32hdr.inc` 同步）；②`alloc_cluster()` / `write_cluster()` 做数据区上界检查（含 WAL 保留区），越界直接 `RuntimeError` 构建失败；③`save()` 落盘大小必须精确等于卷大小，否则报错 |

