ENTRY(_start)

PHDRS
{
    app PT_LOAD FLAGS(7);
}

SECTIONS
{
    . = 0x04000000;

    . = ALIGN(0x1000);
    .text : {
        *(.text*)
    } :app

    . = ALIGN(0x1000);
    .rdata : {
        *(.rdata*)
        *(.rodata*)
    } :app

    . = ALIGN(0x1000);
    .idata : {
        KEEP (SORT(*)(.idata$2))
        KEEP (SORT(*)(.idata$3))
        LONG (0); LONG (0); LONG (0); LONG (0); LONG (0);
        . = ALIGN(8);
        KEEP (SORT(*)(.idata$4))
        __IAT_start__ = .;
        SORT(*)(.idata$5)
        __IAT_end__ = .;
        KEEP (SORT(*)(.idata$6))
        KEEP (SORT(*)(.idata$7))
        *(.idata)
    } :app

    . = ALIGN(0x1000);
    .edata : {
        *(.edata*)
    } :app

    . = ALIGN(0x1000);
    .rsrc : {
        KEEP(*(.rsrc$01))
        KEEP(*(.rsrc$02))
        *(.rsrc*)
    } :app

    . = ALIGN(0x1000);
    .data : {
        *(.data*)
    } :app

    . = ALIGN(0x1000);
    .bss : {
        *(.bss*)
        *(COMMON)
    } :app
}
