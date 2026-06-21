ENTRY(_start)

/*
 * MoniOS kernel linker script – x86_64
 *
 * Memory layout (assumes identity-map in lower 2 GiB):
 *   [0x00000000 – 0x000FFFFF]  BIOS / IVT / EBDA            (reserved)
 *   [0x00100000 – 0x007FFFFF]  Kernel text + rodata + data   (5 MiB)
 *   [0x00800000 – 0x008FFFFF]  Kernel BSS (zeroed at boot)   (1 MiB)
 *   [0x00900000 – 0x009FFFFF]  Reserved
 *   [0x00A00000 – 0x00FFFFFF]  I/O hole / video RAM
 *   [0x9F000]                 Crash dump buffer (64 KiB fixed)
 *   [0x100000]                Kernel load address (link base)
 *
 * Virtual address space (after paging enabled):
 *   Low half:  identity-mapped 0x0000_0000 – 0x7FFF_FFFF
 *   Kernel:     0xFFFF_8000_0000_0000 + 0x100000 = 0xFFFF_8000_0010_0000
 *              (upper half alias – set up by bootloader/paging)
 */

SECTIONS
{
    . = 0x100000;

    /* ── Kernel text + read-only data ───────────────────────── */
    .text : ALIGN(16) SUBALIGN(16)
    {
        _text_start = .;
        *(.text*)
        _text_end = .;
    }

    .rodata : ALIGN(16) SUBALIGN(16)
    {
        _rodata_start = .;
        *(.rodata*)
        *(.rdata)
        . = ALIGN(8);
        _rodata_end = .;
    }

    /* ── Writable data ──────────────────────────────────────── */
    .data : ALIGN(16) SUBALIGN(16)
    {
        _data_start = .;
        *(.data*)
        _data_end = .;
    }

    /* ── Kernel configuration / notifier chain ────────────────
     * This small section holds pointers used by crash_dump
     * and other subsystems to find kernel global state.
     * It is placed at a fixed VA so early crash_dump code
     * (before paging is fully set up) can reach it via identity
     * mapping.
     *
     * Layout:
     *   [0]  uint64_t  kernel_base_pa   – physical base of kernel image
     *   [8]  uint64_t  kernel_base_va   – virtual base (upper half alias)
     *   [16] uint64_t  panic_notifier_fn – (*fn)(const char *msg, uint64_t rip)
     *   [24] uint64_t  reserved
     */
    .kconfig : ALIGN(8)
    {
        _kconfig_start = .;
        KERNEL_BASE_PA   = 0x100000;
        KERNEL_BASE_VA   = 0xFFFF800000100000;
        _panic_notifier_fn = .;
        LONG(0);            /* placeholder – filled by kernel.c */
        LONG(0);
        LONG(0);
        _kconfig_end = .;
    }

    /* ── PML4 page: must be page-aligned for CR3 pointer ───────── */
    .pml4 : ALIGN(4096)
    {
        *(.pml4)
    }

    /* ── BSS – zeroed by the boot loader ─────────────────────── */
    .bss : ALIGN(16) SUBALIGN(16)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }
    

    /* ── Debug symbols (DWARF .debug_* are stripped by objcopy
     *   -S, so only the ELF sections above are loaded into RAM) */
}
