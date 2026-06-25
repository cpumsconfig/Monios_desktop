ENTRY(_start)

/*
 * MoniOS kernel linker script - x86_64
 *
 * The loader reads kernel.bin into a low real-mode buffer and then copies
 * the runtime image to 0x00200000. Keeping the runtime base above 1 MiB
 * avoids BIOS/loader overlap and leaves 1..2 MiB for boot-time scratch.
 */

SECTIONS
{
    . = 0x200000;

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

    .data : ALIGN(16) SUBALIGN(16)
    {
        _data_start = .;
        *(.data*)
        _data_end = .;
    }

    .kconfig : ALIGN(8)
    {
        _kconfig_start = .;
        KERNEL_BASE_PA   = 0x200000;
        KERNEL_BASE_VA   = 0xFFFF800000200000;
        QUAD(0);            /* kernel_base_pa, filled by kernel.c */
        QUAD(0);            /* kernel_base_va, filled by kernel.c */
        _panic_notifier_fn = .;
        QUAD(0);            /* panic notifier */
        QUAD(0);            /* reserved */
        _kconfig_end = .;
    }

    .pml4 : ALIGN(4096)
    {
        *(.pml4)
    }

    .bss : ALIGN(16) SUBALIGN(16)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }
}
