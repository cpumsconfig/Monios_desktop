X86_64_CC = x86_64-elf-gcc
X86_64_LD = x86_64-elf-ld
X86_64_OBJCOPY = x86_64-elf-objcopy
VMWARE_VMX ?= D:\Users\xiaot\Documents\Virtual Machines\monios_x64\monios_x64.vmx

APP_RUNTIME_OBJS = out/app_runtime.o out/appsys.o out/stdio.o out/stdlib.o out/unistd.o out/string.o
APP_DEMO_OBJS = $(APP_RUNTIME_OBJS) out/app_demo.o
APP_EXPLORAR_OBJS = $(APP_RUNTIME_OBJS) out/app_explorar.o
APP_MONILOGON_OBJS = $(APP_RUNTIME_OBJS) out/app_monilogon.o
APP_PLAYER_OBJS = $(APP_RUNTIME_OBJS) out/app_player.o
APP_NOTEPAD_OBJS = $(APP_RUNTIME_OBJS) out/app_notepad.o
APP_TASKMGR_OBJS = $(APP_RUNTIME_OBJS) out/app_taskmgr.o
APP_SQUARE_OBJS = $(APP_RUNTIME_OBJS) out/app_square.o
APP_CUBE3D_OBJS = $(APP_RUNTIME_OBJS) out/app_cube3d.o

KERNEL_BOOT_OBJS = out/kernel_entry.o out/kernel.o out/mmu.o out/common.o out/string.o out/console.o out/interrupt.o out/input.o
KERNEL_MEM_OBJS = out/memory.o out/bitmap.o out/pool.o out/heap.o out/frame.o out/buddy.o out/vma.o out/lazyalloc.o out/vmext.o
KERNEL_RUNTIME_OBJS = out/eevdf.o out/muqss.o out/scheduler.o out/schedopt.o out/pcb.o out/signal.o out/futex.o out/ipc.o out/prsys.o out/task.o out/bsod.o
KERNEL_FS_OBJS = out/file.o out/fs_cache.o out/fat16.o out/fat32.o out/iso9660.o out/ntfs.o out/extfs.o
KERNEL_UI_OBJS = out/graphics.o out/gui.o out/gpu.o out/syscall.o out/shell.o out/hash.o out/base64.o out/path.o out/exec.o out/session.o out/browser.o
KERNEL_INPUT_OBJS = out/keyboard.o out/mouse.o out/usb.o out/xhci.o out/usb_ext.o out/hid.o out/bluetooth.o
KERNEL_PLATFORM_OBJS = out/bios.o out/gop.o out/rtc.o out/iic.o out/cpu.o out/pci.o out/dma.o out/ide.o out/ahci.o out/nvme.o out/storage_ext.o out/audio.o out/aac.o out/hda.o out/es1371.o out/acpi.o out/power.o out/cmos.o out/driver_manager.o out/device.o out/terminal.o out/smp.o
KERNEL_NET_OBJS = out/net.o out/lwip.o out/tls.o out/http.o out/wifi.o out/ip.o out/ipv4.o out/ipv6.o out/dns.o out/socket.o out/tcp.o out/e1000.o out/pcnet.o
KERNEL_DEBUG_OBJS = out/registry.o out/smbus.o out/intelbus1.o out/amdbus1.o out/gdb_stub.o out/crash_dump.o out/ftrace.o
KERNEL_OBJS = $(KERNEL_BOOT_OBJS) $(KERNEL_MEM_OBJS) $(KERNEL_RUNTIME_OBJS) $(KERNEL_FS_OBJS) $(KERNEL_UI_OBJS) $(KERNEL_INPUT_OBJS) $(KERNEL_PLATFORM_OBJS) $(KERNEL_NET_OBJS) $(KERNEL_DEBUG_OBJS)

kernel/font_data.inc : tools/gen_font.py
	python tools/gen_font.py

out/%.bin : boot/%.asm boot/include/fat32hdr.inc boot/include/load.inc boot/include/pm.inc
	nasm -I boot/include -o out/$*.bin boot/$*.asm

out/kernel_entry.o : kernel/kernel_entry.asm
	nasm -f elf64 -o out/kernel_entry.o kernel/kernel_entry.asm

out/graphics.o : kernel/graphics.c kernel/font_data.inc
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/graphics.o kernel/graphics.c

out/%.o : kernel/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o kernel/$*.c

out/%.o : fs/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o fs/$*.c

out/%.o : drivers/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o drivers/$*.c

out/pci.o : drivers/pci/pci.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/pci.o drivers/pci/pci.c

out/dma.o : drivers/dma/dma.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/dma.o drivers/dma/dma.c

out/ide.o : drivers/storage/ide.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/ide.o drivers/storage/ide.c

out/ahci.o : drivers/storage/ahci.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/ahci.o drivers/storage/ahci.c

out/nvme.o : drivers/storage/nvme.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/nvme.o drivers/storage/nvme.c

out/storage_ext.o : drivers/storage/storage_ext.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/storage_ext.o drivers/storage/storage_ext.c

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

out/net.o : drivers/net/net.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/net.o drivers/net/net.c

out/ip.o : drivers/net/ip.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/ip.o drivers/net/ip.c

out/ipv4.o : drivers/net/ipv4.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/ipv4.o drivers/net/ipv4.c

out/ipv6.o : drivers/net/ipv6.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/ipv6.o drivers/net/ipv6.c

out/dns.o : drivers/net/dns.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/dns.o drivers/net/dns.c

out/socket.o : drivers/net/socket.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/socket.o drivers/net/socket.c

out/tcp.o : drivers/net/tcp.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/tcp.o drivers/net/tcp.c

out/e1000.o : drivers/net/e1000.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/e1000.o drivers/net/e1000.c

out/pcnet.o : drivers/net/pcnet.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/pcnet.o drivers/net/pcnet.c

out/lwip.o : drivers/net/lwip.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/lwip.o drivers/net/lwip.c

out/tls.o : drivers/net/tls.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/tls.o drivers/net/tls.c

out/http.o : drivers/net/http.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/http.o drivers/net/http.c

out/wifi.o : drivers/net/wifi.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/wifi.o drivers/net/wifi.c

out/gpu.o : drivers/gpu/gpu.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/gpu.o drivers/gpu/gpu.c

out/audio.o : drivers/music/audio.c
	$(X86_64_CC) -c -I include -I drivers/music -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/audio.o drivers/music/audio.c

out/aac.o : drivers/music/aac.c
	$(X86_64_CC) -c -I include -I drivers/music -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/aac.o drivers/music/aac.c

out/hda.o : drivers/music/hda.c
	$(X86_64_CC) -c -I include -I drivers/music -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/hda.o drivers/music/hda.c

out/es1371.o : drivers/music/es1371.c
	$(X86_64_CC) -c -I include -I drivers/music -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/es1371.o drivers/music/es1371.c

out/%.o : lib/%.c
	$(X86_64_CC) -c -I include -I lib -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/$*.o lib/$*.c

out/app_%.o : apps/%.c
	$(X86_64_CC) -c -I include -I lib -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/app_$*.o apps/$*.c

out/gdb_stub.o : kernel/gdb_stub.c include/gdb_stub.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/gdb_stub.o kernel/gdb_stub.c

out/crash_dump.o : kernel/crash_dump.c include/crash_dump.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/crash_dump.o kernel/crash_dump.c

out/ftrace.o : kernel/ftrace.c include/ftrace.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -m64 -o out/ftrace.o kernel/ftrace.c

out/kernel.elf : $(KERNEL_OBJS)
	$(X86_64_LD) -nostdlib -T kernel/kernel.ld -o out/kernel.elf $(KERNEL_OBJS)

out/kernel.bin : out/kernel.elf
	$(X86_64_OBJCOPY) -O binary out/kernel.elf out/kernel.bin

out/demo.elf : apps/app.ld $(APP_DEMO_OBJS)
	$(X86_64_LD) -nostdlib -z max-page-size=0x1000 -T apps/app.ld -o out/demo.elf $(APP_DEMO_OBJS)

out/explorar.exe : apps/app.ld $(APP_EXPLORAR_OBJS)
	$(X86_64_LD) -nostdlib -z max-page-size=0x1000 -T apps/app.ld -o out/explorar.exe $(APP_EXPLORAR_OBJS)

out/monilog.exe : apps/app.ld $(APP_MONILOGON_OBJS)
	$(X86_64_LD) -nostdlib -z max-page-size=0x1000 -T apps/app.ld -o out/monilog.exe $(APP_MONILOGON_OBJS)

out/player.elf : apps/app.ld $(APP_PLAYER_OBJS)
	$(X86_64_LD) -nostdlib -z max-page-size=0x1000 -T apps/app.ld -o out/player.elf $(APP_PLAYER_OBJS)

out/notepad.elf : apps/app.ld $(APP_NOTEPAD_OBJS)
	$(X86_64_LD) -nostdlib -z max-page-size=0x1000 -T apps/app.ld -o out/notepad.elf $(APP_NOTEPAD_OBJS)

out/taskmgr.elf : apps/app.ld $(APP_TASKMGR_OBJS)
	$(X86_64_LD) -nostdlib -z max-page-size=0x1000 -T apps/app.ld -o out/taskmgr.elf $(APP_TASKMGR_OBJS)

out/square.elf : apps/app.ld $(APP_SQUARE_OBJS)
	$(X86_64_LD) -nostdlib -z max-page-size=0x1000 -T apps/app.ld -o out/square.elf $(APP_SQUARE_OBJS)

out/cube3d.elf : apps/app.ld $(APP_CUBE3D_OBJS)
	$(X86_64_LD) -nostdlib -z max-page-size=0x1000 -T apps/app.ld -o out/cube3d.elf $(APP_CUBE3D_OBJS)

bgm.wav : bgm.m4a tools/aac_to_wav.py
	python tools/aac_to_wav.py bgm.m4a bgm.wav --placeholder-ok

music_vm.wav : music.wav tools/downsample_wav.py
	python tools/downsample_wav.py music.wav music_vm.wav --rate 8000

hd.img : out/boot.bin out/loader.bin out/kernel.bin out/demo.elf out/explorar.exe out/monilog.exe out/player.elf out/notepad.elf out/taskmgr.elf out/square.elf out/cube3d.elf pwd.txt music_vm.wav bgm.m4a bgm.wav tools/mkfat32.py
	python tools/mkfat32.py --image hd.img --boot out/boot.bin --copy out/loader.bin:/loader.bin --copy out/kernel.bin:/kernel.bin --copy out/demo.elf:/demo.elf --copy out/explorar.exe:/apps/explorar.exe --copy out/monilog.exe:/apps/monilog.exe --copy out/player.elf:/player.elf --copy out/player.elf:/home/root/desktop/player.elf --copy music_vm.wav:/music.wav --copy music_vm.wav:/home/root/desktop/music.wav --copy bgm.m4a:/bgm.m4a --copy bgm.m4a:/home/root/desktop/bgm.m4a --copy bgm.wav:/bgm.wav --copy bgm.wav:/home/root/desktop/bgm.wav --copy out/notepad.elf:/notepad.elf --copy out/notepad.elf:/home/root/desktop/notepad.elf --copy out/taskmgr.elf:/taskmgr.elf --copy out/taskmgr.elf:/home/root/desktop/taskmgr.elf --copy out/square.elf:/square.elf --copy out/square.elf:/home/root/desktop/square.elf --copy out/cube3d.elf:/cube3d.elf --copy out/cube3d.elf:/home/root/desktop/cube3d.elf --copy pwd.txt:/pwd.txt

hd.vmdk : hd.img
	if exist hd.vmdk cmd /c del /f /q hd.vmdk
	if exist hd-flat.vmdk cmd /c del /f /q hd-flat.vmdk
	qemu-img convert -f raw -O vmdk -o subformat=monolithicFlat,adapter_type=ide hd.img hd.vmdk

run : hd.img
	qemu-system-x86_64 -audiodev dsound,id=audio0 -machine pcspk-audiodev=audio0 -device AC97,audiodev=audio0 -netdev user,id=net0 -device e1000,netdev=net0 -drive file=hd.img,format=raw

run_debug : hd.img
	qemu-system-x86_64 -monitor none -serial stdio -audiodev dsound,id=audio0 -machine pcspk-audiodev=audio0 -device AC97,audiodev=audio0 -netdev user,id=net0 -device e1000,netdev=net0 -device isa-debug-exit,iobase=0x501,iosize=0x02 -drive file=hd.img,format=raw -no-reboot

run_vmware : hd.vmdk
	"D:\Program Files (x86)\VMware\VMware Workstation\vmrun.exe" start "$(VMWARE_VMX)" gui

clean :
	if exist out cmd /c del /f /s /q out

default : clean run
