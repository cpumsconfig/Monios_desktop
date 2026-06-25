BaseOfLoader            equ 09000h ; Loader base: keep above kernel's low bounce buffer
OffsetOfLoader          equ 0100h  ; Loader offset
BaseOfLoaderPhyAddr     equ BaseOfLoader * 10h ; Loader physical address

BaseOfKernelFile        equ 01000h ; Real-mode 512-byte kernel bounce buffer
OffsetOfKernelFile      equ 0h     ; Kernel offset
BaseOfKernelLoadPhyAddr equ BaseOfKernelFile * 10h ; Bounce buffer physical address
BaseOfKernelFilePhyAddr equ 0200000h ; Kernel runtime physical address (2 MiB)
KernelEntryPointPhyAddr equ BaseOfKernelFilePhyAddr ; Kernel entry point
