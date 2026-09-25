ENTRY(DriverEntry)

PHDRS
{
    driver PT_LOAD FLAGS(7);
}

SECTIONS
{
    . = 0x05001000;

    . = ALIGN(0x1000);
    .text : {
        *(.text*)
    } :driver

    . = ALIGN(0x1000);
    .rdata : {
        *(.rdata*)
        *(.rodata*)
    } :driver

    . = ALIGN(0x1000);
    .edata : {
        *(.edata*)
    } :driver

    . = ALIGN(0x1000);
    .data : {
        *(.data*)
    } :driver

    . = ALIGN(0x1000);
    .bss : {
        *(.bss*)
        *(COMMON)
    } :driver

    .reloc : {
        *(.reloc*)
    } :driver
}
