# MoniOS GDB init - load with:  gdb -x .gdbinit
#
# The kernel is a freestanding x86_64 ELF (kernel.elf / kernel.unsigned.exe
# linked with -Ttext 0x2001000).  Load symbols, then connect to the stub.

set architecture i386:x86-64
set endian little

# Point this at the ELF with symbols.  The PE kernel.unsigned.exe also works
# if your gdb supports PE; kernel.elf is the safest.
file out/kernel.elf

# Where the stub lives.  Two common setups:
#   (1) tools/gdb_connect.py relay on localhost:1234
#   (2) QEMU's own -gdb tcp::1234 (hardware-level, no stub needed)
target remote localhost:1234

# Convenience: break at the C entry point once connected.
# break *kmain
# continue

# After `continue` the stub drops you at its initial breakpoint.
# Useful commands:
#   info registers
#   x/10xg $rsp
#   bt
#   step / next
#   monitor quit
