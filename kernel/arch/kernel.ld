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

    /*
     * .data / .bss are aligned to 64 bytes and do NOT use SUBALIGN.
     *
     * SUBALIGN(16) used to be here. It forces every input section to a 16-byte
     * alignment, which *overrides* a larger alignment requested in the source
     * (__attribute__((aligned(64)))). That silently destroyed the 64-byte
     * alignment of the xHCI DMA structures: g_erst and g_dcbaa ended up at
     * addresses whose low 6 bits are non-zero, and the xHC hard-wires bits 5:0
     * of ERSTBA / DCBAAP / CRCR to zero. So the controller read the Event Ring
     * Segment Table 16 bytes early, took whatever garbage was there as the ring
     * base, and DMA-wrote event TRBs into unrelated memory - a wild write that
     * presented as a random hang much later in boot. Same class of bug applies
     * to NVMe and AHCI queue memory, so the fix belongs in the linker script
     * rather than in each driver.
     */
    .data : ALIGN(64)
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
        QUAD(0);            /* kernel heap base, filled by the UEFI loader */
        QUAD(0);            /* kernel heap size, filled by the UEFI loader */
        QUAD(0);            /* reserved */
        _kconfig_end = .;
    }

    .pml4 : ALIGN(4096)
    {
        *(.pml4)
    }

    .bss : ALIGN(64)
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
