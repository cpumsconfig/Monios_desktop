; ─────────────────────────────────────────────────────────────────────
; 引导扇区 / loader 共用的 FAT32 BPB。
;
; 重要：下面的几何数值必须与 tools/mkfat32.py 中的常量保持一致，
; 否则引导链会算错根目录位置而找不到 LOADER.BIN / KERNEL.EXE。
;
;   - boot.asm  在运行时从**磁盘上的** BPB 读取几何（mkfat32.py 会按镜像
;     实际尺寸改写引导扇区里的 BPB），所以它对这里的占位值不敏感。
;   - loader.asm 读的是它**自己内嵌**的这份 BPB —— mkfat32.py 不会改写
;     loader.bin —— 因此这里的数值必须是真实几何。
;
; mkfat32.py 在写入前会校验这里的数值，不一致会直接构建失败，不会静默启动黑屏。
; ─────────────────────────────────────────────────────────────────────

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
    BPB_TotSec32   dd 786432          ; 384MB，与 tools/mkfat32.py 的 TOTAL_SECTORS 一致
    BPB_FATSz32    dd 8192            ; 8192*512/4 = 1048576 个 FAT 表项，必须覆盖整个数据区
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

; 根目录扫描预算（扇区数），不是文件系统几何常量。
; FAT32 的根目录是簇链，这里给足空间即可；SecPerClus=1 时相当于扫这么多个簇。
RootDirSectors equ 64
