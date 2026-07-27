ENTRY(_start)

/*
 * MoniOS kernel linker script - x86_64
 *
 * The PE image is based at 0x02000000. BIOS and UEFI loaders map its standard
 * PE sections at ImageBase + VirtualAddress before entering the PE entrypoint.
 */

SECTIONS
{
    . = 0x2000000;

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
        KERNEL_BASE_PA   = 0x2000000;
        KERNEL_BASE_VA   = 0xFFFF800002000000;
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

    .reloc : ALIGN(4)
    {
        *(.reloc)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note.GNU-stack)
    }
}
