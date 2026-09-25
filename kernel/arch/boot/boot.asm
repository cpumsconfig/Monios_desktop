    org 07c00h
BaseOfStack             equ 07c00h

    jmp short LABEL_START
    nop

%include "fat32hdr.inc"
%include "load.inc"

LABEL_START:
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, BaseOfStack
    mov [BootDrive], dl

    xor ah, ah
    mov dl, [BootDrive]
    int 13h

    ; ---- 从磁盘上的 BPB 读取真实几何（不再依赖编译期常量）--------------
    ; 引导扇区里的 BPB 已由 tools/mkfat32.py 按镜像实际尺寸改写，因此几何
    ; 必须在运行时读取。若沿用编译期常量，镜像尺寸一变就会算错根目录位置，
    ; 于是找不到 LOADER.BIN 而落到 LABEL_NO_LOADERBIN 死循环。
    ;   DATA_LBA = HiddSec + RsvdSecCnt + NumFATs * FATSz32
    movzx eax, word [BPB_RsvdSecCnt]
    movzx ecx, byte [BPB_NumFATs]
    mov edx, [BPB_FATSz32]
    imul edx, ecx
    add eax, edx
    add eax, [BPB_HiddSec]
    mov [wDataLba], eax

    ; cluster -> LBA 的加数 = DATA_LBA - 2 * SecPerClus
    movzx ecx, byte [BPB_SecPerClus]
    add ecx, ecx
    mov ebx, eax
    sub ebx, ecx
    mov [wClusterBase], ebx

    ; 根目录起始 LBA = DATA_LBA + (RootClus - 2) * SecPerClus
    mov ecx, [BPB_RootClus]
    sub ecx, 2
    movzx edx, byte [BPB_SecPerClus]
    imul ecx, edx
    add eax, ecx
    mov dword [wSectorNo], eax
LABEL_SEARCH_IN_ROOT_DIR_BEGIN:
    cmp word [wRootDirSizeForLoop], 0
    jz LABEL_NO_LOADERBIN
    dec word [wRootDirSizeForLoop]
    mov ax, BaseOfLoader
    mov es, ax
    mov bx, OffsetOfLoader
    mov eax, [wSectorNo]
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
    add dword [wSectorNo], 1
    jmp LABEL_SEARCH_IN_ROOT_DIR_BEGIN
LABEL_NO_LOADERBIN:
    jmp $

LABEL_FILENAME_FOUND:
    and di, 0FFE0h

    mov eax, [es:di + 01Ch]
    add eax, 511
    shr eax, 9
    mov [wFileSectors], ax

    add di, 01Ah
    movzx ecx, word [es:di]
    mov eax, [wClusterBase]
    add eax, ecx
    mov dword [wCurrentLba], eax
    mov word [wLoadSegment], BaseOfLoader
    mov word [wLoadOffset], OffsetOfLoader

LABEL_GOON_LOADING_FILE:
    cmp word [wFileSectors], 0
    jz LABEL_FILE_LOADED
    dec word [wFileSectors]

    mov eax, [wCurrentLba]
    mov dx, [wLoadSegment]
    mov es, dx
    mov bx, [wLoadOffset]
    mov cl, 1
    call ReadSector
    add dword [wCurrentLba], 1
    mov ax, [wLoadOffset]
    add ax, [BPB_BytsPerSec]
    mov [wLoadOffset], ax
    jnc LABEL_GOON_LOADING_FILE
    mov ax, [wLoadSegment]
    add ax, 1000h
    mov [wLoadSegment], ax
    jmp LABEL_GOON_LOADING_FILE
LABEL_FILE_LOADED:
    push word 0
    popf
    cld
    mov dl, [BootDrive]
    jmp BaseOfLoader:OffsetOfLoader

BootDrive           db 0
wRootDirSizeForLoop dw RootDirSectors
wSectorNo           dd 0
wFileSectors        dw 0
wCurrentLba         dd 0
wDataLba            dd 0
wClusterBase        dd 0
wLoadSegment        dw 0
wLoadOffset         dw 0
bOdd                db 0

LoaderFileName      db "LOADER  BIN", 0

ReadSector:
    push si
    push ds
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
