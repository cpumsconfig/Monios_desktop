ifeq ($(OS),Windows_NT)
HOST_OS := windows
PYTHON ?= python
X86_64_CC ?= x86_64-elf-gcc
X86_64_LD ?= x86_64-elf-ld
X86_64_MINGW_LD ?= ld
APP_WINDRES ?= windres
UI_FONT_SOURCE ?= C:/Windows/Fonts/msyh.ttc
WINDOWS_CURSOR_SOURCE ?= C:/Windows/Cursors/aero_arrow.cur
QEMU_EFI_CODE ?= C:/Program Files/qemu/share/edk2-x86_64-code.fd
QEMU_AUDIO_BACKEND ?= -audiodev dsound,id=audio0
else
HOST_OS := linux
PYTHON ?= python3
X86_64_CC ?= $(if $(shell command -v x86_64-elf-gcc 2>/dev/null),x86_64-elf-gcc,gcc)
X86_64_LD ?= $(if $(shell command -v x86_64-elf-ld 2>/dev/null),x86_64-elf-ld,ld)
X86_64_MINGW_LD ?= $(if $(shell command -v x86_64-w64-mingw32-ld 2>/dev/null),x86_64-w64-mingw32-ld,ld)
APP_WINDRES ?= x86_64-w64-mingw32-windres
UI_FONT_SOURCE ?= /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
WINDOWS_CURSOR_SOURCE ?=
QEMU_EFI_CODE ?= $(firstword $(wildcard /usr/share/OVMF/OVMF_CODE.fd /usr/share/edk2/x64/OVMF_CODE.fd /usr/share/edk2/ovmf/OVMF_CODE.fd /usr/share/qemu/edk2-x86_64-code.fd))
QEMU_AUDIO_BACKEND ?= -audiodev driver=none,id=audio0
endif

X86_64_CC += -fno-pie -fno-pic -Wall -Wextra
APP_CC ?= x86_64-w64-mingw32-gcc
APP_LD ?= $(APP_CC)
APP_CFLAGS = -c -I include -I user/lib -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -mabi=sysv -fno-pie
APP_LDFLAGS = -nostdlib -nodefaultlibs -nostartfiles -Wl,-T,user/apps/app.ld -Wl,--entry,_start -Wl,--image-base,0x03fff000
DRIVER_LDFLAGS = -nostdlib -nodefaultlibs -nostartfiles -Wl,-T,user/apps/driver.ld -Wl,--entry,DriverEntry -Wl,--image-base,0x05000000 -Wl,--enable-reloc-section
APP_LD_LIBS = -Lout -lconsole -losui -lwindows -lmonios
APP_IMPORT_LIBS = out/libmonios.dll.a out/libconsole.dll.a out/libwindows.dll.a out/libosui.dll.a
APP_DLL_LDFLAGS = -nostdlib -nodefaultlibs -nostartfiles -shared
APP_DLL_MONIOS_LDFLAGS = $(APP_DLL_LDFLAGS) -Wl,--image-base,0x04100000
APP_DLL_CONSOLE_LDFLAGS = $(APP_DLL_LDFLAGS) -Wl,--image-base,0x04200000
APP_DLL_WINDOWS_LDFLAGS = $(APP_DLL_LDFLAGS) -Wl,--image-base,0x04300000
OUT_DIR = out
NASM ?= nasm
VMWARE_VMX ?= D:\Users\xiaot\Documents\Virtual Machines\monios_x64\monios_x64.vmx
UI_FONT_IMAGE = out/msyh.ttc
UEFI_FONT_SOURCE ?= $(UI_FONT_SOURCE)
UEFI_FONT_IMAGE = out/uefi_msyh.ttc
BOOT_IMAGE = assets/boot.bmp
WINDOWS_CURSOR_IMAGE = out/arrow.cur
TRUST_ROOT_BUNDLE = out/trust-roots.bin
TRUST_ROOT_FILES = $(wildcard assets/cent/*.crt)
QEMU ?= qemu-system-x86_64
QEMU_IMG ?= qemu-img
QEMU_AUDIO_PCI ?= $(QEMU_AUDIO_BACKEND) -device AC97,audiodev=audio0
QEMU_AUDIO ?= $(QEMU_AUDIO_BACKEND) -machine pcspk-audiodev=audio0 -device AC97,audiodev=audio0
QEMU_DISPLAY ?= -display gtk,zoom-to-fit=on
QEMU_NET ?= -netdev user,id=net0 -device e1000,netdev=net0
ifeq ($(HOST_OS),windows)
QEMU_UEFI_PFLASH = -drive "if=pflash,format=raw,readonly=on,file=$(QEMU_EFI_CODE)"
else
ifneq ($(strip $(QEMU_EFI_CODE)),)
QEMU_UEFI_PFLASH = -drive if=pflash,format=raw,readonly=on,file=$(QEMU_EFI_CODE)
else
QEMU_UEFI_PFLASH =
endif
endif
QEMU_INSTALL_DISK ?= out/install_target.img
QEMU_ISO_IDE_BOOT = -drive if=none,id=install_disk,file=$(QEMU_INSTALL_DISK),format=raw -drive if=none,id=install_iso,file=out/monios_uefi_installer.iso,format=raw,media=cdrom -device ide-hd,bus=ide.0,unit=0,drive=install_disk,bootindex=2 -device ide-cd,bus=ide.1,unit=0,drive=install_iso,bootindex=1
QEMU_ISO_AHCI_BOOT = -device ich9-ahci,id=ahci -drive if=none,id=install_disk,file=$(QEMU_INSTALL_DISK),format=raw -drive if=none,id=install_iso,file=out/monios_uefi_installer.iso,format=raw,media=cdrom -device ide-hd,bus=ahci.0,drive=install_disk,bootindex=2 -device ide-cd,bus=ahci.1,drive=install_iso,bootindex=1

.DEFAULT_GOAL := default

.PHONY : boot boot_bios boot_install run run_bios run_debug run_install run_uefi run_uefi_debug run_uefi_iso run_uefi_iso_debug run_uefi_q35 run_uefi_q35_debug check_uefi uefi uefi_iso app-runtime drivers hello mingw-sample mingw-tools run_vmware clean default FORCE

APP_RUNTIME_OBJS = out/app_runtime.pe.o out/appsys.pe.o out/stdio.pe.o out/stdlib.pe.o out/unistd.pe.o out/string.pe.o
APP_RESOURCE_OBJS = out/app_resource.pe.o out/uac_resource.pe.o out/driver_resource.pe.o
APP_ICON_ASSETS = assets/icons/app.ico assets/icons/uac.ico assets/icons/driver.ico
APP_DEMO_OBJS = $(APP_RUNTIME_OBJS) out/app_demo.pe.o out/app_resource.pe.o
APP_EXPLORAR_OBJS = $(APP_RUNTIME_OBJS) out/app_explorar.pe.o out/app_resource.pe.o
APP_MONILOGON_OBJS = $(APP_RUNTIME_OBJS) out/app_monilogon.pe.o out/app_resource.pe.o
APP_PLAYER_OBJS = $(APP_RUNTIME_OBJS) out/app_player.pe.o out/app_resource.pe.o
APP_HTTPD_OBJS = $(APP_RUNTIME_OBJS) out/app_httpd.pe.o out/app_resource.pe.o
APP_BROWSER_OBJS = $(APP_RUNTIME_OBJS) out/app_browser.pe.o out/app_resource.pe.o
APP_NOTEPAD_OBJS = $(APP_RUNTIME_OBJS) out/app_notepad.pe.o out/app_resource.pe.o
APP_TASKMGR_OBJS = $(APP_RUNTIME_OBJS) out/app_taskmgr.pe.o out/app_resource.pe.o
APP_SQUARE_OBJS = $(APP_RUNTIME_OBJS) out/app_square.pe.o out/app_resource.pe.o
APP_CUBE3D_OBJS = $(APP_RUNTIME_OBJS) out/app_cube3d.pe.o out/app_resource.pe.o
APP_NETTEST_OBJS = $(APP_RUNTIME_OBJS) out/app_nettest.pe.o out/app_resource.pe.o
APP_AUDIOTEST_OBJS = $(APP_RUNTIME_OBJS) out/app_audiotest.pe.o out/app_resource.pe.o
APP_GFXTEST_OBJS = $(APP_RUNTIME_OBJS) out/app_gfxtest.pe.o out/app_resource.pe.o
APP_SMOKETEST_OBJS = $(APP_RUNTIME_OBJS) out/app_smoke_test.pe.o out/app_resource.pe.o
APP_IMAGEVIEW_OBJS = $(APP_RUNTIME_OBJS) out/app_imageview.pe.o out/zip.pe.o out/crc32.pe.o out/app_resource.pe.o
APP_SETTINGS_OBJS = $(APP_RUNTIME_OBJS) out/app_settings.pe.o out/app_resource.pe.o
APP_ZIP_OBJS = $(APP_RUNTIME_OBJS) out/app_zip.pe.o out/zip.pe.o out/crc32.pe.o out/app_resource.pe.o
APP_BACKUP_OBJS = $(APP_RUNTIME_OBJS) out/app_backup.pe.o out/app_resource.pe.o
APP_DOWNLOAD_OBJS = $(APP_RUNTIME_OBJS) out/app_download.pe.o out/app_resource.pe.o
APP_FTP_OBJS = $(APP_RUNTIME_OBJS) out/app_ftp.pe.o out/app_resource.pe.o
APP_HTTPD_OBJS = $(APP_RUNTIME_OBJS) out/app_httpd.pe.o out/app_resource.pe.o
APP_HELLO_CGI_OBJS = $(APP_RUNTIME_OBJS) out/app_hello_cgi.pe.o out/app_resource.pe.o
APP_SNAKE_OBJS = $(APP_RUNTIME_OBJS) out/app_snake.pe.o out/app_resource.pe.o
APP_TETRIS_OBJS = $(APP_RUNTIME_OBJS) out/app_tetris.pe.o out/app_resource.pe.o
APP_MINESWEEPER_OBJS = $(APP_RUNTIME_OBJS) out/app_minesweeper.pe.o out/app_resource.pe.o
APP_SOLITAIRE_OBJS = $(APP_RUNTIME_OBJS) out/app_solitaire.pe.o out/app_resource.pe.o
APP_MAIL_OBJS = $(APP_RUNTIME_OBJS) out/app_mail.pe.o out/app_resource.pe.o
APP_CHAT_OBJS = $(APP_RUNTIME_OBJS) out/app_chat.pe.o out/app_resource.pe.o
APP_SCREENSHOT_OBJS = $(APP_RUNTIME_OBJS) out/app_screenshot.pe.o out/png.pe.o out/crc32.pe.o out/app_resource.pe.o
APP_RECORD_OBJS = $(APP_RUNTIME_OBJS) out/app_record.pe.o out/png.pe.o out/crc32.pe.o out/app_resource.pe.o
APP_PERFMON_OBJS = $(APP_RUNTIME_OBJS) out/app_perfmon.pe.o out/app_resource.pe.o
APP_GDBGUI_OBJS = $(APP_RUNTIME_OBJS) out/app_gdbgui.pe.o out/app_resource.pe.o
APP_SYSMON_OBJS = $(APP_RUNTIME_OBJS) out/app_sysmon.pe.o out/app_resource.pe.o
APP_MEMMON_OBJS = $(APP_RUNTIME_OBJS) out/app_memmon.pe.o out/app_resource.pe.o
APP_CALCULATOR_OBJS = $(APP_RUNTIME_OBJS) out/app_calculator.pe.o out/app_resource.pe.o
APP_PDFVIEW_OBJS = $(APP_RUNTIME_OBJS) out/app_pdfview.pe.o out/zip.pe.o out/crc32.pe.o out/app_resource.pe.o
APP_FIREWALL_OBJS = $(APP_RUNTIME_OBJS) out/app_firewall.pe.o out/app_resource.pe.o
APP_TERMUX_OBJS = $(APP_RUNTIME_OBJS) out/app_termux.pe.o out/app_resource.pe.o
APP_DEFRAG_OBJS = $(APP_RUNTIME_OBJS) out/app_defrag.pe.o out/app_resource.pe.o
APP_MEMTEST_OBJS = $(APP_RUNTIME_OBJS) out/app_memtest.pe.o out/app_resource.pe.o
APP_HWINFO_OBJS = $(APP_RUNTIME_OBJS) out/app_hwinfo.pe.o out/app_resource.pe.o
APP_DRVMGR_OBJS = $(APP_RUNTIME_OBJS) out/app_drvmgr.pe.o out/app_resource.pe.o
APP_SYSRESTORE_OBJS = $(APP_RUNTIME_OBJS) out/app_sysrestore.pe.o out/app_resource.pe.o
APP_AUDIOREC_OBJS = $(APP_RUNTIME_OBJS) out/app_audiorec.pe.o out/app_resource.pe.o
APP_VIDEOPLAY_OBJS = $(APP_RUNTIME_OBJS) out/app_videoplay.pe.o out/app_resource.pe.o
APP_PRINTDOC_OBJS = $(APP_RUNTIME_OBJS) out/app_printdoc.pe.o out/app_resource.pe.o
DRIVER_RZDRV_OBJS = out/driver_rzdrv.pe.o
DRIVER_PACKAGE_NAMES = audio dma gpu pci ide ahci nvme cdrom storage_ext xhci usb_ext hid bluetooth hda es1371 aac e1000 pcnet smbus i2c i3c spi tpm mcb md virtio
DRIVER_SYS_TARGETS = $(addprefix out/,$(addsuffix .sys,$(DRIVER_PACKAGE_NAMES))) out/rzdrv.sys
DRIVER_IMAGE_COPIES = $(foreach d,$(DRIVER_PACKAGE_NAMES),--copy out/$(d).sys:/Monios/driver/$(d).sys) --copy out/rzdrv.sys:/Monios/driver/rzdrv.sys
APP_SYSINST_OBJS = $(APP_RUNTIME_OBJS) out/app_rzsinstall.pe.o out/uac_resource.pe.o
APP_SETUP_OBJS = $(APP_RUNTIME_OBJS) out/app_setup.pe.o out/uac_resource.pe.o
APP_APPDEV_OBJS = $(APP_RUNTIME_OBJS) out/app_appdev.pe.o out/app_resource.pe.o
APP_HELLO_OBJS = $(APP_RUNTIME_OBJS) out/app_hello.pe.o out/app_resource.pe.o
APP_MONIOS_DLL_OBJS = out/app_moniosdll.pe.o out/string.pe.o
APP_CONSOLE_DLL_OBJS = out/app_consoledll.pe.o
APP_WINDOWS_DLL_OBJS = out/app_windowsdll.pe.o
APP_OSUI_DLL_OBJS = out/app_osuidll.pe.o
APP_DLL_OBJS = $(APP_MONIOS_DLL_OBJS) $(APP_CONSOLE_DLL_OBJS) $(APP_WINDOWS_DLL_OBJS) $(APP_OSUI_DLL_OBJS)
MINGW_SAMPLE_DIR = user/apps/mingw
MINGW_SAMPLE_FILES = $(MINGW_SAMPLE_DIR)/Makefile $(MINGW_SAMPLE_DIR)/README.md $(MINGW_SAMPLE_DIR)/hello.c $(MINGW_SAMPLE_DIR)/hello.S
MINGW_SAMPLE_TARGET = out/mingw_hello.exe
MINGW_TOOL_SOURCES = $(MINGW_SAMPLE_DIR)/gcc.c $(MINGW_SAMPLE_DIR)/as.c $(MINGW_SAMPLE_DIR)/ld.c $(MINGW_SAMPLE_DIR)/selftest.c $(MINGW_SAMPLE_DIR)/tinyhello.c $(MINGW_SAMPLE_DIR)/toolchain_format.h $(MINGW_SAMPLE_DIR)/toolchain_util.h
MINGW_TOOL_TARGETS = out/mingw_gcc.exe out/mingw_as.exe out/mingw_ld.exe
MINGW_IMAGE_COPIES = --copy $(MINGW_SAMPLE_TARGET):/Monios/Apps/mingw/hello.exe --copy $(MINGW_SAMPLE_DIR)/Makefile:/Monios/Apps/mingw/Makefile --copy $(MINGW_SAMPLE_DIR)/hello.c:/Monios/Apps/mingw/hello.c --copy $(MINGW_SAMPLE_DIR)/hello.S:/Monios/Apps/mingw/hello.S --copy $(MINGW_SAMPLE_DIR)/README.md:/Monios/Apps/mingw/README.md --copy out/mingw_gcc.exe:/Monios/Apps/mingw/gcc.exe --copy out/mingw_as.exe:/Monios/Apps/mingw/as.exe --copy out/mingw_ld.exe:/Monios/Apps/mingw/ld.exe --copy $(MINGW_SAMPLE_DIR)/gcc.c:/Monios/Apps/mingw/gcc.c --copy $(MINGW_SAMPLE_DIR)/as.c:/Monios/Apps/mingw/as.c --copy $(MINGW_SAMPLE_DIR)/ld.c:/Monios/Apps/mingw/ld.c --copy $(MINGW_SAMPLE_DIR)/selftest.c:/Monios/Apps/mingw/selftest.c --copy $(MINGW_SAMPLE_DIR)/tinyhello.c:/Monios/Apps/mingw/tinyhello.c --copy $(MINGW_SAMPLE_DIR)/toolchain_format.h:/Monios/Apps/mingw/toolchain_format.h --copy $(MINGW_SAMPLE_DIR)/toolchain_util.h:/Monios/Apps/mingw/toolchain_util.h
MINGW_IMAGE_COPIES += --copy out/browser.exe:/Monios/Apps/browser.exe
DRIVER_USER_OBJS = out/driver_rzdrv.pe.o out/sysstub.pe.o
APP_USER_OBJS = $(APP_RUNTIME_OBJS) out/app_demo.pe.o out/app_explorar.pe.o out/app_monilogon.pe.o out/app_player.pe.o out/app_browser.pe.o out/app_notepad.pe.o out/app_taskmgr.pe.o out/app_square.pe.o out/app_cube3d.pe.o out/app_nettest.pe.o out/app_audiotest.pe.o out/app_gfxtest.pe.o out/app_smoke_test.pe.o out/app_imageview.pe.o out/app_settings.pe.o out/app_zip.pe.o out/app_backup.pe.o out/app_download.pe.o out/app_ftp.pe.o out/app_httpd.pe.o out/app_snake.pe.o out/app_tetris.pe.o out/app_minesweeper.pe.o out/app_solitaire.pe.o out/app_mail.pe.o out/app_chat.pe.o out/app_screenshot.pe.o out/app_record.pe.o out/app_perfmon.pe.o out/app_gdbgui.pe.o out/app_sysmon.pe.o out/app_memmon.pe.o out/app_calculator.pe.o out/app_pdfview.pe.o out/app_firewall.pe.o out/app_termux.pe.o out/app_defrag.pe.o out/app_memtest.pe.o out/app_hwinfo.pe.o out/app_drvmgr.pe.o out/app_sysrestore.pe.o out/app_audiorec.pe.o out/app_videoplay.pe.o out/app_printdoc.pe.o out/app_rzsinstall.pe.o out/app_setup.pe.o out/app_appdev.pe.o out/app_download.pe.o out/app_ftp.pe.o
APP_PUBLIC_HEADERS = include/syscall.h include/system_status.h include/driver_api.h include/driver_status.h user/lib/appsys.h user/lib/stdio.h user/lib/unistd.h user/lib/monios_dll.h user/lib/console_dll.h user/lib/windows_dll.h user/lib/osui_dll.h
APP_DLL_HEADERS = user/lib/monios_dll.h user/lib/console_dll.h user/lib/windows_dll.h user/lib/osui_dll.h
APP_EXE_TARGETS = out/demo.exe out/explorar.exe out/monilog.exe out/player.exe out/browser.exe out/notepad.exe out/taskmgr.exe out/square.exe out/cube3d.exe out/nettest.exe out/audiotest.exe out/gfxtest.exe out/smoke_test.exe out/imageview.exe out/settings.exe out/zip.exe out/backup.exe out/download.exe out/ftp.exe out/httpd.exe out/hello_cgi.exe out/snake.exe out/tetris.exe out/minesweeper.exe out/solitaire.exe out/mail.exe out/chat.exe out/screenshot.exe out/record.exe out/perfmon.exe out/gdbgui.exe out/sysmon.exe out/memmon.exe out/calculator.exe out/pdfview.exe out/firewall.exe out/termux.exe out/defrag.exe out/memtest.exe out/hwinfo.exe out/drvmgr.exe out/sysrestore.exe out/audiorec.exe out/videoplay.exe out/printdoc.exe out/rzdrv.unsigned.sys $(DRIVER_SYS_TARGETS) out/sysinst.exe out/setup.exe out/appdev.exe out/hello.exe $(MINGW_SAMPLE_TARGET) $(MINGW_TOOL_TARGETS)
ISO_PAYLOAD_TARGETS = out/loader.bin out/kernel.exe out/demo.exe out/explorar.exe out/monilog.exe out/player.exe out/browser.exe out/notepad.exe out/taskmgr.exe out/square.exe out/cube3d.exe out/nettest.exe out/audiotest.exe out/gfxtest.exe out/smoke_test.exe $(DRIVER_SYS_TARGETS) out/sysinst.exe out/setup.exe out/appdev.exe out/hello.exe $(MINGW_SAMPLE_TARGET) $(MINGW_TOOL_TARGETS) out/monios.dll out/console.dll out/windows.dll out/osui.dll out/monios.efi $(UEFI_FONT_IMAGE) $(TRUST_ROOT_BUNDLE) pwd.txt music_vm.wav bgm.m4a bgm.wav version.txt assets/wallpaper.jpg $(BOOT_IMAGE) $(WINDOWS_CURSOR_IMAGE)

KERNEL_BOOT_OBJS = out/kernel_entry.o out/kernel.o out/mmu.o out/common.o out/string.o out/console.o out/interrupt.o out/input.o
KERNEL_MEM_OBJS = out/leak_track.o out/memory.o out/bitmap.o out/pool.o out/heap.o out/frame.o out/buddy.o out/vma.o out/lazyalloc.o out/vmext.o out/page.o out/hugetlb.o out/cma.o out/gup.o out/zs.o out/zcomp.o out/memstats.o out/vm.o out/memtest.o
KERNEL_RUNTIME_OBJS = out/eevdf.o out/muqss.o out/scheduler.o out/schedopt.o out/pcb.o out/signal.o out/futex.o out/ipc.o out/pipe.o out/shm.o out/prsys.o out/task.o out/bsod.o out/iosched.o out/procmon.o out/dos.o
KERNEL_FS_OBJS = out/file.o out/fs_cache.o out/fat16.o out/fat32.o out/chkdsk.o out/iso9660.o out/ntfs.o out/extfs.o out/gpt.o out/netfs.o out/efs.o out/defrag.o
KERNEL_UI_OBJS = out/font.o out/graphics.o out/gui.o out/ui.o out/osui_component.o out/osui_theme.o out/osui_desktop.o out/gpu.o out/syscall.o out/proc_mgmt.o out/user_mgmt.o out/fs_perm.o out/app_memory.o out/shell.o out/hash.o out/base64.o out/path.o out/exec.o out/session.o out/browser.o
KERNEL_INPUT_OBJS = out/keyboard.o out/mouse.o out/usb.o out/xhci.o out/usb_ext.o out/hid.o out/msc.o out/bluetooth.o
KERNEL_PLATFORM_OBJS = out/bios.o out/gop.o out/rtc.o out/iic.o out/cpu.o out/pci.o out/dma.o out/ide.o out/ahci.o out/nvme.o out/cdrom.o out/blockdev.o out/storage_ext.o out/sdhci.o out/lpt.o out/installer.o out/audio.o out/aac.o out/mp3_dec.o out/hda.o out/es1371.o out/opengl.o out/vulkan.o out/nvidia.o out/igpu.o out/virtio.o out/virtio_blk.o out/acpi.o out/power.o out/cmos.o out/driver_manager.o out/device.o out/terminal.o out/smp.o out/opp.o out/od.o out/i2c.o out/i3c.o out/spi.o out/tpm.o out/mcb.o out/md.o out/ntp.o out/print.o out/uac.o out/audit.o
KERNEL_NET_OBJS = out/net.o out/dhcp.o out/lwip.o out/tls.o out/http.o out/wifi.o out/ip.o out/ipv4.o out/ipv6.o out/dns.o out/socket.o out/tcp.o out/e1000.o out/pcnet.o out/rtl8139.o out/virtio_net.o out/aes.o out/rsa.o out/x509.o out/authenticode.o out/trust_store.o out/firewall.o
KERNEL_DEBUG_OBJS = out/registry.o out/smbus.o out/intelbus1.o out/amdbus1.o out/gdb_stub.o out/crash_dump.o out/ftrace.o out/memdump.o out/profiler.o
KERNEL_OBJS = $(KERNEL_BOOT_OBJS) $(KERNEL_MEM_OBJS) $(KERNEL_RUNTIME_OBJS) $(KERNEL_FS_OBJS) $(KERNEL_UI_OBJS) $(KERNEL_INPUT_OBJS) $(KERNEL_PLATFORM_OBJS) $(KERNEL_NET_OBJS) $(KERNEL_DEBUG_OBJS)
KERNEL_HEADERS = $(wildcard include/*.h)
OUT_TARGETS = out/boot.bin out/loader.bin out/kernel.elf out/kernel.unsigned.exe out/kernel.exe out/demo.exe out/explorar.exe out/monilog.exe out/player.exe out/browser.exe out/notepad.exe out/taskmgr.exe out/square.exe out/cube3d.exe out/nettest.exe out/audiotest.exe out/gfxtest.exe out/smoke_test.exe out/imageview.exe out/settings.exe out/zip.exe out/backup.exe out/download.exe out/ftp.exe out/httpd.exe out/hello_cgi.exe out/snake.exe out/tetris.exe out/minesweeper.exe out/solitaire.exe out/mail.exe out/chat.exe out/screenshot.exe out/record.exe out/perfmon.exe out/gdbgui.exe out/sysmon.exe out/memmon.exe out/calculator.exe out/pdfview.exe out/firewall.exe out/termux.exe out/defrag.exe out/memtest.exe out/hwinfo.exe out/drvmgr.exe out/sysrestore.exe out/audiorec.exe out/videoplay.exe out/printdoc.exe $(DRIVER_SYS_TARGETS) out/sysinst.exe out/setup.exe out/appdev.exe out/hello.exe $(MINGW_SAMPLE_TARGET) $(MINGW_TOOL_TARGETS) out/monios.dll out/console.dll out/windows.dll out/osui.dll out/monios.efi out/monios_uefi_esp.img out/monios_uefi_installer.iso $(KERNEL_OBJS) $(APP_USER_OBJS) $(DRIVER_USER_OBJS) $(APP_DLL_OBJS) $(APP_RESOURCE_OBJS) $(UI_FONT_IMAGE) $(UEFI_FONT_IMAGE) $(TRUST_ROOT_BUNDLE)

$(KERNEL_OBJS): $(KERNEL_HEADERS)

$(OUT_TARGETS): | $(OUT_DIR)

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

out/%.bin : kernel/arch/boot/%.asm kernel/arch/boot/include/fat32hdr.inc kernel/arch/boot/include/load.inc kernel/arch/boot/include/pm.inc
	"$(NASM)" -I kernel/arch/boot/include -o out/$*.bin kernel/arch/boot/$*.asm

out/kernel_entry.o : kernel/arch/kernel_entry.asm
	"$(NASM)" -f elf64 -o out/kernel_entry.o kernel/arch/kernel_entry.asm

out/%.o : kernel/arch/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o kernel/arch/$*.c

out/%.o : kernel/mm/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o kernel/mm/$*.c

out/%.o : kernel/sched/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o kernel/sched/$*.c

out/%.o : fs/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o fs/$*.c

out/%.o : kernel/net/%.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o kernel/net/$*.c

out/%.o : kernel/ui/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o kernel/ui/$*.c

out/osui_component.o : kernel/ui/osui/component.c include/osui.h include/ui.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/osui_component.o kernel/ui/osui/component.c

out/osui_theme.o : kernel/ui/osui/theme.c include/osui.h include/registry.h include/ui.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/osui_theme.o kernel/ui/osui/theme.c

out/osui_desktop.o : kernel/ui/osui/desktop.c include/osui.h include/graphics.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/osui_desktop.o kernel/ui/osui/desktop.c

out/%.o : kernel/ipc/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o kernel/ipc/$*.c

out/%.o : kernel/syscall/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o kernel/syscall/$*.c

out/%.o : kernel/debug/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o kernel/debug/$*.c

out/%.o : kernel/security/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o kernel/security/$*.c

out/%.o : kernel/compat/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o kernel/compat/$*.c

out/hash.o : lib/hash.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/hash.o lib/hash.c

out/%.o : lib/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o lib/$*.c

out/%.o : kernel/platform/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o kernel/platform/$*.c

out/%.o : drivers/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o drivers/$*.c

out/%.o : drivers/input/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o drivers/input/$*.c

out/pci.o : drivers/pci/pci.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/pci.o drivers/pci/pci.c

out/dma.o : drivers/dma/dma.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/dma.o drivers/dma/dma.c

out/i2c.o : drivers/i2c/i2c.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/i2c.o drivers/i2c/i2c.c

out/i3c.o : drivers/i3c/i3c.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/i3c.o drivers/i3c/i3c.c

out/spi.o : drivers/spi/spi.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/spi.o drivers/spi/spi.c

out/tpm.o : drivers/tpm/tpm.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/tpm.o drivers/tpm/tpm.c

out/mcb.o : drivers/mcb/mcb.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/mcb.o drivers/mcb/mcb.c

out/md.o : drivers/md/md.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/md.o drivers/md/md.c

out/ide.o : drivers/storage/ide.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/ide.o drivers/storage/ide.c

out/ahci.o : drivers/storage/ahci.c include/ahci.h include/dma.h include/mmu.h include/pci.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/ahci.o drivers/storage/ahci.c

out/nvme.o : drivers/storage/nvme.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/nvme.o drivers/storage/nvme.c

out/storage_ext.o : drivers/storage/storage_ext.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/storage_ext.o drivers/storage/storage_ext.c

out/cdrom.o : drivers/storage/cdrom.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/cdrom.o drivers/storage/cdrom.c

out/blockdev.o : drivers/storage/blockdev.c include/blockdev.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/blockdev.o drivers/storage/blockdev.c

out/sdhci.o : drivers/storage/sdhci.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/sdhci.o drivers/storage/sdhci.c

out/lpt.o : drivers/printer/lpt.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/lpt.o drivers/printer/lpt.c

out/xhci.o : drivers/usb/xhci.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/xhci.o drivers/usb/xhci.c

out/usb_ext.o : drivers/usb/usb_ext.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/usb_ext.o drivers/usb/usb_ext.c

out/hid.o : drivers/usb/hid.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/hid.o drivers/usb/hid.c

out/msc.o : drivers/usb/msc.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/msc.o drivers/usb/msc.c

out/bluetooth.o : drivers/bluetooth/bluetooth.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/bluetooth.o drivers/bluetooth/bluetooth.c

out/smbus.o : drivers/smbus/smbus.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/smbus.o drivers/smbus/smbus.c

out/intelbus1.o : drivers/smbus/intelbus1.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/intelbus1.o drivers/smbus/intelbus1.c

out/amdbus1.o : drivers/smbus/amdbus1.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/amdbus1.o drivers/smbus/amdbus1.c

out/net.o : kernel/net/net.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/net.o kernel/net/net.c

out/ip.o : kernel/net/ip.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/ip.o kernel/net/ip.c

out/ipv4.o : kernel/net/ipv4.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/ipv4.o kernel/net/ipv4.c

out/ipv6.o : kernel/net/ipv6.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/ipv6.o kernel/net/ipv6.c

out/dhcp.o : kernel/net/dhcp.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/dhcp.o kernel/net/dhcp.c

out/dns.o : kernel/net/dns.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/dns.o kernel/net/dns.c

out/socket.o : kernel/net/socket.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/socket.o kernel/net/socket.c

out/tcp.o : kernel/net/tcp.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/tcp.o kernel/net/tcp.c

out/e1000.o : drivers/net/e1000.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/e1000.o drivers/net/e1000.c

out/pcnet.o : drivers/net/pcnet.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/pcnet.o drivers/net/pcnet.c

out/rtl8139.o : drivers/net/rtl8139.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/rtl8139.o drivers/net/rtl8139.c

out/virtio_net.o : drivers/virtio/virtio_net.c include/virtio.h include/net.h include/pci.h include/dma.h include/mmu.h
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/virtio_net.o drivers/virtio/virtio_net.c

out/lwip.o : kernel/net/lwip.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/lwip.o kernel/net/lwip.c

out/tls.o : kernel/net/tls.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/tls.o kernel/net/tls.c

out/http.o : kernel/net/http.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/http.o kernel/net/http.c

out/wifi.o : kernel/net/wifi.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/wifi.o kernel/net/wifi.c

out/gpu.o : drivers/gpu/gpu.c include/gpu.h include/gpu_backend.h include/gpu_igpu.h include/gpu_nvidia.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/gpu.o drivers/gpu/gpu.c

out/opengl.o : drivers/gpu/opengl.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/opengl.o drivers/gpu/opengl.c

out/vulkan.o : drivers/gpu/vulkan.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/vulkan.o drivers/gpu/vulkan.c

out/nvidia.o : drivers/gpu/nvidia.c include/gpu_nvidia.h include/gpu_backend.h include/pci.h include/mmu.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/nvidia.o drivers/gpu/nvidia.c

out/igpu.o : drivers/gpu/igpu.c include/gpu_igpu.h include/gpu_backend.h include/pci.h include/kernel.h include/mmu.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/igpu.o drivers/gpu/igpu.c

out/virtio.o : drivers/virtio/virtio.c include/virtio.h include/pci.h include/mmu.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/virtio.o drivers/virtio/virtio.c

out/virtio_blk.o : drivers/virtio/virtio_blk.c include/virtio.h include/pci.h include/dma.h include/mmu.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/virtio_blk.o drivers/virtio/virtio_blk.c

out/audio.o : drivers/audio/audio.c
	$(X86_64_CC) -c -I include -I drivers/audio -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/audio.o drivers/audio/audio.c

out/aac.o : drivers/audio/aac.c
	$(X86_64_CC) -c -I include -I drivers/audio -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/aac.o drivers/audio/aac.c

out/mp3_dec.o : drivers/audio/mp3_dec.c drivers/audio/minimp3.h
	$(X86_64_CC) -c -I include -I drivers/audio -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/mp3_dec.o drivers/audio/mp3_dec.c

out/hda.o : drivers/audio/hda.c
	$(X86_64_CC) -c -I include -I drivers/audio -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/hda.o drivers/audio/hda.c

out/es1371.o : drivers/audio/es1371.c
	$(X86_64_CC) -c -I include -I drivers/audio -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/es1371.o drivers/audio/es1371.c

out/%.pe.o : user/lib/%.c
	$(APP_CC) $(APP_CFLAGS) -o out/$*.pe.o user/lib/$*.c

out/string.pe.o : lib/string.c
	$(APP_CC) $(APP_CFLAGS) -o out/string.pe.o lib/string.c
out/zip.pe.o : lib/zip.c
	$(APP_CC) $(APP_CFLAGS) -o out/zip.pe.o lib/zip.c
out/crc32.pe.o : lib/crc32.c
	$(APP_CC) $(APP_CFLAGS) -o out/crc32.pe.o lib/crc32.c
out/png.pe.o : lib/png.c
	$(APP_CC) $(APP_CFLAGS) -o out/png.pe.o lib/png.c

out/app_%.pe.o : user/apps/%.c
	$(APP_CC) $(APP_CFLAGS) -o out/app_$*.pe.o user/apps/$*.c

out/app_hello.pe.o : examples/hello.c user/lib/console_dll.h tools/monios-gcc.py
	$(PYTHON) tools/monios-gcc.py -c -o out/app_hello.pe.o examples/hello.c

out/sysstub.pe.o : drivers/monios/sysstub.c include/driver_api.h
	$(APP_CC) $(APP_CFLAGS) -o out/sysstub.pe.o drivers/monios/sysstub.c

out/driver_%.pe.o : drivers/monios/%.c
	$(APP_CC) $(APP_CFLAGS) -o out/driver_$*.pe.o drivers/monios/$*.c

assets/icons/app.ico assets/icons/uac.ico assets/icons/driver.ico : tools/make_icons.py
	$(PYTHON) tools/make_icons.py

out/app_resource.pe.o : assets/resources/app.rc assets/manifests/as-invoker.manifest assets/icons/app.ico | $(OUT_DIR)
	$(APP_WINDRES) --target=pe-x86-64 -O coff -i assets/resources/app.rc -o out/app_resource.pe.o

out/uac_resource.pe.o : assets/resources/uac.rc assets/manifests/require-admin.manifest assets/icons/uac.ico | $(OUT_DIR)
	$(APP_WINDRES) --target=pe-x86-64 -O coff -i assets/resources/uac.rc -o out/uac_resource.pe.o

out/driver_resource.pe.o : assets/resources/driver.rc assets/icons/driver.ico | $(OUT_DIR)
	$(APP_WINDRES) --target=pe-x86-64 -O coff -i assets/resources/driver.rc -o out/driver_resource.pe.o

$(APP_USER_OBJS): $(APP_PUBLIC_HEADERS)
$(DRIVER_USER_OBJS): $(APP_PUBLIC_HEADERS)
$(APP_DLL_OBJS): $(APP_PUBLIC_HEADERS) $(APP_DLL_HEADERS)
$(APP_RESOURCE_OBJS): $(APP_ICON_ASSETS)
$(APP_EXE_TARGETS): $(APP_IMPORT_LIBS)

out/exec.o : include/exec.h include/path.h include/file.h include/pcb.h include/session.h
out/syscall.o : include/syscall.h include/exec.h include/app_memory.h include/registry.h include/system_status.h include/installer.h
out/app_memory.o : include/app_memory.h include/exec.h
out/driver_manager.o : include/driver_manager.h include/driver_api.h include/driver_status.h include/exec.h include/file.h include/registry.h include/bsod.h include/memory.h include/hash.h include/gpu.h include/virtio.h include/authenticode.h
out/shell.o : include/exec.h include/gpu.h include/osui.h include/registry.h include/shell.h include/syscall.h
out/graphics.o : kernel/ui/graphics.c include/exec.h include/font.h include/graphics.h include/registry.h include/shell.h include/ui.h
	$(X86_64_CC) -c -I include -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/graphics.o kernel/ui/graphics.c
out/authenticode.o : kernel/net/authenticode.c include/authenticode.h include/hash.h include/memory.h include/rsa.h include/trust_store.h include/x509.h

out/gdb_stub.o : kernel/debug/gdb_stub.c include/gdb_stub.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/gdb_stub.o kernel/debug/gdb_stub.c

out/crash_dump.o : kernel/debug/crash_dump.c include/crash_dump.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/crash_dump.o kernel/debug/crash_dump.c

out/ftrace.o : kernel/debug/ftrace.c include/ftrace.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/ftrace.o kernel/debug/ftrace.c

out/kernel.elf : $(KERNEL_OBJS)
	$(X86_64_LD) -nostdlib -T kernel/arch/kernel.ld -o out/kernel.elf $(KERNEL_OBJS)

out/kernel.unsigned.exe : $(KERNEL_OBJS) kernel/arch/kernel.ld
	$(X86_64_MINGW_LD) -m i386pep --orphan-handling=discard -nostdlib -T kernel/arch/kernel.ld --entry _start --subsystem native --image-base 0x2000000 --section-start=.text=0x2001000 --file-alignment 0x200 --section-alignment 0x1000 --enable-reloc-section -o out/kernel.unsigned.exe $(KERNEL_OBJS)

out/kernel.exe : out/kernel.unsigned.exe tools/sign_driver_sys.py
	$(PYTHON) tools/sign_driver_sys.py out/kernel.unsigned.exe out/kernel.exe --qm-dir D:/qm --subject "MoniOS Driver Signing"

out/demo.exe : user/apps/app.ld $(APP_DEMO_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/demo.exe $(APP_DEMO_OBJS) $(APP_LD_LIBS)

out/explorar.exe : user/apps/app.ld $(APP_EXPLORAR_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/explorar.exe $(APP_EXPLORAR_OBJS) $(APP_LD_LIBS)

out/monilog.exe : user/apps/app.ld $(APP_MONILOGON_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/monilog.exe $(APP_MONILOGON_OBJS) $(APP_LD_LIBS)

out/player.exe : user/apps/app.ld $(APP_PLAYER_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/player.exe $(APP_PLAYER_OBJS) $(APP_LD_LIBS)

out/httpd.exe : user/apps/app.ld $(APP_HTTPD_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/httpd.exe $(APP_HTTPD_OBJS) $(APP_LD_LIBS)
out/hello_cgi.exe : user/apps/app.ld $(APP_HELLO_CGI_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/hello_cgi.exe $(APP_HELLO_CGI_OBJS) $(APP_LD_LIBS)

out/browser.exe : user/apps/app.ld $(APP_BROWSER_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/browser.exe $(APP_BROWSER_OBJS) $(APP_LD_LIBS)

out/notepad.exe : user/apps/app.ld $(APP_NOTEPAD_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/notepad.exe $(APP_NOTEPAD_OBJS) $(APP_LD_LIBS)

out/taskmgr.exe : user/apps/app.ld $(APP_TASKMGR_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/taskmgr.exe $(APP_TASKMGR_OBJS) $(APP_LD_LIBS)

out/square.exe : user/apps/app.ld $(APP_SQUARE_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/square.exe $(APP_SQUARE_OBJS) $(APP_LD_LIBS)

out/cube3d.exe : user/apps/app.ld $(APP_CUBE3D_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/cube3d.exe $(APP_CUBE3D_OBJS) $(APP_LD_LIBS)

out/nettest.exe : user/apps/app.ld $(APP_NETTEST_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/nettest.exe $(APP_NETTEST_OBJS) $(APP_LD_LIBS)

out/audiotest.exe : user/apps/app.ld $(APP_AUDIOTEST_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/audiotest.exe $(APP_AUDIOTEST_OBJS) $(APP_LD_LIBS)

out/gfxtest.exe : user/apps/app.ld $(APP_GFXTEST_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/gfxtest.exe $(APP_GFXTEST_OBJS) $(APP_LD_LIBS)

out/smoke_test.exe : user/apps/app.ld $(APP_SMOKETEST_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/smoke_test.exe $(APP_SMOKETEST_OBJS) $(APP_LD_LIBS)

out/imageview.exe : user/apps/app.ld $(APP_IMAGEVIEW_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/imageview.exe $(APP_IMAGEVIEW_OBJS) $(APP_LD_LIBS)

out/settings.exe : user/apps/app.ld $(APP_SETTINGS_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/settings.exe $(APP_SETTINGS_OBJS) $(APP_LD_LIBS)
out/calculator.exe : user/apps/app.ld $(APP_CALCULATOR_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/calculator.exe $(APP_CALCULATOR_OBJS) $(APP_LD_LIBS)
out/pdfview.exe : user/apps/app.ld $(APP_PDFVIEW_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/pdfview.exe $(APP_PDFVIEW_OBJS) $(APP_LD_LIBS)
out/firewall.exe : user/apps/app.ld $(APP_FIREWALL_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/firewall.exe $(APP_FIREWALL_OBJS) $(APP_LD_LIBS)

out/termux.exe : user/apps/app.ld $(APP_TERMUX_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/termux.exe $(APP_TERMUX_OBJS) $(APP_LD_LIBS)

out/defrag.exe : user/apps/app.ld $(APP_DEFRAG_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/defrag.exe $(APP_DEFRAG_OBJS) $(APP_LD_LIBS)

out/memtest.exe : user/apps/app.ld $(APP_MEMTEST_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/memtest.exe $(APP_MEMTEST_OBJS) $(APP_LD_LIBS)

out/hwinfo.exe : user/apps/app.ld $(APP_HWINFO_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/hwinfo.exe $(APP_HWINFO_OBJS) $(APP_LD_LIBS)

out/drvmgr.exe : user/apps/app.ld $(APP_DRVMGR_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/drvmgr.exe $(APP_DRVMGR_OBJS) $(APP_LD_LIBS)

out/sysrestore.exe : user/apps/app.ld $(APP_SYSRESTORE_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/sysrestore.exe $(APP_SYSRESTORE_OBJS) $(APP_LD_LIBS)

out/audiorec.exe : user/apps/app.ld $(APP_AUDIOREC_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/audiorec.exe $(APP_AUDIOREC_OBJS) $(APP_LD_LIBS)

out/videoplay.exe : user/apps/app.ld $(APP_VIDEOPLAY_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/videoplay.exe $(APP_VIDEOPLAY_OBJS) $(APP_LD_LIBS)

out/printdoc.exe : user/apps/app.ld $(APP_PRINTDOC_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/printdoc.exe $(APP_PRINTDOC_OBJS) $(APP_LD_LIBS)

out/zip.exe : user/apps/app.ld $(APP_ZIP_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/zip.exe $(APP_ZIP_OBJS) $(APP_LD_LIBS)
out/backup.exe : user/apps/app.ld $(APP_BACKUP_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/backup.exe $(APP_BACKUP_OBJS) $(APP_LD_LIBS)

out/download.exe : user/apps/app.ld $(APP_DOWNLOAD_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/download.exe $(APP_DOWNLOAD_OBJS) $(APP_LD_LIBS)

out/ftp.exe : user/apps/app.ld $(APP_FTP_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/ftp.exe $(APP_FTP_OBJS) $(APP_LD_LIBS)

out/snake.exe : user/apps/app.ld $(APP_SNAKE_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/snake.exe $(APP_SNAKE_OBJS) $(APP_LD_LIBS)

out/tetris.exe : user/apps/app.ld $(APP_TETRIS_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/tetris.exe $(APP_TETRIS_OBJS) $(APP_LD_LIBS)

out/minesweeper.exe : user/apps/app.ld $(APP_MINESWEEPER_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/minesweeper.exe $(APP_MINESWEEPER_OBJS) $(APP_LD_LIBS)

out/solitaire.exe : user/apps/app.ld $(APP_SOLITAIRE_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/solitaire.exe $(APP_SOLITAIRE_OBJS) $(APP_LD_LIBS)

out/mail.exe : user/apps/app.ld $(APP_MAIL_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/mail.exe $(APP_MAIL_OBJS) $(APP_LD_LIBS)

out/chat.exe : user/apps/app.ld $(APP_CHAT_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/chat.exe $(APP_CHAT_OBJS) $(APP_LD_LIBS)

out/screenshot.exe : user/apps/app.ld $(APP_SCREENSHOT_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/screenshot.exe $(APP_SCREENSHOT_OBJS) $(APP_LD_LIBS)

out/record.exe : user/apps/app.ld $(APP_RECORD_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/record.exe $(APP_RECORD_OBJS) $(APP_LD_LIBS)

out/perfmon.exe : user/apps/app.ld $(APP_PERFMON_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/perfmon.exe $(APP_PERFMON_OBJS) $(APP_LD_LIBS)
out/gdbgui.exe : user/apps/app.ld $(APP_GDBGUI_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/gdbgui.exe $(APP_GDBGUI_OBJS) $(APP_LD_LIBS)
out/sysmon.exe : user/apps/app.ld $(APP_SYSMON_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/sysmon.exe $(APP_SYSMON_OBJS) $(APP_LD_LIBS)

out/memmon.exe : user/apps/app.ld $(APP_MEMMON_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/memmon.exe $(APP_MEMMON_OBJS) $(APP_LD_LIBS)

out/rzdrv.unsigned.sys : user/apps/driver.ld $(DRIVER_RZDRV_OBJS)
	$(APP_LD) $(DRIVER_LDFLAGS) -Wl,--subsystem,native -o out/rzdrv.unsigned.sys $(DRIVER_RZDRV_OBJS)

out/rzdrv.sys : out/rzdrv.unsigned.sys tools/sign_driver_sys.py
	$(PYTHON) tools/sign_driver_sys.py out/rzdrv.unsigned.sys out/rzdrv.sys --qm-dir D:/qm --subject "MoniOS Driver Signing"

out/monios_%.unsigned.sys : user/apps/driver.ld out/sysstub.pe.o
	$(APP_LD) $(DRIVER_LDFLAGS) -Wl,--subsystem,native -o out/monios_$*.unsigned.sys out/sysstub.pe.o

out/%.sys : out/monios_%.unsigned.sys tools/sign_driver_sys.py
	$(PYTHON) tools/sign_driver_sys.py out/monios_$*.unsigned.sys out/$*.sys --qm-dir D:/qm --subject "MoniOS Driver Signing"

out/sysinst.exe : user/apps/app.ld $(APP_SYSINST_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/sysinst.exe $(APP_SYSINST_OBJS) $(APP_LD_LIBS)

out/setup.exe : user/apps/app.ld $(APP_SETUP_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/setup.exe $(APP_SETUP_OBJS) $(APP_LD_LIBS)

out/appdev.exe : user/apps/app.ld $(APP_APPDEV_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/appdev.exe $(APP_APPDEV_OBJS) $(APP_LD_LIBS)

out/hello.exe : out/app_hello.pe.o $(APP_RUNTIME_OBJS) out/app_resource.pe.o tools/monios-ld.py $(APP_IMPORT_LIBS)
	$(PYTHON) tools/monios-ld.py --subsystem console -o out/hello.exe out/app_hello.pe.o out/app_resource.pe.o

$(MINGW_SAMPLE_TARGET) : $(MINGW_SAMPLE_FILES) app-runtime
	"$(MAKE)" -C $(MINGW_SAMPLE_DIR)

out/app_mingw_gcc.pe.o : $(MINGW_SAMPLE_DIR)/gcc.c $(MINGW_SAMPLE_DIR)/toolchain_format.h $(MINGW_SAMPLE_DIR)/toolchain_util.h $(APP_PUBLIC_HEADERS)
	$(APP_CC) $(APP_CFLAGS) -mno-stack-arg-probe -o out/app_mingw_gcc.pe.o $(MINGW_SAMPLE_DIR)/gcc.c

out/app_mingw_as.pe.o : $(MINGW_SAMPLE_DIR)/as.c $(MINGW_SAMPLE_DIR)/toolchain_format.h $(MINGW_SAMPLE_DIR)/toolchain_util.h $(APP_PUBLIC_HEADERS)
	$(APP_CC) $(APP_CFLAGS) -o out/app_mingw_as.pe.o $(MINGW_SAMPLE_DIR)/as.c

out/app_mingw_ld.pe.o : $(MINGW_SAMPLE_DIR)/ld.c $(MINGW_SAMPLE_DIR)/toolchain_format.h $(MINGW_SAMPLE_DIR)/toolchain_util.h $(APP_PUBLIC_HEADERS)
	$(APP_CC) $(APP_CFLAGS) -o out/app_mingw_ld.pe.o $(MINGW_SAMPLE_DIR)/ld.c

out/mingw_gcc.exe : user/apps/app.ld out/app_mingw_gcc.pe.o $(APP_RUNTIME_OBJS) $(APP_IMPORT_LIBS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/mingw_gcc.exe $(APP_RUNTIME_OBJS) out/app_mingw_gcc.pe.o $(APP_LD_LIBS)

out/mingw_as.exe : user/apps/app.ld out/app_mingw_as.pe.o $(APP_RUNTIME_OBJS) $(APP_IMPORT_LIBS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/mingw_as.exe $(APP_RUNTIME_OBJS) out/app_mingw_as.pe.o $(APP_LD_LIBS)

out/mingw_ld.exe : user/apps/app.ld out/app_mingw_ld.pe.o $(APP_RUNTIME_OBJS) $(APP_IMPORT_LIBS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,console -o out/mingw_ld.exe $(APP_RUNTIME_OBJS) out/app_mingw_ld.pe.o $(APP_LD_LIBS)

out/monios.dll : $(APP_MONIOS_DLL_OBJS)
	$(APP_LD) $(APP_DLL_MONIOS_LDFLAGS) -Wl,--subsystem,native -Wl,--out-implib,out/libmonios.dll.a -o out/monios.dll $(APP_MONIOS_DLL_OBJS)

out/libmonios.dll.a : out/monios.dll

out/console.dll : $(APP_CONSOLE_DLL_OBJS) out/libmonios.dll.a
	$(APP_LD) $(APP_DLL_CONSOLE_LDFLAGS) -Wl,--subsystem,console -Wl,--out-implib,out/libconsole.dll.a -o out/console.dll $(APP_CONSOLE_DLL_OBJS) -Lout -lmonios

out/libconsole.dll.a : out/console.dll

out/windows.dll : $(APP_WINDOWS_DLL_OBJS) out/libmonios.dll.a
	$(APP_LD) $(APP_DLL_WINDOWS_LDFLAGS) -Wl,--subsystem,windows -Wl,--out-implib,out/libwindows.dll.a -o out/windows.dll $(APP_WINDOWS_DLL_OBJS) -Lout -lmonios

out/libwindows.dll.a : out/windows.dll

out/osui.dll : $(APP_OSUI_DLL_OBJS) out/libwindows.dll.a
	$(APP_LD) $(APP_DLL_WINDOWS_LDFLAGS) -Wl,--subsystem,windows -Wl,--out-implib,out/libosui.dll.a -o out/osui.dll $(APP_OSUI_DLL_OBJS) -Lout -lwindows

out/libosui.dll.a : out/osui.dll

out/monios.efi : out/kernel.exe tools/build_uefi.py
	$(PYTHON) tools/build_uefi.py --output out/monios.efi

out/monios_uefi_installer.iso : out/boot.bin $(ISO_PAYLOAD_TARGETS) tools/build_uefi_iso.py
	$(PYTHON) tools/build_uefi_iso.py --output out/monios_uefi_installer.iso --esp out/monios_uefi_esp.img --kernel out/kernel.exe --setup out/setup.exe --font $(UEFI_FONT_IMAGE) --boot-bin out/boot.bin

$(QEMU_INSTALL_DISK) : | $(OUT_DIR)
	$(QEMU_IMG) create -f raw $(QEMU_INSTALL_DISK) 128M

hd_uefi.img : out/boot.bin out/loader.bin out/kernel.exe out/demo.exe out/explorar.exe out/monilog.exe out/player.exe out/browser.exe out/notepad.exe out/taskmgr.exe out/square.exe out/cube3d.exe out/nettest.exe out/audiotest.exe out/gfxtest.exe out/smoke_test.exe out/imageview.exe out/settings.exe out/zip.exe out/backup.exe out/download.exe out/ftp.exe out/httpd.exe out/hello_cgi.exe out/snake.exe out/tetris.exe out/minesweeper.exe out/solitaire.exe out/mail.exe out/chat.exe out/screenshot.exe out/record.exe out/perfmon.exe out/gdbgui.exe out/sysmon.exe out/memmon.exe out/calculator.exe out/pdfview.exe out/firewall.exe out/termux.exe out/defrag.exe out/memtest.exe out/hwinfo.exe out/drvmgr.exe out/sysrestore.exe out/audiorec.exe out/videoplay.exe out/printdoc.exe $(DRIVER_SYS_TARGETS) out/sysinst.exe out/setup.exe out/appdev.exe out/hello.exe $(MINGW_SAMPLE_TARGET) $(MINGW_SAMPLE_FILES) $(MINGW_TOOL_TARGETS) $(MINGW_TOOL_SOURCES) out/monios.dll out/console.dll out/windows.dll out/osui.dll out/monios.efi $(UI_FONT_IMAGE) $(TRUST_ROOT_BUNDLE) pwd.txt music_vm.wav bgm.m4a bgm.wav version.txt assets/wallpaper.jpg $(BOOT_IMAGE) $(WINDOWS_CURSOR_IMAGE) tools/mkfat32.py
	$(PYTHON) tools/mkfat32.py --image hd_uefi.img --boot out/boot.bin --copy out/loader.bin:/loader.bin --copy out/kernel.exe:/kernel.exe --copy out/loader.bin:/Monios/System/Boot/loader.bin --copy out/kernel.exe:/Monios/System/Boot/kernel.exe --copy out/kernel.exe:/Monios/kernel.exe --copy out/monios.efi:/EFI/BOOT/BOOTX64.EFI --copy out/monios.efi:/BOOTX64.EFI --copy out/kernel.exe:/KERNEL.EXE --copy out/demo.exe:/Monios/Apps/demo.exe --copy out/explorar.exe:/Monios/Apps/explorar.exe --copy out/monilog.exe:/Monios/Apps/monilog.exe --copy out/player.exe:/Monios/Apps/player.exe --copy out/notepad.exe:/Monios/Apps/notepad.exe --copy out/taskmgr.exe:/Monios/Apps/taskmgr.exe --copy out/square.exe:/Monios/Apps/square.exe --copy out/cube3d.exe:/Monios/Apps/cube3d.exe --copy out/nettest.exe:/Monios/Apps/nettest.exe --copy out/audiotest.exe:/Monios/Apps/audiotest.exe --copy out/gfxtest.exe:/Monios/Apps/gfxtest.exe --copy out/smoke_test.exe:/Monios/Apps/smoke_test.exe --copy out/imageview.exe:/Monios/Apps/imageview.exe --copy out/settings.exe:/Monios/Apps/settings.exe --copy out/zip.exe:/Monios/Apps/zip.exe --copy out/backup.exe:/Monios/Apps/backup.exe --copy out/download.exe:/Monios/Apps/download.exe --copy out/ftp.exe:/Monios/Apps/ftp.exe --copy out/httpd.exe:/Monios/Apps/httpd.exe --copy out/hello_cgi.exe:/Monios/www/hello_cgi.exe --copy out/snake.exe:/Monios/Apps/snake.exe --copy out/tetris.exe:/Monios/Apps/tetris.exe --copy out/minesweeper.exe:/Monios/Apps/minesweeper.exe --copy out/solitaire.exe:/Monios/Apps/solitaire.exe --copy out/mail.exe:/Monios/Apps/mail.exe --copy out/chat.exe:/Monios/Apps/chat.exe --copy out/screenshot.exe:/Monios/Apps/screenshot.exe --copy out/record.exe:/Monios/Apps/record.exe --copy out/perfmon.exe:/Monios/Apps/perfmon.exe --copy out/gdbgui.exe:/Monios/Apps/gdbgui.exe --copy out/sysmon.exe:/Monios/Apps/sysmon.exe --copy out/memmon.exe:/Monios/Apps/memmon.exe --copy out/calculator.exe:/Monios/Apps/calculator.exe --copy out/pdfview.exe:/Monios/Apps/pdfview.exe --copy out/firewall.exe:/Monios/Apps/firewall.exe --copy out/sysinst.exe:/Monios/Apps/sysinst.exe --copy out/setup.exe:/Monios/Apps/setup.exe --copy out/appdev.exe:/Monios/Apps/appdev.exe --copy out/hello.exe:/Monios/Apps/hello.exe --copy out/monios.dll:/Monios/System/Lib/monios.dll --copy out/console.dll:/Monios/System/Lib/console.dll --copy out/windows.dll:/Monios/System/Lib/windows.dll --copy out/osui.dll:/Monios/System/Lib/osui.dll --copy $(TRUST_ROOT_BUNDLE):/Monios/System/Security/trust.bin --copy music_vm.wav:/Monios/Users/root/Desktop/music.wav --copy bgm.m4a:/Monios/Users/root/Desktop/bgm.m4a --copy bgm.wav:/Monios/Users/root/Desktop/bgm.wav $(DRIVER_IMAGE_COPIES) $(MINGW_IMAGE_COPIES) --copy $(UI_FONT_IMAGE):/Monios/System/Fonts/msyh.ttc --copy assets/wallpaper.jpg:/Monios/System/Media/wallpaper.jpg --copy $(BOOT_IMAGE):/Monios/System/Media/boot.bmp --copy $(WINDOWS_CURSOR_IMAGE):/Monios/System/Cursors/arrow.cur --copy pwd.txt:/Monios/System/Config/pwd.txt --copy version.txt:/Monios/System/version.txt --copy out/termux.exe:/Monios/Apps/termux.exe --copy out/defrag.exe:/Monios/Apps/defrag.exe --copy out/memtest.exe:/Monios/Apps/memtest.exe --copy out/hwinfo.exe:/Monios/Apps/hwinfo.exe --copy out/drvmgr.exe:/Monios/Apps/drvmgr.exe --copy out/sysrestore.exe:/Monios/Apps/sysrestore.exe --copy out/audiorec.exe:/Monios/Apps/audiorec.exe --copy out/videoplay.exe:/Monios/Apps/videoplay.exe --copy out/printdoc.exe:/Monios/Apps/printdoc.exe

bgm.wav : bgm.m4a tools/aac_to_wav.py
	$(PYTHON) tools/aac_to_wav.py bgm.m4a bgm.wav --placeholder-ok

music_vm.wav : music.wav tools/downsample_wav.py
	$(PYTHON) tools/downsample_wav.py music.wav music_vm.wav --rate 44100

hd.img : out/boot.bin out/loader.bin out/kernel.exe out/demo.exe out/explorar.exe out/monilog.exe out/player.exe out/browser.exe out/notepad.exe out/taskmgr.exe out/square.exe out/cube3d.exe out/nettest.exe out/audiotest.exe out/gfxtest.exe out/smoke_test.exe out/imageview.exe out/settings.exe out/zip.exe out/backup.exe out/download.exe out/ftp.exe out/httpd.exe out/hello_cgi.exe out/snake.exe out/tetris.exe out/minesweeper.exe out/solitaire.exe out/mail.exe out/chat.exe out/screenshot.exe out/record.exe out/perfmon.exe out/gdbgui.exe out/sysmon.exe out/memmon.exe out/calculator.exe out/pdfview.exe out/firewall.exe out/termux.exe out/defrag.exe out/memtest.exe out/hwinfo.exe out/drvmgr.exe out/sysrestore.exe out/audiorec.exe out/videoplay.exe out/printdoc.exe $(DRIVER_SYS_TARGETS) out/sysinst.exe out/setup.exe out/appdev.exe out/hello.exe $(MINGW_SAMPLE_TARGET) $(MINGW_SAMPLE_FILES) $(MINGW_TOOL_TARGETS) $(MINGW_TOOL_SOURCES) out/monios.dll out/console.dll out/windows.dll out/osui.dll $(UI_FONT_IMAGE) $(TRUST_ROOT_BUNDLE) pwd.txt music_vm.wav bgm.m4a bgm.wav version.txt assets/wallpaper.jpg $(BOOT_IMAGE) $(WINDOWS_CURSOR_IMAGE) tools/mkfat32.py
	$(PYTHON) tools/mkfat32.py --image hd.img --boot out/boot.bin --copy out/loader.bin:/loader.bin --copy out/kernel.exe:/kernel.exe --copy out/loader.bin:/Monios/System/Boot/loader.bin --copy out/kernel.exe:/Monios/System/Boot/kernel.exe --copy out/kernel.exe:/Monios/kernel.exe --copy out/demo.exe:/Monios/Apps/demo.exe --copy out/explorar.exe:/Monios/Apps/explorar.exe --copy out/monilog.exe:/Monios/Apps/monilog.exe --copy out/player.exe:/Monios/Apps/player.exe --copy out/notepad.exe:/Monios/Apps/notepad.exe --copy out/taskmgr.exe:/Monios/Apps/taskmgr.exe --copy out/square.exe:/Monios/Apps/square.exe --copy out/cube3d.exe:/Monios/Apps/cube3d.exe --copy out/nettest.exe:/Monios/Apps/nettest.exe --copy out/audiotest.exe:/Monios/Apps/audiotest.exe --copy out/gfxtest.exe:/Monios/Apps/gfxtest.exe --copy out/smoke_test.exe:/Monios/Apps/smoke_test.exe --copy out/imageview.exe:/Monios/Apps/imageview.exe --copy out/settings.exe:/Monios/Apps/settings.exe --copy out/zip.exe:/Monios/Apps/zip.exe --copy out/backup.exe:/Monios/Apps/backup.exe --copy out/download.exe:/Monios/Apps/download.exe --copy out/ftp.exe:/Monios/Apps/ftp.exe --copy out/httpd.exe:/Monios/Apps/httpd.exe --copy out/hello_cgi.exe:/Monios/www/hello_cgi.exe --copy out/snake.exe:/Monios/Apps/snake.exe --copy out/tetris.exe:/Monios/Apps/tetris.exe --copy out/minesweeper.exe:/Monios/Apps/minesweeper.exe --copy out/solitaire.exe:/Monios/Apps/solitaire.exe --copy out/mail.exe:/Monios/Apps/mail.exe --copy out/chat.exe:/Monios/Apps/chat.exe --copy out/screenshot.exe:/Monios/Apps/screenshot.exe --copy out/record.exe:/Monios/Apps/record.exe --copy out/perfmon.exe:/Monios/Apps/perfmon.exe --copy out/gdbgui.exe:/Monios/Apps/gdbgui.exe --copy out/sysmon.exe:/Monios/Apps/sysmon.exe --copy out/memmon.exe:/Monios/Apps/memmon.exe --copy out/calculator.exe:/Monios/Apps/calculator.exe --copy out/pdfview.exe:/Monios/Apps/pdfview.exe --copy out/firewall.exe:/Monios/Apps/firewall.exe --copy out/sysinst.exe:/Monios/Apps/sysinst.exe --copy out/setup.exe:/Monios/Apps/setup.exe --copy out/appdev.exe:/Monios/Apps/appdev.exe --copy out/hello.exe:/Monios/Apps/hello.exe --copy out/monios.dll:/Monios/System/Lib/monios.dll --copy out/console.dll:/Monios/System/Lib/console.dll --copy out/windows.dll:/Monios/System/Lib/windows.dll --copy out/osui.dll:/Monios/System/Lib/osui.dll --copy $(TRUST_ROOT_BUNDLE):/Monios/System/Security/trust.bin --copy music_vm.wav:/Monios/Users/root/Desktop/music.wav --copy bgm.m4a:/Monios/Users/root/Desktop/bgm.m4a --copy bgm.wav:/Monios/Users/root/Desktop/bgm.wav $(DRIVER_IMAGE_COPIES) $(MINGW_IMAGE_COPIES) --copy $(UI_FONT_IMAGE):/Monios/System/Fonts/msyh.ttc --copy assets/wallpaper.jpg:/Monios/System/Media/wallpaper.jpg --copy $(BOOT_IMAGE):/Monios/System/Media/boot.bmp --copy $(WINDOWS_CURSOR_IMAGE):/Monios/System/Cursors/arrow.cur --copy pwd.txt:/Monios/System/Config/pwd.txt --copy version.txt:/Monios/System/version.txt --copy out/termux.exe:/Monios/Apps/termux.exe --copy out/defrag.exe:/Monios/Apps/defrag.exe --copy out/memtest.exe:/Monios/Apps/memtest.exe --copy out/hwinfo.exe:/Monios/Apps/hwinfo.exe --copy out/drvmgr.exe:/Monios/Apps/drvmgr.exe --copy out/sysrestore.exe:/Monios/Apps/sysrestore.exe --copy out/audiorec.exe:/Monios/Apps/audiorec.exe --copy out/videoplay.exe:/Monios/Apps/videoplay.exe --copy out/printdoc.exe:/Monios/Apps/printdoc.exe

hd.vmdk : hd.img
	rm -f hd.vmdk hd-flat.vmdk
	$(QEMU_IMG) convert -f raw -O vmdk -o subformat=monolithicFlat,adapter_type=ide hd.img hd.vmdk

boot : run

boot_bios : run

boot_install : run_uefi_iso

run_bios : run

run_install : run_uefi_iso

run : hd.img
	$(QEMU) $(QEMU_DISPLAY) $(QEMU_AUDIO) $(QEMU_NET) -drive file=hd.img,format=raw

run_debug : hd.img
	$(QEMU) -monitor none -serial stdio -serial tcp::1234,server,nowait $(QEMU_AUDIO) $(QEMU_NET) -device isa-debug-exit,iobase=0x501,iosize=0x02 -drive file=hd.img,format=raw -no-reboot

run_uefi : hd_uefi.img check_uefi
	$(QEMU) $(QEMU_DISPLAY) -machine pc -m 512M $(QEMU_UEFI_PFLASH) $(QEMU_AUDIO_PCI) -drive file=hd_uefi.img,format=raw $(QEMU_NET) -no-reboot

run_uefi_debug : hd_uefi.img check_uefi
	$(QEMU) -machine pc -m 512M -monitor none -serial stdio -serial tcp::1234,server,nowait $(QEMU_UEFI_PFLASH) $(QEMU_AUDIO_PCI) -drive file=hd_uefi.img,format=raw $(QEMU_NET) -no-reboot

run_uefi_gdb : hd_uefi.img check_uefi
	@echo ## MoniOS GDB debug: kernel console on COM1 (stdio), GDB stub on COM2 (TCP :1234)
	@echo ## In another terminal: x86_64-elf-gdb out/kernel.elf -ex target remote tcp:localhost:1234
	$(QEMU) -machine pc -m 512M -monitor none -serial stdio -serial tcp::1234,server,nowait $(QEMU_UEFI_PFLASH) $(QEMU_AUDIO_PCI) -drive file=hd_uefi.img,format=raw $(QEMU_NET) -no-reboot

run_uefi_iso : out/monios_uefi_installer.iso $(QEMU_INSTALL_DISK) check_uefi
	$(QEMU) $(QEMU_DISPLAY) -machine pc $(QEMU_UEFI_PFLASH) $(QEMU_ISO_IDE_BOOT) $(QEMU_NET) -no-reboot

run_uefi_iso_debug : out/monios_uefi_installer.iso $(QEMU_INSTALL_DISK) check_uefi
	$(QEMU) -machine pc -monitor none -serial stdio $(QEMU_UEFI_PFLASH) $(QEMU_ISO_IDE_BOOT) $(QEMU_NET) -no-reboot

run_uefi_q35 : out/monios_uefi_installer.iso $(QEMU_INSTALL_DISK) check_uefi
	$(QEMU) $(QEMU_DISPLAY) -machine q35 $(QEMU_UEFI_PFLASH) $(QEMU_ISO_AHCI_BOOT) $(QEMU_NET) -no-reboot

run_uefi_q35_debug : out/monios_uefi_installer.iso $(QEMU_INSTALL_DISK) check_uefi
	$(QEMU) -machine q35 -monitor none -serial stdio $(QEMU_UEFI_PFLASH) $(QEMU_ISO_AHCI_BOOT) $(QEMU_NET) -no-reboot

check_uefi :
ifeq ($(HOST_OS),windows)
	@echo Using UEFI firmware: $(QEMU_EFI_CODE)
else
	@if [ -z "$(QEMU_EFI_CODE)" ]; then echo "UEFI firmware not found; install ovmf or set QEMU_EFI_CODE."; exit 1; fi
	@if [ ! -f "$(QEMU_EFI_CODE)" ]; then echo "UEFI firmware not found: $(QEMU_EFI_CODE)"; exit 1; fi
endif

uefi : out/monios.efi

uefi_iso : out/monios_uefi_installer.iso

app-runtime : $(APP_RUNTIME_OBJS) $(APP_IMPORT_LIBS)

drivers : $(DRIVER_SYS_TARGETS)

hello : out/hello.exe

mingw-sample : $(MINGW_SAMPLE_TARGET)

mingw-tools : $(MINGW_TOOL_TARGETS)

ifeq ($(HOST_OS),windows)
run_vmware : hd.vmdk
	"D:\Program Files (x86)\VMware\VMware Workstation\vmrun.exe" start "$(VMWARE_VMX)" gui
else
run_vmware : hd.vmdk
	@echo "run_vmware is only available on Windows; use 'make run' with QEMU on Linux."
endif

clean :
	rm -rf out

default : hd.img
$(BOOT_IMAGE) : tools/make_boot_image.py
	$(PYTHON) tools/make_boot_image.py

$(UI_FONT_IMAGE) : tools/stage_font.py
	$(PYTHON) tools/stage_font.py "$(UI_FONT_SOURCE)" "$(UI_FONT_IMAGE)"

$(WINDOWS_CURSOR_IMAGE) : FORCE tools/stage_cursor.py
	$(PYTHON) tools/stage_cursor.py "$(WINDOWS_CURSOR_SOURCE)" "$(WINDOWS_CURSOR_IMAGE)"

$(TRUST_ROOT_BUNDLE) : out/kernel.exe tools/stage_trust_roots.py $(TRUST_ROOT_FILES)
	$(PYTHON) tools/stage_trust_roots.py assets/cent "$(TRUST_ROOT_BUNDLE)"

$(UEFI_FONT_IMAGE) : FORCE tools/stage_font.py
	$(PYTHON) tools/stage_font.py "$(UEFI_FONT_SOURCE)" "$(UEFI_FONT_IMAGE)"

FORCE :
