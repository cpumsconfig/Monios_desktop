ENTRY(_start)

PHDRS
{
    app PT_LOAD FLAGS(7);
}

SECTIONS
{
    . = 0x04000000;

    .text : ALIGN(0x1000) {
        *(.text*)
    } :app

    .rodata : ALIGN(0x1000) {
        *(.rodata*)
    } :app

    .data : ALIGN(0x1000) {
        *(.data*)
    } :app

    .bss : ALIGN(0x1000) {
        *(.bss*)
        *(COMMON)
    } :app
}
