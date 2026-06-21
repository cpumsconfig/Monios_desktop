    BS_OEMName     db 'MONIOS  '
    BPB_BytsPerSec dw 512
    BPB_SecPerClus db 1
    BPB_RsvdSecCnt dw 32
    BPB_NumFATs    db 2
    BPB_RootEntCnt dw 0
    BPB_TotSec16   dw 0
    BPB_Media      db 0xF8
    BPB_FATSz16    dw 0
    BPB_SecPerTrk  dw 63
    BPB_NumHeads   dw 16
    BPB_HiddSec    dd 0
    BPB_TotSec32   dd 202752
    BPB_FATSz32    dd 1576
    BPB_ExtFlags   dw 0
    BPB_FSVer      dw 0
    BPB_RootClus   dd 2
    BPB_FSInfo     dw 1
    BPB_BkBootSec  dw 6
    BPB_Reserved   times 12 db 0
    BS_DrvNum      db 0x80
    BS_Reserved1   db 0
    BS_BootSig     db 29h
    BS_VolID       dd 0
    BS_VolLab      db 'MONIOS     '
    BS_FileSysType db 'FAT32   '

FATSz                   equ 1576
RootDirSectors          equ 32
SectorNoOfRootDirectory equ 3184
SectorNoOfFAT1          equ 32
DeltaSectorNo           equ 3150
