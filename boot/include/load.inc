BaseOfLoader            equ 08000h ; Loader base (moved from 0x5000 to avoid kernel overlap)
OffsetOfLoader          equ 0100h  ; Loader offset
BaseOfLoaderPhyAddr     equ BaseOfLoader * 10h ; Loader physical address

BaseOfKernelFile        equ 02000h ; Real-mode kernel load buffer
OffsetOfKernelFile      equ 0h     ; Kernel offset
BaseOfKernelLoadPhyAddr equ BaseOfKernelFile * 10h ; Real-mode load buffer physical address
BaseOfKernelFilePhyAddr equ 0100000h ; Kernel runtime physical address
KernelEntryPointPhyAddr equ BaseOfKernelFilePhyAddr ; Kernel entry point
