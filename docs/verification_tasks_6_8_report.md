# Monios x64 验证任务交付说明（任务 6 / 7 / 8）

日期：2026-09-24
范围：网络连通性验证、音频播放验证、图形渲染验证。

---

## 1. 交付物清单

### 1.1 新增应用（user/apps/，PE，x86_64-w64-mingw32-gcc）
| 文件 | 用途 |
|---|---|
| `user/apps/nettest.c` | `nettest ping/http/dns/ifconfig` |
| `user/apps/audiotest.c` | `audiotest play/tone/info` |
| `user/apps/gfxtest.c` | `gfxtest bench/windows/animate/info` |

### 1.2 新增 Host 端测试（tests/，MinGW gcc，纯 host 运行）
| 文件 | 用例数 | 结果 |
|---|---|---|
| `tests/test_net_proto.c` | 31 checks（IP 校验和 / TCP 校验和 / DNS 编码 / IP 文本） | 31 passed, 0 failed |
| `tests/test_wav.c` | 41 checks（8/16bit、单/立体声、多采样率、错误路径） | 41 passed, 0 failed |
| `tests/test_gfx_logic.c` | 22 checks（矩形相交/裁剪、RGB565↔888↔RGBA、Bresenham） | 22 passed, 0 failed |

### 1.3 内核侧改动
| 文件 | 改动 |
|---|---|
| `include/syscall.h` | 新增 `SYS_NET_PING=70`、`SYS_NET_RESOLVE=71`（紧接现有最大值 69，无冲突） |
| `kernel/syscall/syscall.c` | 在 `SYS_PACKAGE_CTL`(69) 之后、`default:` 之前新增两个 case 处理函数 |

**没有新增内核 .o**：两个新 syscall 直接复用已链接的 `net.o`（`net_ping`、`net_resolve_ipv4`、`app_memory_*` 辅助函数），因此 `KERNEL_OBJS` 无需改动。
**没有改 monios.dll / appsys.c / appsys.h**：应用通过已导出的 `monios_syscall1/2/3` 直接发起新号系统调用，避免重建 DLL 导入链。

---

## 2. 新系统调用语义

| 号 | 名称 | 参数 | 返回 |
|---|---|---|---|
| 70 | `SYS_NET_PING` | `rbx` = NUL 结尾主机串（IPv4 或域名） | 0=收到 ICMP echo reply；-1=失败/离线 |
| 71 | `SYS_NET_RESOLVE` | `rbx`=主机名，`rcx`=用户 `char[16]` 缓冲，`rdx`=缓冲大小 | 0=解析成功并写入点分十进制；-1=失败 |

两个处理函数都做了 `exec_active()` 用户空间拷贝（`app_memory_copy_string_from_user` / `app_memory_user_range` / `app_memory_copy_to_user`），与现有 `SYS_HTTP_GET_URL`、`SYS_SOCKET_CALL` 同样的安全模型；非 exec 路径直接访问。IP 点分格式化在内核 freestanding 环境下手写（无 snprintf）。

### 各命令复用的既有能力
- `nettest http` → 既有 `SYS_HTTP_GET_URL`(43) → `http_get_url()`。
- `nettest ifconfig` → 既有 `SYS_SYSTEM_STATUS`(18) → `app_system_status_t.net_*`（IP/网关/DNS/MAC/收发包数/ping 计数）。
- `audiotest play/tone` → 既有 `SYS_AUDIO_PLAY_PCM`(21) → `audio_play_pcm()`。
- `audiotest info` → 既有 `SYS_SYSTEM_STATUS`(18) → `audio_playing/paused/present/volume/driver/track`。
- `gfxtest bench/windows/animate` → 既有 `SYS_GRAPHICS_FILL_RECT/DRAW_TEXT/PRESENT`(19/35/20)。
- `gfxtest info` → `SYS_GRAPHICS_GET_WIDTH/HEIGHT`(38/39) + `SYS_SYSTEM_STATUS` 的 `gpu_submits/presents/pending/wm_windows`。

---

## 3. Makefile 需要添加的内容（**本次未修改 Makefile**）

### 3.1 应用对象清单（在 `APP_*_OBJS` 区块附近）
```make
APP_NETTEST_OBJS    = $(APP_RUNTIME_OBJS) out/app_nettest.pe.o    out/app_resource.pe.o
APP_AUDIOTEST_OBJS  = $(APP_RUNTIME_OBJS) out/app_audiotest.pe.o  out/app_resource.pe.o
APP_GFXTEST_OBJS    = $(APP_RUNTIME_OBJS) out/app_gfxtest.pe.o    out/app_resource.pe.o
```

### 3.2 把三个 .pe.o 加入 `APP_USER_OBJS`
```make
APP_USER_OBJS = $(APP_RUNTIME_OBJS) out/app_demo.pe.o ... out/app_appdev.pe.o \
                out/app_nettest.pe.o out/app_audiotest.pe.o out/app_gfxtest.pe.o
```
（`out/app_%.pe.o : user/apps/%.c` 是已有模式规则，会自动编译三个新 .c，无需新规则。）

### 3.3 链接目标（参照 out/square.exe 的 console 子系统写法）
```make
out/nettest.exe   : user/apps/app.ld $(APP_NETTEST_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/nettest.exe   $(APP_NETTEST_OBJS)   $(APP_LD_LIBS)
out/audiotest.exe : user/apps/app.ld $(APP_AUDIOTEST_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/audiotest.exe $(APP_AUDIOTEST_OBJS) $(APP_LD_LIBS)
out/gfxtest.exe   : user/apps/app.ld $(APP_GFXTEST_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/gfxtest.exe   $(APP_GFXTEST_OBJS)   $(APP_LD_LIBS)
```

### 3.4 加入 `APP_EXE_TARGETS`、`ISO_PAYLOAD_TARGETS`
在这两个变量末尾追加：`out/nettest.exe out/audiotest.exe out/gfxtest.exe`

### 3.5 `hd.img` 与 `hd_uefi.img` 的 mkfat32 拷贝条目
在两条 `mkfat32.py ...` 命令末尾追加（与现有 demo.exe 同路径）：
```
--copy out/nettest.exe:/Monios/Apps/nettest.exe
--copy out/audiotest.exe:/Monios/Apps/audiotest.exe
--copy out/gfxtest.exe:/Monios/Apps/gfxtest.exe
```

### 3.6 Host 测试的编译方式（不进内核构建，可在 tests/build_test.bat 追加）
```bat
gcc -std=gnu17 -fno-builtin -Wall -Wno-unused-parameter -Wno-unused-function -g ^
    -I ..\include -I . -o test_net_proto.exe test_net_proto.c stubs.c
gcc -std=gnu17 -fno-builtin -Wall -g -o test_wav.exe test_wav.c
gcc -std=gnu17 -fno-builtin -Wall -g -o test_gfx_logic.exe test_gfx_logic.c
```
（test_wav / test_gfx_logic 是自包含的，不需要 stubs.c；test_net_proto 依赖 stubs.c 的 memcpy/strcpy 等桩函数，与 test_vfs 同模式。）

---

## 4. 构建验证结果

| 验证项 | 命令 | 结果 |
|---|---|---|
| 内核 syscall.c 编译 | `x86_64-elf-gcc ... -c -o out/syscall.o kernel/syscall/syscall.c` | EXIT=0 |
| nettest.c 编译 | `x86_64-w64-mingw32-gcc $(APP_CFLAGS) -o out/app_nettest.pe.o` | EXIT=0 |
| audiotest.c 编译 | 同上 → `out/app_audiotest.pe.o` | EXIT=0 |
| gfxtest.c 编译 | 同上 → `out/app_gfxtest.pe.o` | EXIT=0（首次漏 include syscall.h，已补） |
| nettest.exe 链接 | 按 Makefile 链接命令手动链接 | EXIT=0，38220 字节 |
| audiotest.exe 链接 | 同上 | EXIT=0，37535 字节 |
| gfxtest.exe 链接 | 同上 | EXIT=0，38373 字节 |
| test_net_proto 运行 | gcc 编译 + 直接运行 | **31 passed, 0 failed** |
| test_wav 运行 | gcc 编译 + 直接运行 | **41 passed, 0 failed** |
| test_gfx_logic 运行 | gcc 编译 + 直接运行 | **22 passed, 0 failed** |

关键硬编码已知答案（KAT）已校验：
- RFC1071 教科书 IP 头 `45 00 00 73 ... c0 a8 00 c7`（校验和字段清零）→ `ip_checksum == 0xB861`，填回后整头校验和 == 0。
- TCP 伪头+段校验和：计算→回填→重算自洽为 0，篡改一字节后 != 0。
- DNS 编码 `www.example.com` → `03www 07example 03com 00`（17 字节）。

---

## 5. 关于既有 VFS 回归测试（约束 #2）

我**没有**修改 `tests/test_vfs.c`、`tests/stubs.c`、`tests/stub_values.h`，也没有修改它 include 的任何头（common/file/fs_cache/ntfs/path/string）。

实测：本会话用官方 `tests/build_test.bat` 重新编译并运行 `test_vfs.exe`，进程在打印首行之前即崩溃，退出码 `0xC0000096`（STATUS_PRIVILEGED_INSTRUCTION）。同一 shell 中其它 host 套件均可正常运行：
- `test_fs.exe` → 76 passed, 0 failed
- `test_net.exe` → 84 passed, 0 failed
- 本次新增三件 → 全部通过

因此该崩溃发生在工作区已被大量改动的 `lib/file.c` / `lib/path.c` 链接代码内（git status 显示这两个文件及其头在本任务之前就已是 modified 状态），**与本次新增的 syscall / 应用 / 测试无编译或链接关系**。本次改动未触碰任何 VFS 源文件，不构成对 119 项 VFS 用例的破坏；如需恢复 VFS host 可执行，应单独排查 `lib/file.c`、`lib/path.c` 当前工作区版本。

---

## 6. 设计要点 / 备注

- **零新内核对象、零 DLL 重建**：ping/resolve 直接落到已有 `net_ping`/`net_resolve_ipv4`；ifconfig/audio-info/gpu-info 全部走既有 `SYS_SYSTEM_STATUS`，最大化复用、最小化 blast radius。
- **应用打印**：使用应用运行时自带的 `printf`/`print_uint`/`fputs`（支持 %u/%d/%s/%x/%c，无浮点），tone 用 256 项泰勒正弦表在 16.8 定点相位上合成，不依赖 libm。
- **WAV 解析**：`audiotest play` 与 `tests/test_wav.c` 共用同一套 RIFF/fmt/data 遍历逻辑（跳过分块、word 对齐填充、校验 audio_format==1 且位深 8/16、声道 1/2）。
- **gfxtest FPS**：帧率按系统 tick 粗算（假设 100Hz tick，输出标注 approx），同时输出 min/max/avg 帧 ticks 与绘制操作数，便于在 QEMU 里观察。
- 三个应用均为 console 子系统（与 demo/square 一致），便于在串口/终端直接看输出。
