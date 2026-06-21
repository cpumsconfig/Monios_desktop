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
LABEL_GDT:
LABEL_DESC_NULL:     Descriptor 0,            0, 0
LABEL_DESC_FLAT_C:   Descriptor 0,      0fffffh, DA_C | DA_32 | DA_LIMIT_4K
LABEL_DESC_FLAT_RW:  Descriptor 0,      0fffffh, DA_DRW | DA_32 | DA_LIMIT_4K
LABEL_DESC_LONG_C:   Descriptor 0,      0,       0A09Ah
GdtLen equ $ - LABEL_GDT
GdtPtr dw GdtLen - 1
       dd BaseOfLoaderPhyAddr + LABEL_GDT
SelectorFlatC        equ LABEL_DESC_FLAT_C  - LABEL_GDT
SelectorFlatRW       equ LABEL_DESC_FLAT_RW - LABEL_GDT
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

    push eax
    mov eax, [es:di + 01Ch]
    mov dword [dwKernelSize], eax
    add eax, 511
    shr eax, 9
    mov word [wFileSectors], ax
    pop eax

    mov ax, RootDirSectors
    add di, 01Ah
    mov cx, word [es:di]
    add cx, ax
    add cx, DeltaSectorNo
    mov [wCurrentLba], cx
    mov word [wLoadSegment], BaseOfKernelFile
    mov word [wLoadOffset], OffsetOfKernelFile

LABEL_GOON_LOADING_FILE:
    cmp word [wFileSectors], 0
    jz LABEL_FILE_LOADED
    dec word [wFileSectors]

    mov ax, [wCurrentLba]
    mov dx, [wLoadSegment]
    mov es, dx
    mov bx, [wLoadOffset]

    mov cl, 1
    call ReadSector
    inc word [wCurrentLba]
    mov ax, [wLoadOffset]
    add ax, [BPB_BytsPerSec]
    mov [wLoadOffset], ax
    jnc LABEL_GOON_LOADING_FILE
    mov ax, [wLoadSegment]
    add ax, 1000h
    mov [wLoadSegment], ax
    jmp LABEL_GOON_LOADING_FILE
LABEL_FILE_LOADED:
    mov al, 'P'
    call DebugPutc
    lgdt [GdtPtr]
    cli

    in al, 92h
    or al, 00000010b
    out 92h, al

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp dword SelectorFlatC:(BaseOfLoaderPhyAddr + LABEL_PM_START)

dwKernelSize        dd 0
wRootDirSizeForLoop dw RootDirSectors
wSectorNo           dw 0
wFileSectors        dw 0
wCurrentLba         dw 0
wLoadSegment        dw 0
wLoadOffset         dw 0
bOdd                db 0
BootDrive           db 0

KernelFileName      db "KERNEL  BIN", 0

ReadSector:
    push si
    push ds
    mov [dap_lba], eax
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

DebugPutc:
    push ax
    push bx
    push dx
    mov dx, 03f8h
    out dx, al
    mov ah, 0eh
    mov bh, 0
    mov bl, 07h
    int 10h
    pop dx
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

    mov esi, BaseOfKernelLoadPhyAddr
    mov edi, BaseOfKernelFilePhyAddr
    mov ecx, [BaseOfLoaderPhyAddr + dwKernelSize]
    add ecx, 3
    shr ecx, 2
    cld
    rep movsd

    call SetupLongMode

SetupLongMode:
    mov edi, PageTablesBasePhys
    mov ecx, 4096
    xor eax, eax
    rep stosd

    mov dword [PageTablesBasePhys + 0], PageTablesBasePhys + 1000h + 003h
    mov dword [PageTablesBasePhys + 4], 0
    mov dword [PageTablesBasePhys + 1000h + 0], PageTablesBasePhys + 2000h + 003h
    mov dword [PageTablesBasePhys + 1000h + 4], 0
    mov dword [PageTablesBasePhys + 2000h + 0], 00000083h
    mov dword [PageTablesBasePhys + 2000h + 4], 0
    mov dword [PageTablesBasePhys + 2000h + 8], 00200083h
    mov dword [PageTablesBasePhys + 2000h + 0Ch], 0
    mov dword [PageTablesBasePhys + 2000h + 10h], 00400083h
    mov dword [PageTablesBasePhys + 2000h + 14h], 0
    mov dword [PageTablesBasePhys + 2000h + 18h], 00600083h
    mov dword [PageTablesBasePhys + 2000h + 1Ch], 0
    mov dword [PageTablesBasePhys + 2000h + 20h], 00800083h
    mov dword [PageTablesBasePhys + 2000h + 24h], 0
    mov dword [PageTablesBasePhys + 2000h + 28h], 00A00083h
    mov dword [PageTablesBasePhys + 2000h + 2Ch], 0
    mov dword [PageTablesBasePhys + 2000h + 30h], 00C00083h
    mov dword [PageTablesBasePhys + 2000h + 34h], 0
    mov dword [PageTablesBasePhys + 2000h + 38h], 00E00083h
    mov dword [PageTablesBasePhys + 2000h + 3Ch], 0

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
