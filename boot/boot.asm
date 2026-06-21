    org 07c00h
BaseOfStack             equ 07c00h

    jmp short LABEL_START
    nop

%include "fat32hdr.inc"
%include "load.inc"

LABEL_START:
    mov al, 'B'
    call DebugPutc
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, BaseOfStack
    mov [BootDrive], dl

    xor ah, ah
    mov dl, [BootDrive]
    int 13h

    mov word [wSectorNo], SectorNoOfRootDirectory
LABEL_SEARCH_IN_ROOT_DIR_BEGIN:
    cmp word [wRootDirSizeForLoop], 0
    jz LABEL_NO_LOADERBIN
    dec word [wRootDirSizeForLoop]
    mov ax, BaseOfLoader
    mov es, ax
    mov bx, OffsetOfLoader
    xor eax, eax
    mov ax, [wSectorNo]
    mov cl, 1
    call ReadSector

    mov si, LoaderFileName
    mov di, OffsetOfLoader
    cld
    mov dx, 10h
LABEL_SEARCH_FOR_LOADERBIN:
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
    mov si, LoaderFileName
    jmp LABEL_SEARCH_FOR_LOADERBIN
LABEL_GOTO_NEXT_SECTOR_IN_ROOT_DIR:
    add word [wSectorNo], 1
    jmp LABEL_SEARCH_IN_ROOT_DIR_BEGIN
LABEL_NO_LOADERBIN:
    mov al, 'N'
    call DebugPutc
    jmp $

LABEL_FILENAME_FOUND:
    mov al, 'L'
    call DebugPutc
    and di, 0FFE0h

    mov eax, [es:di + 01Ch]
    add eax, 511
    shr eax, 9
    mov [wFileSectors], ax

    mov ax, RootDirSectors
    add di, 01Ah
    mov cx, word [es:di]
    add cx, ax
    add cx, DeltaSectorNo
    mov [wCurrentLba], cx
    mov word [wLoadSegment], BaseOfLoader
    mov word [wLoadOffset], OffsetOfLoader

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
    mov al, 'J'
    call DebugPutc
    push word 0
    popf
    cld
    mov dl, [BootDrive]
    jmp BaseOfLoader:OffsetOfLoader

BootDrive           db 0
wRootDirSizeForLoop dw RootDirSectors
wSectorNo           dw 0
wFileSectors        dw 0
wCurrentLba         dw 0
wLoadSegment        dw 0
wLoadOffset         dw 0
bOdd                db 0

LoaderFileName      db "LOADER  BIN", 0

ReadSector:
    push si
    push ds
    movzx eax, ax
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

times 510 - ($ - $$) db 0
db 0x55, 0xaa
