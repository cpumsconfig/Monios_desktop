X86_64_CC = x86_64-elf-gcc
X86_64_LD = x86_64-elf-ld
X86_64_OBJCOPY = x86_64-elf-objcopy

APP_RUNTIME_OBJS = out/app_runtime.o out/appsys.o out/stdio.o out/stdlib.o out/unistd.o out/string.o
APP_DEMO_OBJS = $(APP_RUNTIME_OBJS) out/app_demo.o
APP_EXPLORAR_OBJS = $(APP_RUNTIME_OBJS) out/app_explorar.o
APP_MONILOGON_OBJS = $(APP_RUNTIME_OBJS) out/app_monilogon.o
APP_PLAYER_OBJS = $(APP_RUNTIME_OBJS) out/app_player.o
APP_NOTEPAD_OBJS = $(APP_RUNTIME_OBJS) out/app_notepad.o
APP_TASKMGR_OBJS = $(APP_RUNTIME_OBJS) out/app_taskmgr.o
APP_SQUARE_OBJS = $(APP_RUNTIME_OBJS) out/app_square.o
APP_CUBE3D_OBJS = $(APP_RUNTIME_OBJS) out/app_cube3d.o
KERNEL_OBJS = out/kernel_entry.o out/kernel.o out/mmu.o out/common.o out/string.o out/console.o out/interrupt.o out/input.o out/memory.o out/task.o out/bsod.o out/file.o out/fat16.o out/fat32.o out/graphics.o out/syscall.o out/shell.o out/hash.o out/base64.o out/path.o out/exec.o out/session.o out/keyboard.o out/mouse.o out/usb.o out/cpu.o out/pci.o out/dma.o out/audio.o out/es1371.o out/acpi.o out/cmos.o out/driver_manager.o out/device.o out/terminal.o out/smp.o out/net.o out/ip.o out/tcp.o out/e1000.o out/registry.o out/smbus.o out/gdb_stub.o out/crash_dump.o out/ftrace.o

kernel/font_data.inc : tools/gen_font.py
	python tools/gen_font.py

out/%.bin : boot/%.asm boot/include/fat32hdr.inc
	nasm -I boot/include -o out/$*.bin boot/$*.asm

out/kernel_entry.o : kernel/kernel_entry.asm
	nasm -f elf64 -o out/kernel_entry.o kernel/kernel_entry.asm

out/graphics.o : kernel/graphics.c kernel/font_data.inc
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -m64 -o out/graphics.o kernel/graphics.c

out/%.o : kernel/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -m64 -o out/$*.o kernel/$*.c

out/%.o : fs/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -m64 -o out/$*.o fs/$*.c

out/%.o : drivers/%.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -m64 -o out/$*.o drivers/$*.c

out/pci.o : drivers/pci/pci.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -m64 -o out/pci.o drivers/pci/pci.c

out/dma.o : drivers/dma/dma.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -m64 -o out/dma.o drivers/dma/dma.c

out/smbus.o : drivers/smbus/smbus.c
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -m64 -o out/smbus.o drivers/smbus/smbus.c

out/net.o : drivers/net/net.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -m64 -o out/net.o drivers/net/net.c

out/ip.o : drivers/net/ip.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -m64 -o out/ip.o drivers/net/ip.c

out/tcp.o : drivers/net/tcp.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -m64 -o out/tcp.o drivers/net/tcp.c

out/e1000.o : drivers/net/e1000.c
	$(X86_64_CC) -c -I include -I drivers/net -O0 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -m64 -o out/e1000.o drivers/net/e1000.c

out/audio.o : drivers/music/audio.c
	$(X86_64_CC) -c -I include -I drivers/music -O0 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -m64 -o out/audio.o drivers/music/audio.c

out/es1371.o : drivers/music/es1371.c
	$(X86_64_CC) -c -I include -I drivers/music -O0 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -m64 -o out/es1371.o drivers/music/es1371.c

out/%.o : lib/%.c
	$(X86_64_CC) -c -I include -I lib -O0 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -m64 -o out/$*.o lib/$*.c

out/app_%.o : apps/%.c
	$(X86_64_CC) -c -I include -I lib -O0 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -m64 -o out/app_$*.o apps/$*.c

out/gdb_stub.o : kernel/gdb_stub.c include/gdb_stub.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -m64 -o out/gdb_stub.o kernel/gdb_stub.c

out/crash_dump.o : kernel/crash_dump.c include/crash_dump.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -m64 -o out/crash_dump.o kernel/crash_dump.c

out/ftrace.o : kernel/ftrace.c include/ftrace.h
	$(X86_64_CC) -c -I include -O0 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -m64 -o out/ftrace.o kernel/ftrace.c

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
	"D:\Program Files (x86)\VMware\VMware Workstation\vmrun.exe" start "D:\Users\xiaot\Documents\Virtual Machines\其他 64 位\其他 64 位.vmx" gui

clean :
	if exist out cmd /c del /f /s /q out

default : clean run
