X86_64_CC = x86_64-elf-gcc
X86_64_LD = x86_64-elf-ld
X86_64_OBJCOPY = x86_64-elf-objcopy
VMWARE_VMX ?= D:\Users\xiaot\Documents\Virtual Machines\monios_x64\monios_x64.vmx
UI_FONT_SOURCE ?= C:/Windows/Fonts/msyh.ttc
UI_FONT_IMAGE = out/msyh.ttc
UEFI_FONT_SOURCE ?= $(UI_FONT_SOURCE)
UEFI_FONT_IMAGE = out/uefi_msyh.ttc
QEMU ?= qemu-system-x86_64
QEMU_EFI_CODE ?= C:/Program Files/qemu/share/edk2-x86_64-code.fd
QEMU_AUDIO ?= -audiodev dsound,id=audio0 -machine pcspk-audiodev=audio0 -device AC97,audiodev=audio0
QEMU_NET ?= -netdev user,id=net0 -device e1000,netdev=net0
QEMU_UEFI_PFLASH = -drive "if=pflash,format=raw,readonly=on,file=$(QEMU_EFI_CODE)"

.PHONY : boot boot_bios boot_install run run_bios run_debug run_install run_uefi_iso run_uefi_iso_debug run_uefi_q35 run_uefi_q35_debug uefi uefi_iso run_vmware clean default FORCE

APP_RUNTIME_OBJS = out/app_runtime.o out/appsys.o out/stdio.o out/stdlib.o out/unistd.o out/string.o
APP_DEMO_OBJS = $(APP_RUNTIME_OBJS) out/app_demo.o
APP_EXPLORAR_OBJS = $(APP_RUNTIME_OBJS) out/app_explorar.o
APP_MONILOGON_OBJS = $(APP_RUNTIME_OBJS) out/app_monilogon.o
APP_PLAYER_OBJS = $(APP_RUNTIME_OBJS) out/app_player.o
APP_NOTEPAD_OBJS = $(APP_RUNTIME_OBJS) out/app_notepad.o
APP_TASKMGR_OBJS = $(APP_RUNTIME_OBJS) out/app_taskmgr.o
APP_SQUARE_OBJS = $(APP_RUNTIME_OBJS) out/app_square.o
APP_CUBE3D_OBJS = $(APP_RUNTIME_OBJS) out/app_cube3d.o
APP_RZDRV_OBJS = $(APP_RUNTIME_OBJS) out/app_rzdrv.o
APP_SETUP_OBJS = $(APP_RUNTIME_OBJS) out/app_setup.o

KERNEL_BOOT_OBJS = out/kernel_entry.o out/kernel.o out/mmu.o out/common.o out/string.o out/console.o out/interrupt.o out/input.o
KERNEL_MEM_OBJS = out/memory.o out/bitmap.o out/pool.o out/heap.o out/frame.o out/buddy.o out/vma.o out/lazyalloc.o out/vmext.o out/page.o out/hugetlb.o out/cma.o out/gup.o out/zs.o
KERNEL_RUNTIME_OBJS = out/eevdf.o out/muqss.o out/scheduler.o out/schedopt.o out/pcb.o out/signal.o out/futex.o out/ipc.o out/prsys.o out/task.o out/bsod.o
KERNEL_FS_OBJS = out/file.o out/fs_cache.o out/fat16.o out/fat32.o out/iso9660.o out/ntfs.o out/extfs.o
KERNEL_UI_OBJS = out/font.o out/graphics.o out/gui.o out/gpu.o out/syscall.o out/shell.o out/hash.o out/base64.o out/path.o out/exec.o out/session.o out/browser.o
KERNEL_INPUT_OBJS = out/keyboard.o out/mouse.o out/usb.o out/xhci.o out/usb_ext.o out/hid.o out/bluetooth.o
KERNEL_PLATFORM_OBJS = out/bios.o out/gop.o out/rtc.o out/iic.o out/cpu.o out/pci.o out/dma.o out/ide.o out/ahci.o out/nvme.o out/cdrom.o out/storage_ext.o out/audio.o out/aac.o out/hda.o out/es1371.o out/acpi.o out/power.o out/cmos.o out/driver_manager.o out/device.o out/terminal.o out/smp.o out/opp.o out/od.o out/i2c.o out/i3c.o out/spi.o out/tpm.o out/mcb.o out/md.o
KERNEL_NET_OBJS = out/net.o out/lwip.o out/tls.o out/http.o out/wifi.o out/ip.o out/ipv4.o out/ipv6.o out/dns.o out/socket.o out/tcp.o out/e1000.o out/pcnet.o out/aes.o out/rsa.o out/x509.o
KERNEL_DEBUG_OBJS = out/registry.o out/smbus.o out/intelbus1.o out/amdbus1.o out/gdb_stub.o out/crash_dump.o out/ftrace.o
KERNEL_OBJS = $(KERNEL_BOOT_OBJS) $(KERNEL_MEM_OBJS) $(KERNEL_RUNTIME_OBJS) $(KERNEL_FS_OBJS) $(KERNEL_UI_OBJS) $(KERNEL_INPUT_OBJS) $(KERNEL_PLATFORM_OBJS) $(KERNEL_NET_OBJS) $(KERNEL_DEBUG_OBJS)

out/%.bin : kernel/arch/boot/%.asm kernel/arch/boot/include/fat32hdr.inc kernel/arch/boot/include/load.inc kernel/arch/boot/include/pm.inc
	nasm -I kernel/arch/boot/include -o out/$*.bin kernel/arch/boot/$*.asm

out/kernel_entry.o : kernel/arch/kernel_entry.asm
	nasm -f elf64 -o out/kernel_entry.o kernel/arch/kernel_entry.asm

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

out/%.o : kernel/ipc/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o kernel/ipc/$*.c

out/%.o : kernel/syscall/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o kernel/syscall/$*.c

out/%.o : kernel/debug/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o kernel/debug/$*.c

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

out/ahci.o : drivers/storage/ahci.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/ahci.o drivers/storage/ahci.c

out/nvme.o : drivers/storage/nvme.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/nvme.o drivers/storage/nvme.c

out/storage_ext.o : drivers/storage/storage_ext.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/storage_ext.o drivers/storage/storage_ext.c

out/cdrom.o : drivers/storage/cdrom.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/cdrom.o drivers/storage/cdrom.c

out/xhci.o : drivers/usb/xhci.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/xhci.o drivers/usb/xhci.c

out/usb_ext.o : drivers/usb/usb_ext.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/usb_ext.o drivers/usb/usb_ext.c

out/hid.o : drivers/usb/hid.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/hid.o drivers/usb/hid.c

out/bluetooth.o : drivers/usb/bluetooth.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/bluetooth.o drivers/usb/bluetooth.c

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

out/lwip.o : kernel/net/lwip.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/lwip.o kernel/net/lwip.c

out/tls.o : kernel/net/tls.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/tls.o kernel/net/tls.c

out/http.o : kernel/net/http.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/http.o kernel/net/http.c

out/wifi.o : kernel/net/wifi.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/wifi.o kernel/net/wifi.c

out/gpu.o : drivers/gpu/gpu.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/gpu.o drivers/gpu/gpu.c

out/audio.o : drivers/audio/audio.c
	$(X86_64_CC) -c -I include -I drivers/audio -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/audio.o drivers/audio/audio.c

out/aac.o : drivers/audio/aac.c
	$(X86_64_CC) -c -I include -I drivers/audio -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/aac.o drivers/audio/aac.c

out/hda.o : drivers/audio/hda.c
	$(X86_64_CC) -c -I include -I drivers/audio -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/hda.o drivers/audio/hda.c

out/es1371.o : drivers/audio/es1371.c
	$(X86_64_CC) -c -I include -I drivers/audio -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/es1371.o drivers/audio/es1371.c

out/%.o : user/lib/%.c
	$(X86_64_CC) -c -I include -I user/lib -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o user/lib/$*.c

out/app_%.o : user/apps/%.c
	$(X86_64_CC) -c -I include -I user/lib -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/app_$*.o user/apps/$*.c

out/gdb_stub.o : kernel/debug/gdb_stub.c include/gdb_stub.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/gdb_stub.o kernel/debug/gdb_stub.c

out/crash_dump.o : kernel/debug/crash_dump.c include/crash_dump.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/crash_dump.o kernel/debug/crash_dump.c

out/ftrace.o : kernel/debug/ftrace.c include/ftrace.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/ftrace.o kernel/debug/ftrace.c

out/kernel.elf : $(KERNEL_OBJS)
	$(X86_64_LD) -nostdlib -T kernel/arch/kernel.ld -o out/kernel.elf $(KERNEL_OBJS)

out/kernel.bin : out/kernel.elf
	$(X86_64_OBJCOPY) -O binary out/kernel.elf out/kernel.bin

out/demo.elf : user/apps/app.ld $(APP_DEMO_OBJS)
	$(X86_64_LD) -nostdlib -z max-page-size=0x1000 -T user/apps/app.ld -o out/demo.elf $(APP_DEMO_OBJS)

out/explorar.exe : user/apps/app.ld $(APP_EXPLORAR_OBJS)
	$(X86_64_LD) -nostdlib -z max-page-size=0x1000 -T user/apps/app.ld -o out/explorar.exe $(APP_EXPLORAR_OBJS)

out/monilog.exe : user/apps/app.ld $(APP_MONILOGON_OBJS)
	$(X86_64_LD) -nostdlib -z max-page-size=0x1000 -T user/apps/app.ld -o out/monilog.exe $(APP_MONILOGON_OBJS)

out/player.elf : user/apps/app.ld $(APP_PLAYER_OBJS)
	$(X86_64_LD) -nostdlib -z max-page-size=0x1000 -T user/apps/app.ld -o out/player.elf $(APP_PLAYER_OBJS)

out/notepad.elf : user/apps/app.ld $(APP_NOTEPAD_OBJS)
	$(X86_64_LD) -nostdlib -z max-page-size=0x1000 -T user/apps/app.ld -o out/notepad.elf $(APP_NOTEPAD_OBJS)

out/taskmgr.elf : user/apps/app.ld $(APP_TASKMGR_OBJS)
	$(X86_64_LD) -nostdlib -z max-page-size=0x1000 -T user/apps/app.ld -o out/taskmgr.elf $(APP_TASKMGR_OBJS)

out/square.elf : user/apps/app.ld $(APP_SQUARE_OBJS)
	$(X86_64_LD) -nostdlib -z max-page-size=0x1000 -T user/apps/app.ld -o out/square.elf $(APP_SQUARE_OBJS)

out/cube3d.elf : user/apps/app.ld $(APP_CUBE3D_OBJS)
	$(X86_64_LD) -nostdlib -z max-page-size=0x1000 -T user/apps/app.ld -o out/cube3d.elf $(APP_CUBE3D_OBJS)

out/rzdrv.elf : user/apps/app.ld $(APP_RZDRV_OBJS)
	$(X86_64_LD) -nostdlib -z max-page-size=0x1000 -T user/apps/app.ld -o out/rzdrv.elf $(APP_RZDRV_OBJS)

out/rzdrv.rzs : out/rzdrv.elf tools/make_rzs.py
	python tools/make_rzs.py out/rzdrv.elf out/rzdrv.rzs --flags driver,console,r2

out/setup.elf : user/apps/app.ld $(APP_SETUP_OBJS)
	$(X86_64_LD) -nostdlib -z max-page-size=0x1000 -T user/apps/app.ld -o out/setup.elf $(APP_SETUP_OBJS)

out/monios.efi : out/kernel.bin tools/build_uefi.py
	python tools/build_uefi.py --output out/monios.efi

out/monios_uefi_installer.iso : out/monios.efi out/kernel.bin out/setup.elf $(UEFI_FONT_IMAGE) pwd.txt tools/build_uefi_iso.py
	python tools/build_uefi_iso.py --output out/monios_uefi_installer.iso --esp out/monios_uefi_esp.img --font $(UEFI_FONT_IMAGE) --password pwd.txt

bgm.wav : bgm.m4a tools/aac_to_wav.py
	python tools/aac_to_wav.py bgm.m4a bgm.wav --placeholder-ok

music_vm.wav : music.wav tools/downsample_wav.py
	python tools/downsample_wav.py music.wav music_vm.wav --rate 8000

hd.img : out/boot.bin out/loader.bin out/kernel.bin out/demo.elf out/explorar.exe out/monilog.exe out/player.elf out/notepad.elf out/taskmgr.elf out/square.elf out/cube3d.elf out/rzdrv.rzs out/setup.elf $(UI_FONT_IMAGE) pwd.txt music_vm.wav bgm.m4a bgm.wav version.txt tools/mkfat32.py
	python tools/mkfat32.py --image hd.img --boot out/boot.bin --copy out/loader.bin:/loader.bin --copy out/kernel.bin:/kernel.bin --copy out/explorar.exe:/apps/explorar.exe --copy out/monilog.exe:/apps/monilog.exe --copy out/setup.elf:/apps/setup.elf --copy out/player.elf:/home/root/desktop/player.elf --copy music_vm.wav:/home/root/desktop/music.wav --copy bgm.m4a:/home/root/desktop/bgm.m4a --copy bgm.wav:/home/root/desktop/bgm.wav --copy out/notepad.elf:/home/root/desktop/notepad.elf --copy out/taskmgr.elf:/home/root/desktop/taskmgr.elf --copy out/square.elf:/home/root/desktop/square.elf --copy out/cube3d.elf:/home/root/desktop/cube3d.elf --copy out/setup.elf:/home/root/desktop/setup.elf --copy out/rzdrv.rzs:/drivers/rzdrv.rzs --copy out/rzdrv.rzs:/home/root/desktop/rzdrv.rzs --copy $(UI_FONT_IMAGE):/fonts/msyh.ttc --copy pwd.txt:/pwd.txt --copy version.txt:/version.txt

hd.vmdk : hd.img
	if exist hd.vmdk cmd /c del /f /q hd.vmdk
	if exist hd-flat.vmdk cmd /c del /f /q hd-flat.vmdk
	qemu-img convert -f raw -O vmdk -o subformat=monolithicFlat,adapter_type=ide hd.img hd.vmdk

boot : run

boot_bios : run

boot_install : run_uefi_iso

run_bios : run

run_install : run_uefi_iso

run : hd.img
	$(QEMU) $(QEMU_AUDIO) $(QEMU_NET) -drive file=hd.img,format=raw

run_debug : hd.img
	$(QEMU) -monitor none -serial stdio $(QEMU_AUDIO) $(QEMU_NET) -device isa-debug-exit,iobase=0x501,iosize=0x02 -drive file=hd.img,format=raw -no-reboot

run_uefi_iso : out/monios_uefi_installer.iso
	$(QEMU) -machine pc $(QEMU_UEFI_PFLASH) -drive file=out/monios_uefi_installer.iso,if=ide,media=cdrom,format=raw -boot d $(QEMU_NET) -no-reboot

run_uefi_iso_debug : out/monios_uefi_installer.iso
	$(QEMU) -machine pc -monitor none -serial stdio $(QEMU_UEFI_PFLASH) -drive file=out/monios_uefi_installer.iso,if=ide,media=cdrom,format=raw -boot d $(QEMU_NET) -no-reboot

run_uefi_q35 : out/monios_uefi_installer.iso
	$(QEMU) -machine q35 $(QEMU_UEFI_PFLASH) -cdrom out/monios_uefi_installer.iso -boot d $(QEMU_NET) -no-reboot

run_uefi_q35_debug : out/monios_uefi_installer.iso
	$(QEMU) -machine q35 -monitor none -serial stdio $(QEMU_UEFI_PFLASH) -cdrom out/monios_uefi_installer.iso -boot d $(QEMU_NET) -no-reboot

uefi : out/monios.efi

uefi_iso : out/monios_uefi_installer.iso

run_vmware : hd.vmdk
	"D:\Program Files (x86)\VMware\VMware Workstation\vmrun.exe" start "$(VMWARE_VMX)" gui

clean :
	if exist out cmd /c del /f /s /q out

default : clean run
$(UI_FONT_IMAGE) : tools/stage_font.py
	python tools/stage_font.py "$(UI_FONT_SOURCE)" "$(UI_FONT_IMAGE)"

$(UEFI_FONT_IMAGE) : FORCE tools/stage_font.py
	python tools/stage_font.py "$(UEFI_FONT_SOURCE)" "$(UEFI_FONT_IMAGE)"

FORCE :
