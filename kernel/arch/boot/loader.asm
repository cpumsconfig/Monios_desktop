    org 0100h
BaseOfStack                 equ 0F000h

    jmp LABEL_START

%include "fat32hdr.inc"
%include "load.inc"
%include "pm.inc"

%define IA32_EFER           0C0000080h
%define EFER_LME            00000100h
%define PageTablesBasePhys  070000h
%define StackTop32Phys      074000h
%define StackTop64Phys      076000h
%define SectorSize          512
LABEL_GDT:
LABEL_DESC_NULL:     Descriptor 0,            0, 0
LABEL_DESC_FLAT_C:   Descriptor 0,      0fffffh, DA_C | DA_32 | DA_LIMIT_4K
LABEL_DESC_FLAT_RW:  Descriptor 0,      0fffffh, DA_DRW | DA_32 | DA_LIMIT_4K
LABEL_DESC_LOADER_C16: Descriptor BaseOfLoaderPhyAddr, 0ffffh, DA_C
LABEL_DESC_LONG_C:   Descriptor 0,      0,       0A09Ah
GdtLen equ $ - LABEL_GDT
GdtPtr dw GdtLen - 1
       dd BaseOfLoaderPhyAddr + LABEL_GDT
SelectorFlatC        equ LABEL_DESC_FLAT_C  - LABEL_GDT
SelectorFlatRW       equ LABEL_DESC_FLAT_RW - LABEL_GDT
SelectorLoaderC16    equ LABEL_DESC_LOADER_C16 - LABEL_GDT
SelectorLongC        equ LABEL_DESC_LONG_C  - LABEL_GDT

LABEL_START:
    mov al, 'R'
    call DebugPutc
    cli
    push word 0
    popf
    cli
    cld
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, BaseOfStack
    mov [BootDrive], dl

    mov word [wSectorNo], SectorNoOfRootDirectory
LABEL_SEARCH_IN_ROOT_DIR_BEGIN:
    cmp word [wRootDirSizeForLoop], 0
    jz LABEL_NO_KERNELBIN
    dec word [wRootDirSizeForLoop]
    mov ax, BaseOfKernelFile
    mov es, ax
    mov bx, OffsetOfKernelFile
    xor eax, eax
    mov ax, [wSectorNo]
    mov cl, 1
    call ReadSector

    mov si, KernelFileName
    mov di, OffsetOfKernelFile
    cld
    mov dx, 10h
LABEL_SEARCH_FOR_KERNELBIN:
    cmp dx, 0
    jz LABEL_GOTO_NEXT_SECTOR_IN_ROOT_DIR
    dec dx
    mov cx, 11
LABEL_CMP_FILENAME:
    cmp cx, 0
    jz LABEL_FILENAME_FOUND
    dec cx
    lodsb
    cmp al, byte [es:di]
    jz LABEL_GO_ON
    jmp LABEL_DIFFERENT
LABEL_GO_ON:
    inc di
    jmp LABEL_CMP_FILENAME

LABEL_DIFFERENT:
    and di, 0FFE0h
    add di, 20h
    mov si, KernelFileName
    jmp LABEL_SEARCH_FOR_KERNELBIN
LABEL_GOTO_NEXT_SECTOR_IN_ROOT_DIR:
    add word [wSectorNo], 1
    jmp LABEL_SEARCH_IN_ROOT_DIR_BEGIN
LABEL_NO_KERNELBIN:
    mov al, 'K'
    call DebugPutc
    jmp $

LABEL_FILENAME_FOUND:
    mov al, 'F'
    call DebugPutc
    and di, 0FFE0h

    mov eax, [es:di + 01Ch]
    mov dword [dwKernelSize], eax
    add eax, 511
    shr eax, 9
    mov dword [dwFileSectors], eax

    movzx eax, word [es:di + 014h]
    shl eax, 16
    mov ax, word [es:di + 01Ah]
    add eax, RootDirSectors + DeltaSectorNo
    mov dword [dwCurrentLba], eax
    mov dword [dwKernelLoadPhys], BaseOfKernelFilePhyAddr
    mov dword [dwKernelLoadPhys + 4], 0

    call EnableA20

LABEL_GOON_LOADING_FILE:
    cmp dword [dwFileSectors], 0
    jz LABEL_FILE_LOADED
    dec dword [dwFileSectors]

    mov eax, [dwCurrentLba]
    mov dx, BaseOfKernelFile
    mov es, dx
    mov bx, OffsetOfKernelFile
    mov cl, 1
    call ReadSector
    jmp EnterUnrealMode
LABEL_UNREAL_READY:
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, BaseOfStack
    call CopyBounceToKernel
    inc dword [dwCurrentLba]
    add dword [dwKernelLoadPhys], SectorSize
    adc dword [dwKernelLoadPhys + 4], 0
    jmp LABEL_GOON_LOADING_FILE
LABEL_FILE_LOADED:
    mov al, 'P'
    call DebugPutc
    lgdt [GdtPtr]
    cli

    call EnableA20

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp dword SelectorFlatC:(BaseOfLoaderPhyAddr + LABEL_PM_START)

dwKernelSize        dd 0
dwFileSectors       dd 0
dwCurrentLba        dd 0
dwKernelLoadPhys    dq 0
wRootDirSizeForLoop dw RootDirSectors
wSectorNo           dw 0
bOdd                db 0
BootDrive           db 0

KernelFileName      db "KERNEL  BIN", 0

EnableA20:
    in al, 92h
    or al, 00000010b
    out 92h, al
    ret

ReadSector:
    push si
    push ds
    mov byte [DiskAddressPacket], 10h
    mov [dap_lba], eax
    mov dword [dap_lba + 4], 0
    mov [dap_count], cl
    mov [dap_offset], bx
    mov ax, es
    mov [dap_segment], ax
    mov ax, cs
    mov ds, ax
    mov si, DiskAddressPacket
    mov ah, 42h
    mov dl, [BootDrive]
    int 13h
    pop ds
    pop si
    ret

EnterUnrealMode:
    cli
    mov ax, cs
    mov ds, ax
    lgdt [GdtPtr]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword SelectorFlatC:(BaseOfLoaderPhyAddr + LABEL_UNREAL_PM)

DebugPutc:
    push ax
    push bx
    push ds
    push es
    push dx
    mov dx, 03f8h
    out dx, al
    mov ah, 0eh
    mov bh, 0
    mov bl, 07h
    int 10h
    pop dx
    pop es
    pop ds
    pop bx
    pop ax
    ret

DiskAddressPacket:
    db 10h
    db 0
dap_count:
    dw 0
dap_offset:
    dw 0
dap_segment:
    dw 0
dap_lba:
    dq 0

CopyBounceToKernel:
    mov ax, cs
    mov ds, ax
    mov esi, BaseOfKernelLoadPhyAddr
    mov edi, [dwKernelLoadPhys]
    mov cx, SectorSize / 4
.copy_dword:
    mov eax, [fs:esi]
    mov [fs:edi], eax
    add esi, 4
    add edi, 4
    loop .copy_dword
    ret

align 32
[bits 32]
LABEL_UNREAL_PM:
    mov ax, SelectorFlatRW
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, StackTop32Phys
    mov fs, ax
    mov gs, ax
    jmp SelectorLoaderC16:LABEL_UNREAL_PM16

[bits 16]
LABEL_UNREAL_PM16:
    mov eax, cr0
    and eax, 0FFFFFFFEh
    mov cr0, eax
    jmp BaseOfLoader:LABEL_UNREAL_READY

align 32
[bits 32]
LABEL_PM_START:
    mov ax, SelectorFlatRW
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, StackTop32Phys

    jmp SetupLongMode

SetupLongMode:
    mov edi, PageTablesBasePhys
    mov ecx, 4096
    xor eax, eax
    rep stosd

    mov dword [PageTablesBasePhys + 0], PageTablesBasePhys + 1000h + 003h
    mov dword [PageTablesBasePhys + 4], 0
    mov dword [PageTablesBasePhys + 1000h + 0], PageTablesBasePhys + 2000h + 003h
    mov dword [PageTablesBasePhys + 1000h + 4], 0
    mov edi, PageTablesBasePhys + 2000h
    xor ebx, ebx
    mov ecx, 512
.map_identity_1g:
    mov eax, ebx
    or eax, 00000083h
    stosd
    xor eax, eax
    stosd
    add ebx, 00200000h
    loop .map_identity_1g

    mov eax, PageTablesBasePhys
    mov cr3, eax

    mov eax, cr4
    or eax, 20h
    mov cr4, eax

    mov ecx, IA32_EFER
    rdmsr
    or eax, EFER_LME
    wrmsr

    mov eax, cr0
    or eax, 80000000h
    mov cr0, eax
    jmp SelectorLongC:(BaseOfLoaderPhyAddr + LongModeEntry)

[bits 64]
LongModeEntry:
    mov ax, SelectorFlatRW
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov rsp, StackTop64Phys
    mov rbp, rsp

    mov rax, BaseOfKernelFilePhyAddr
    jmp rax
