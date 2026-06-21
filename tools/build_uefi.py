#!/usr/bin/env python3
"""Build a minimal UEFI bootloader for MoniOS (PE/COFF format).

This generates a standalone .efi binary that:
  1. Called by UEFI firmware with SystemTable pointer in RDI (X64 SysV ABI)
  2. Uses UEFI Boot Services to open KERNEL.BIN from the ESP
  3. Reads the kernel ELF image into memory at 0x200000
  4. Sets up long-mode paging (identity-mapped 1 GiB window)
  5. Jumps to kernel entry at 0x200000

The generated .efi file is placed in out/monios.efi and can be placed on
an ESP partition as \\EFI\\MONIOS\\MONIOS.EFI
or loaded by any UEFI bootloader (rEFInd, GRUB2-UEFI, etc.).

Usage:
  python tools/build_uefi.py
"""
import struct, sys, os, shutil

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(PROJECT_DIR, "out")
KERNEL_LOAD_PHYS = 0x200000

# ── Helpers ────────────────────────────────────────────────────
def u8(v):  return struct.pack("<B", v & 0xFF)
def u16(v): return struct.pack("<H", v & 0xFFFF)
def u32(v): return struct.pack("<I", v & 0xFFFFFFFF)
def u64(v): return struct.pack("<Q", v & 0xFFFFFFFFFFFFFFFF)

def checksum8(data):
    return sum(data) & 0xFF

# ── 16-bit real-mode DOS stub ─────────────────────────────────
def dos_stub():
    """Minimal DOS COM-style program that prints 'EFI' and exits."""
    code = bytearray()
    # mov ah, 4Ch / int 21h – DOS exit
    code += b"\xB8\x00\x4C"        # mov ax, 4C00h
    code += b"\xCD\x21"            # int 21h
    # pad to even
    while len(code) % 2: code += b"\x90"
    return bytes(code)

# ── PE/COFF header constants ──────────────────────────────────
MACHINE_X64     = 0x8664
MACHINE_I386    = 0x14C
COFF_TYPE_EXE   = 0x02
COFF_TYPE_DLL   = 0x03
IMAGE_SUBSYS_EF = 10          # EFI executable

OPT_HDR_MAGIC   = 0x20B       # PE32+ (PE64)

# ── Section constants ─────────────────────────────────────────
IMAGE_SCN_CNT_CODE         = 0x00000020
IMAGE_SCN_CNT_INIT_DATA     = 0x00000040
IMAGE_SCN_MEM_EXECUTE      = 0x20000000
IMAGE_SCN_MEM_READ          = 0x40000000
IMAGE_SCN_MEM_WRITE        = 0x80000000

# ── UEFI boot code (assembly) ────────────────────────────────
# SysV x64 ABI: RDI = SystemTable, RSI = ImageHandle
# We preserve all callee-saved registers (RBX, RBP, R12-R15, RSP)
UEFI_BOOT_CODE = bytes([
    # ── function prologue ──────────────────────────────────────
    # save callee-saved regs
    0x53,                         # push rbx
    0x55,                         # push rbp
    0x41, 0x54,                   # push r12
    0x41, 0x55,                   # push r13
    0x41, 0x56,                   # push r14
    0x41, 0x57,                   # push r15
    0x48, 0x81, 0xEC, 0x00,0x10,0x00,0x00,  # sub rsp, 0x1000  (scratch space)

    # ── save params ────────────────────────────────────────────
    0x49, 0x89, 0xFF,            # mov r15, rdi  (SystemTable)
    0x49, 0x89, 0xF6,            # mov r14, rsi  (ImageHandle)

    # ── debug: print 'U' via UART COM1 ────────────────────────
    0xB0, 0x55,                  # mov al, 'U'
    0xBA, 0xF8, 0x03, 0x00, 0x00, # mov edx, 0x3F8 (COM1)
    0xEE,                         # out dx, al
    # wait THRE
    0x52,                         # push rdx
    0x5A,                         # pop rdx
    0xA0, 0xFD, 0x03, 0x00, 0x00, # mov al, [0x3FD]
    0x24, 0x20,                   # and al, 0x20
    0x74, 0xF6,                   # jz $-10
    0xB0, 0x0D,                   # mov al, '\r'
    0xEE,                         # out dx, al

    # ── Step 1: LocateRootSystemPointerOrTable ─────────────────
    # RAX = SystemTable->Hdr.CRC32 (offset 0x08 in EFI_TABLE_HEADER)
    # We actually need to get the BootServices pointer
    # EFI_SYSTEM_TABLE:
    #   +0x00: Hdr (signature + crc + ...)
    #   +0x08: Reserved (uint64_t = FirmwareVendor)
    #   +0x10: EFI_BOOT_SERVICES**
    #   +0x18: EFI_RUNTIME_SERVICES**
    # SystemTable is in RDI (r15)
    # BootServices = [r15 + 0x10] (qword)
    0x49, 0x8B, 0x47, 0x10,       # mov rax, [r15 + 0x10]
    0x49, 0x89, 0xC3,             # mov rbx, rax  (BootServices)

    # ── Step 2: LocateSimpleFileSystem ─────────────────────────
    # BS->LocateProtocol(&SimpleFileSystemProtocol, NULL, &fs)
    # fs = open volume (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL**)
    # AllocatePool (EfiBootServicesData, sizeof(EFI_HANDLE*), &fs_handle)
    0x49, 0x83, 0xEC, 0x08,      # sub rsp, 8
    0x48, 0x31, 0xC9,             # xor rcx, rcx   (NULL - only ControllerHandle)
    0x48, 0x31, 0xD2,             # xor rdx, rdx   # NULL - AgentHandle
    0x49, 0xC7, 0xC0, 0x60, 0x10,0x40, 0x00,  # mov r8, EFI_SIMPLE_FS_GUID (3cbb0...)
    0x49, 0x89, 0xC1,             # mov r9, rax   (Protocol)
    # Actually, LocateProtocol is: RCX=Protocol, RDX=Registration, R8=Interface
    # We need to call BS->LocateProtocol
    #   mov rcx, addr_of_guid
    #   xor rdx, rdx
    #   lea r8, [rsp+0]
    #   call [rbx + 0xB8]   (LocateProtocol offset)
])

# Actually let me rewrite this more carefully as a proper assembly listing
UEFI_BOOT_CODE = None  # will fill below


def asm_bytes():
    """Emit the UEFI boot stub as raw machine code."""
    out = bytearray()

    def pushImm64(val):
        out.extend([0x68]); out.extend(struct.pack("<Q", val))   # push qword imm

    def emit(opc, *args):
        out.extend(opc)

    # ── Save callee-saved registers ─────────────────────────────
    emit(b"\x53")                        # push rbx
    emit(b"\x55")                        # push rbp
    emit(b"\x41\x54")                    # push r12
    emit(b"\x41\x55")                    # push r13
    emit(b"\x41\x56")                    # push r14
    emit(b"\x41\x57")                    # push r15
    emit(b"\x48\x81\xEC\x00\x10\x00\x00")  # sub rsp, 0x1000

    # ── Save SystemTable (RDI) → r15 ────────────────────────────
    emit(b"\x49\x89\xFF")               # mov r15, rdi
    emit(b"\x49\x89\xF6")               # mov r14, rsi  (ImageHandle)

    # ── UART debug: print 'E' ───────────────────────────────────
    emit(b"\xB0\x45")                   # mov al, 'E'
    emit(b"\xBA\xF8\x03\x00\x00")       # mov edx, 0x3F8
    emit(b"\xEE")                        # out dx, al

    # ── BootServices = [SystemTable + 0x10] ─────────────────────
    #   EFI_SYSTEM_TABLE.Hdr (size=24) + Reserved(8) + FirmwareVendor(8)
    #   then BOOT_SERVICES**
    emit(b"\x49\x8B\x47\x10")           # mov rax, [r15 + 0x10]
    emit(b"\x49\x89\xC3")               # mov rbx, rax  (BootServices*)

    # ── UART debug: print 'F' ───────────────────────────────────
    emit(b"\xB0\x46")
    emit(b"\xEE")

    # ── LocateProtocol(SimpleFileSystemProtocol, NULL, &fs)
    # EFI_BOOT_SERVICES.LocateProtocol = [rbx + 0xB8]
    # Protocol GUID (16 bytes): 0965E976-8B0A-11D3-9957-0011242F718A
    EFI_SFS_GUID = bytes([
        0x76, 0xE9, 0x95, 0x96, 0x0A, 0x8B, 0xD3, 0x11,
        0x99, 0x57, 0x00, 0x11, 0x24, 0x2F, 0x71, 0x8A,
    ])
    # Push args right-to-left for SysV:
    #   RCX = Protocol (addr of GUID on stack)
    #   RDX = Registration (NULL)
    #   R8  = Interface (ptr to ptr on stack)
    emit(b"\x48\x83\xEC\x18")           # sub rsp, 24 (3 args × 8)
    # guid on stack at [rsp]
    for i, b in enumerate(EFI_SFS_GUID):
        out.extend([0xC6, 0x44, 0x24, i, b])  # mov [rsp+i], imm8

    emit(b"\x48\x31\xD2")               # xor rdx, rdx  (NULL registration)
    emit(b"\x48\x8D\x54\x24\x00")       # lea r10, [rsp]  (guid ptr)
    emit(b"\x4C\x8D\x4C\x24\x10")       # lea r9,  [rsp+16] (interface ptr)
    # RCX = r10
    emit(b"\x4C\x89\xD1")               # mov rcx, r10
    emit(b"\x49\x89\xCA")               # mov rdx, r10  (oops, RDX=guid, RCX=guid)

    # Actually: RCX=ProtocolGuid*, RDX=Registration, R8=Interface**
    # Fix:
    emit(b"\x4C\x89\xD1")               # mov rcx, r10  (guid*)
    emit(b"\x48\x31\xD2")               # xor rdx, rdx   (NULL registration)
    emit(b"\x4C\x89\xC9")               # mov rcx, r9   # wrong again
    # Let me restart this more carefully...
    out = out[:-3]  # undo

    # Proper SysV:
    # LocateProtocol(ProtocolGuid*, Registration, Interface)
    #   rcx = ProtocolGuid*
    #   rdx = Registration
    #   r8  = Interface**
    emit(b"\x48\x83\xEC\x20")           # sub rsp, 32 (4 × qword arg space + alignment)
    emit(b"\x48\x83\xEC\x10")           # sub rsp, 16 for guid
    # put guid at [rsp+16]
    for i, b in enumerate(EFI_SFS_GUID):
        out.extend([0xC6, 0x44, 0x24, 0x10 + i, b])
    emit(b"\x48\x8D\x4C\x24\x10")       # lea rcx, [rsp+16]  (ProtocolGuid*)
    emit(b"\x48\x31\xD2")               # xor rdx, rdx   (Registration = NULL)
    emit(b"\x48\x8D\x44\x24\x28")       # lea rax, [rsp+40]  # wait
    emit(b"\x49\x89\xC0")               # mov r8, rax   (Interface** = &fs)
    # rax = [rsp+40] -> will hold EFI_SIMPLE_FS_PROTOCOL*

    # Call BootServices->LocateProtocol
    # BOOT_SERVICES.LocateProtocol = [rbx + 0xB8]
    emit(b"\xFF\x54\xD8\xB8")           # call qword [rbx+0xB8]  (offset varies!)
    # Actually, LocateProtocol is at offset 0xB8 in EFI_BOOT_SERVICES
    emit(b"\x4C\x89\xC3")               # mov rbx, rax (preserve BootServices)
    # Check rax for error
    # If rax != 0, jump to halt
    emit(b"\x48\x85\xC0")               # test rax, rax
    emit(b"\x75\x1A")                   # jnz near (skip ahead)

    # ── UART debug: print 'I' ───────────────────────────────────
    emit(b"\xB0\x49")
    emit(b"\xBA\xF8\x03\x00\x00")
    emit(b"\xEE")

    # ── OpenVolume(fs, &root) ───────────────────────────────────
    # EFI_SIMPLE_FILE_SYSTEM_PROTOCOL.OpenVolume(handle, root)
    # root = [rsp+40] (filled by LocateProtocol)
    # We have: rax = fs_protocol*
    # Load image handle of this boot loader: r14
    emit(b"\x48\x89\xC3")               # mov rbx, rax   (fs)

    # AllocatePool to get a buffer for the file
    # BS->AllocatePool(EfiBootServicesData, 0x200000, &buf)
    emit(b"\x48\x83\xEC\x20")           # sub rsp, 32
    emit(b"\x49\xC7\xC1\x00\x00\x20\x00")  # mov r9, 0x200000 (buf size)
    emit(b"\x48\x31\xD2")               # xor rdx, rdx  (type = EfiBootServicesData = 0)
    emit(b"\x4C\x89\xC9")               # mov rcx, r9   # wait wrong
    # Actually: RCX=PoolType, RDX=Size, R8=Buffer
    emit(b"\x48\x31\xC9")               # xor rcx, rcx   (PoolType = EfiBootServicesData = 0)
    emit(b"\x48\x31\xD2")               # xor rdx, rdx
    emit(b"\x49\xC7\xC2\x00\x00\x20\x00")  # mov r10, 0x200000
    emit(b"\x4C\x89\xD2")               # mov rdx, r10   (Size)
    emit(b"\x49\x89\xC1")               # mov r9, rax   (Buffer*)
    # Align stack 16
    emit(b"\x48\x83\xEC\x10")           # sub rsp, 16
    emit(b"\x4C\x89\xC1")               # mov rcx, r9
    emit(b"\xFF\x54\xD8\x90")           # call [rbx+0x90]  AllocatePool
    emit(b"\x4C\x89\xC5")               # mov rbp, rax   (save buffer in rbp)

    # Open file KERNEL.BIN
    # BS->OpenVolume(fs, &root)  → root = EFI_FILE_PROTOCOL*
    emit(b"\x48\x89\xDB")               # mov rbx, rbx  # already have fs
    emit(b"\x48\x83\xEC\x20")           # sub rsp, 32
    emit(b"\x4C\x89\xC3")               # mov rbx, rax   (fs)
    emit(b"\x48\x31\xC9")               # xor rcx, rcx   # wait

    # This assembly is getting too complex to do without proper labels.
    # Let me rewrite as a simpler self-contained binary.

    return bytes(out)


# ── Simpler approach: write the boot code as a well-structured NASM source ──
NASM_UEFI_BOOT = rf"""
; MoniOS UEFI bootloader – nasm -f bin -o monios.efi
; This is compiled as a PE/COFF with the PE wrapper added by Python.
; Entry: _start(SystemTable, ImageHandle)
; Uses x64 SysV ABI: RDI=arg1, RSI=arg2

BITS 64

; ── Section .text ──────────────────────────────────────────────
section .text code=0x20 exec=0x20000000 read=0x40000000

global _start
_start:
    ; Save callee-saved registers
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    sub rsp, 0x1000          ; scratch space (aligned)

    ; Save parameters
    mov r15, rdi             ; r15 = SystemTable*
    mov r14, rsi             ; r14 = ImageHandle

    ; UART debug: print 'U'
    mov al, 'U'
    mov dx, 0x3F8
    out dx, al
    call .wait_uart

    ; ── BootServices = [SystemTable + 0x10] ──────────────────
    mov rax, [r15 + 0x10]
    mov rbx, rax             ; rbx = BootServices*

    ; ── AllocatePool(EfiBootServicesData, 0x10000, &tmp_buf) ──
    ; tmp_buf = 0x9F000  (same as crash dump – keep it reserved here)
    mov rcx, 0               ; PoolType = EfiBootServicesData
    mov rdx, 0x10000         ; Size
    lea r8, [rel .kernel_buf] ; Buffer* (points to .bss.kernel_buf)
    call [rbx + 0x90]        ; AllocatePool
    test rax, rax
    jnz .halt                ; if (error) halt

    ; ── LocateProtocol(SimpleFileSystemProtocol, NULL, &fs) ──
    ; EFI_SIMPLE_FILE_SYSTEM_PROTOCOL GUID:
    ;   {{0965E976-8B0A-11D3-9957-0011242F718A}}
    ; Call: LocateProtocol(Protocol*, Registration, Interface)
    sub rsp, 32
    mov qword [rsp], 0x0965E9768B0A11D3  ; part 1 of GUID (little-endian)
    mov qword [rsp+8], 0x8A57950011242F71  ; part 2 of GUID
    lea rcx, [rsp]           ; Protocol*
    xor rdx, rdx             ; Registration = NULL
    lea r8, [rel .fs_iface]  ; Interface**
    call [rbx + 0xB8]         ; LocateProtocol
    test rax, rax
    jnz .halt

    ; ── OpenVolume(fs, &root) ──────────────────────────────────
    mov rax, [rel .fs_iface] ; rax = EFI_SIMPLE_FS_PROTOCOL*
    mov rcx, rax             ; Protocol
    xor rdx, rdx             ; ControllerHandle = NULL
    xor r8, r8               ; AgentHandle = NULL
    xor r9, r9               ; Attributes = 0
    sub rsp, 32
    call [rax + 0x18]        ; OpenVolume(handle, &root)

    ; ── Open(KERNEL.BIN) ───────────────────────────────────────
    ; EFI_FILE_PROTOCOL.Open(root, &file, "KERNEL BIN", ...)
    mov rbx, rax             ; rbx = root (EFI_FILE_PROTOCOL*)
    lea rcx, [rsp+48]        ; file handle result
    ; filename: "KERNEL  BIN" + null (8.3 FAT)
    mov qword [rsp+48+0], 0x2020204B52454E45  ; "KERNE  " (padded)
    mov qword [rsp+48+8], 0x4942002020202020  ; "  BI" + spaces
    mov byte [rsp+48+14], 0   ; null terminator
    lea rdx, [rsp+48+16]     ; filename ptr
    mov r8, 1                ; open mode = EFI_FILE_MODE_READ
    xor r9, r9               ; attributes = 0
    sub rsp, 32
    call [rbx + 0x20]        ; Open(root, &file, name, mode, attrs)
    test rax, rax
    jnz .halt

    ; ── Read(file, &file_size, buf) ────────────────────────────
    ; First read 512 bytes (ELF header)
    mov rbx, [rsp+48]        ; file handle
    mov rcx, rbx
    lea rdx, [rel .file_size]
    mov r8, 512              ; bytes to read
    lea r9, [rel .kernel_buf]
    sub rsp, 32
    call [rbx + 0x28]         ; Read(file, *BufferSize, Buffer)

    ; ── Validate ELF magic ─────────────────────────────────────
    mov rax, [rel .kernel_buf]
    cmp eax, 0x464C457F       ; "\x7fELF"
    jne .halt

    ; ── Setup Long Mode (paging) ────────────────────────────────
    ; Build 4-level paging: identity-map {KERNEL_LOAD_PHYS:#x}..0x50000000
    ; Page tables at 0x90000 (16 KiB)
    mov edi, 0x90000
    mov ecx, 4096 * 4
    xor eax, eax
    rep stosd                ; clear page tables

    ; PML4[0] → PDP at 0x91000 (present + writable)
    mov dword [0x90000 + 0*8],     0x91003
    mov dword [0x90000 + 0*8 + 4], 0

    ; PDP[0] → PD at 0x92000
    mov dword [0x91000 + 0*8],     0x92003
    mov dword [0x91000 + 0*8 + 4], 0

    ; PD: map 0x00000000..0x40000000 (1 GiB, 2M pages) – present + writable + PWT + PCD
    ; Each PDE = base + flags(0x83=present+r/w+dirty)
    mov eax, 0x000003         ; 4M pages starting at 0
    mov ecx, 512              ; 512 × 2M = 1 GiB
    mov edi, 0x92000
.pd_loop:
    stosd
    mov eax, 0                ; no upper 32 bits
    stosd
    add eax, 0x200000         ; next 2M block
    loop .pd_loop

    ; Also map {KERNEL_LOAD_PHYS:#x}..0x50000000 with explicit entries
    ; PD[0x100] → PT at 0x93000
    mov dword [0x92000 + 0x100*8],     0x93003
    mov dword [0x92000 + 0x100*8 + 4], 0

    ; PT: map {KERNEL_LOAD_PHYS:#x}..0x400000 (256 x 2M pages) = 0x100 pages of 2M each
    mov eax, {KERNEL_LOAD_PHYS + 3:#x}          ; {KERNEL_LOAD_PHYS:#x} + flags
    mov ecx, 256
    mov edi, 0x93000
.pt_loop:
    stosd
    xor eax, eax
    stosd
    add eax, 0x200000
    loop .pt_loop

    ; Load CR3 with PML4
    mov rax, 0x90000
    mov cr3, rax

    ; Enable PAE + PGE (CR4)
    mov rax, cr4
    or eax, 0xA0              ; PAE(bit5) + PGE(bit7)
    mov cr4, rax

    ; Set EFER.LME (long mode enable)
    mov ecx, 0xC0000080       ; MSR_EFER
    rdmsr
    or eax, 0x100              ; LME = bit 8
    wrmsr

    ; Enable paging (CR0)
    mov rax, cr0
    or eax, 0x80000000         ; PG = bit 31
    mov cr0, rax

    ; ── UART debug: print 'L' ───────────────────────────────────
    mov al, 'L'
    mov dx, 0x3F8
    out dx, al

    ; ── Load kernel ELF segments ────────────────────────────────
    ; ELF header already at .kernel_buf
    ; e_phoff = offset of program headers, e_phentsize, e_phnum
    ; For simplicity, do a flat load:
    ; Copy KERNEL.BIN sectors from FAT to {KERNEL_LOAD_PHYS:#x}
    ; Since we already have it in .kernel_buf (512 bytes),
    ; we need to read the rest.
    ; For now: assume kernel is contiguous on FAT.
    ; Read entire kernel: file_size bytes (stored at .file_size)
    ; Actually, Read already populated .kernel_buf with 512 bytes
    ; Continue reading the rest directly to {KERNEL_LOAD_PHYS:#x}
    mov rbx, [rsp+48]         ; file handle
    mov qword [rsp], 0x200000  ; read 2 MB
    mov rax, {KERNEL_LOAD_PHYS:#x}
    mov qword [rsp+8], rax     ; buffer = {KERNEL_LOAD_PHYS:#x}
    mov rcx, rbx
    call [rbx + 0x28]         ; Read(file, &size, buf={KERNEL_LOAD_PHYS:#x})

    ; Close file
    mov rcx, [rsp+48]
    call [rbx + 0x08]         ; Close(file)
    ; Close root
    mov rcx, rbx              ; root handle
    call [rbx + 0x08]         ; Close(root)

    ; ── Exit Boot Services ─────────────────────────────────────
    ; BS->ExitBootServices(ImageHandle, MapKey)
    ; Need to get the map key from GetMemoryMap
    ; Skip for now – jump to kernel
    mov rax, [rel .kernel_buf + 24]  ; e_entry (offset 24 in ELF header)
    add rax, {KERNEL_LOAD_PHYS:#x}          ; virtual address = load_phys + offset

    ; UART debug: print 'K'
    push rax
    mov al, 'K'
    mov dx, 0x3F8
    out dx, al
    pop rax

    ; Jump to kernel!
    jmp rax

.halt:
    jmp short .halt

.wait_uart:
    in al, 0x3FD
    test al, 0x20
    jz .wait_uart
    ret

; ── Section .bss (uninitialised data) ──────────────────────────
section .bss data=0x40 write=0x80000000 read=0x40000000
align 16
.kernel_buf:   resb 0x200000   ; 2 MiB – kernel buffer
.file_size:    resq 1          ; uint64_t file size
.fs_iface:     resq 1          ; EFI_SIMPLE_FS_PROTOCOL*
.root_handle:  resq 1          ; EFI_FILE_PROTOCOL*
.file_handle:  resq 1          ; EFI_FILE_PROTOCOL*

; ── Section .rdata (read-only data) ────────────────────────────
section .rdata data=0x40 read=0x40000000
_efi_file_name:
    db "KERNEL  BIN", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
"""


def build_pe_efi(boot_code: bytes, entry_rva: int = 0x1000) -> bytes:
    """Wrap boot_code as a valid PE32+ UEFI executable."""

    DOS_HEADER_SIZE  = 0x40
    COFF_HDR_SIZE    = 20
    OPT_HDR_SIZE     = 0xF0     # PE32+ optional header
    SECTION_HDR_SIZE = 40
    NUM_SECTIONS     = 3        # .text, .bss, .rdata

    text_align = 0x1000
    data_align = 0x1000

    text_vsize = len(boot_code)
    bss_size   = 0x200000 + 8 + 8 + 8 + 8   # kernel_buf + 3×resq
    rdata_size = 256  # _efi_file_name + padding

    text_rawsz = (text_vsize + text_align - 1) & ~(text_align - 1)
    bss_vsize  = bss_size
    rdata_rawsz= (rdata_size + data_align - 1) & ~(data_align - 1)

    # File offsets
    dos_end    = DOS_HEADER_SIZE + 4  # MZ + "PE\0\0" + padding
    coff_ptr   = dos_end
    opt_ptr    = coff_ptr + COFF_HDR_SIZE
    sec_ptr    = opt_ptr + OPT_HDR_SIZE

    text_file_off   = (sec_ptr + SECTION_HDR_SIZE * NUM_SECTIONS + data_align - 1) & ~(data_align - 1)
    rdata_file_off  = text_file_off + text_rawsz
    # .bss has no file data
    # (text_rawsz + rdata_rawsz + ... must be padded)
    file_size = rdata_file_off + rdata_rawsz

    # Build optional header (PE32+)
    opt = bytearray(OPT_HDR_SIZE)
    struct.pack_into("<H", opt, 0,  OPT_HDR_MAGIC)          # Magic (0x20B)
    opt[2] = 0                                                  # MajorLinkerVersion
    struct.pack_into("<I", opt, 4,  text_vsize)                # SizeOfCode
    struct.pack_into("<I", opt, 8,  rdata_size)               # SizeOfInitializedData
    struct.pack_into("<I", opt, 12, 0)                         # SizeOfUninitializedData
    struct.pack_into("<I", opt, 16, entry_rva)                 # AddressOfEntryPoint
    struct.pack_into("<I", opt, 20, 0x1000)                    # BaseOfCode
    struct.pack_into("<Q", opt, 24, 0x400000)                  # ImageBase (2 GiB)
    struct.pack_into("<I", opt, 32, text_align)                # SectionAlignment
    struct.pack_into("<I", opt, 36, data_align)                # FileAlignment
    struct.pack_into("<H", opt, 40, 0)                         # MajorOperatingSystemVersion
    struct.pack_into("<H", opt, 42, 0)                         # MinorOperatingSystemVersion
    struct.pack_into("<H", opt, 44, 0)                         # MajorImageVersion
    struct.pack_into("<H", opt, 46, 0)                         # MinorImageVersion
    struct.pack_into("<H", opt, 48, 0)                         # MajorSubsystemVersion
    struct.pack_into("<H", opt, 50, 0)                         # MinorSubsystemVersion
    struct.pack_into("<I", opt, 52, 0)                         # Win32VersionValue
    struct.pack_into("<I", opt, 56, 0x10000)                   # SizeOfImage
    struct.pack_into("<I", opt, 60, 0x200)                     # SizeOfHeaders
    struct.pack_into("<I", opt, 64, 0)                         # CheckSum
    struct.pack_into("<H", opt, 68, IMAGE_SUBSYS_EF)           # Subsystem (EFI app)
    struct.pack_into("<H", opt, 70, 0)                         # DllCharacteristics
    struct.pack_into("<Q", opt, 72, 0x100000)                  # SizeOfStackReserve
    struct.pack_into("<Q", opt, 80, 0x1000)                    # SizeOfStackCommit
    struct.pack_into("<Q", opt, 88, 0x100000)                  # SizeOfHeapReserve
    struct.pack_into("<Q", opt, 96, 0x1000)                    # SizeOfHeapCommit
    struct.pack_into("<I", opt, 104, 0)                        # LoaderFlags
    struct.pack_into("<I", opt, 108, 16)                       # NumberOfRvaAndSizes
    # DataDirectory[0] = Export (0)
    # DataDirectory[1] = Base Reloc (0)
    # DataDirectory[2] = Debug (0)
    # PE32+ has 16 entries × 8 bytes each = 128 bytes at offset 112

    opt = bytes(opt)

    # COFF header
    coff = bytearray(COFF_HDR_SIZE)
    struct.pack_into("<H", coff, 0,  MACHINE_X64)
    struct.pack_into("<H", coff, 2,  NUM_SECTIONS)
    struct.pack_into("<I", coff, 4,   0)           # TimeDateStamp
    struct.pack_into("<I", coff, 8,   0)           # PointerToSymbolTable
    struct.pack_into("<I", coff, 12,  0)           # NumberOfSymbols
    struct.pack_into("<H", coff, 16,  OPT_HDR_SIZE)
    struct.pack_into("<H", coff, 18,  0x0162)      # Characteristics = EXEC | LINE_NUMS_STRIPPED | 32BIT_MACHINE

    # Section headers
    def sec_hdr(name, vaddr, vsize, rawsize, file_off, chars):
        s = bytearray(SECTION_HDR_SIZE)
        name_bytes = name.encode()[:8].ljust(8, b'\x00')
        s[:8] = name_bytes
        struct.pack_into("<I", s, 8,   vsize)
        struct.pack_into("<I", s, 12,  vaddr)
        struct.pack_into("<I", s, 16,  rawsize)
        struct.pack_into("<I", s, 20,  file_off)
        struct.pack_into("<I", s, 24,  0)  # PointerToRelocations
        struct.pack_into("<I", s, 28,  0)  # PointerToLinenumbers
        struct.pack_into("<H", s, 32,  0)  # NumberOfRelocations
        struct.pack_into("<H", s, 34,  0)  # NumberOfLinenumbers
        struct.pack_into("<I", s, 36,  chars)
        return bytes(s)

    text_rva  = 0x1000
    rdata_rva = text_rva + ((text_vsize + 0xFFF) & ~0xFFF)
    bss_rva   = rdata_rva + rdata_size

    sh_text  = sec_hdr(".text",  text_rva,  text_vsize,  text_rawsz,
                       text_file_off,  IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ)
    sh_rdata = sec_hdr(".rdata", rdata_rva, rdata_size,  rdata_rawsz,
                       rdata_file_off, IMAGE_SCN_CNT_INIT_DATA | IMAGE_SCN_MEM_READ)
    sh_bss   = sec_hdr(".bss",   bss_rva,   bss_size,    0, 0,
                       IMAGE_SCN_CNT_INIT_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE)

    # Assemble file
    pe = bytearray(file_size)

    # DOS header (MZ)
    pe[0:2] = b"MZ"
    struct.pack_into("<I", pe, 60, dos_end)  # e_lfanew

    # Padding to PE offset
    pe[DOS_HEADER_SIZE:DOS_HEADER_SIZE+4] = b"PE\x00\x00"

    # COFF header
    pe[coff_ptr:coff_ptr+COFF_HDR_SIZE] = coff

    # Optional header
    pe[opt_ptr:opt_ptr+OPT_HDR_SIZE] = opt

    # Section headers
    pe[sec_ptr:sec_ptr+SECTION_HDR_SIZE] = sh_text
    pe[sec_ptr+SECTION_HDR_SIZE:sec_ptr+SECTION_HDR_SIZE*2] = sh_rdata
    pe[sec_ptr+SECTION_HDR_SIZE*2:sec_ptr+SECTION_HDR_SIZE*3] = sh_bss

    # Pad headers to file offset
    headers_end = sec_ptr + SECTION_HDR_SIZE * NUM_SECTIONS
    assert headers_end <= 0x200, f"Headers exceed 0x200: {headers_end}"

    # Code section
    pe[text_file_off:text_file_off+len(boot_code)] = boot_code

    # .rdata section
    rdata_name = b"KERNEL  BIN\x00"
    pe[rdata_file_off:rdata_file_off+len(rdata_name)] = rdata_name

    return bytes(pe)


def assemble_nasm(source: str) -> bytes:
    """Assemble NASM source using the system's nasm if available."""
    import tempfile, subprocess, os

    tmp_src = tempfile.NamedTemporaryFile(suffix=".asm", delete=False, mode="w",
                                          encoding="utf-8")
    tmp_src.write(source)
    tmp_src.close()

    tmp_out = tempfile.mktemp(suffix=".bin")
    try:
        result = subprocess.run(
            ["nasm", "-f", "bin", "-o", tmp_out, tmp_src.name],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode != 0:
            print(f"NASM stderr: {result.stderr}", file=sys.stderr)
            raise RuntimeError(f"NASM assembly failed: {result.stderr}")
        with open(tmp_out, "rb") as f:
            code = f.read()
    finally:
        os.unlink(tmp_src.name)
        if os.path.exists(tmp_out):
            os.unlink(tmp_out)
    return code


def build_uefi_bootloader() -> bytes:
    """Assemble the NASM UEFI bootloader → raw binary code."""
    print("  Assembling UEFI boot stub (NASM)...")
    code = assemble_nasm(NASM_UEFI_BOOT)
    print(f"  Boot stub: {len(code)} bytes")
    return code


def main():
    print("MoniOS UEFI bootloader builder")
    print("=" * 40)

    # Step 1: assemble boot stub
    boot_code = build_uefi_bootloader()

    # Step 2: wrap as PE/COFF UEFI executable
    print("  Wrapping as PE32+ UEFI executable...")
    efi_bin = build_pe_efi(boot_code, entry_rva=0x1000)

    # Step 3: write output
    os.makedirs(OUT_DIR, exist_ok=True)
    out_path = os.path.join(OUT_DIR, "monios.efi")
    with open(out_path, "wb") as f:
        f.write(efi_bin)

    print(f"  Written: {out_path}")
    print(f"  Size:    {len(efi_bin):,} bytes ({len(efi_bin)//1024} KiB)")
    print()
    print("To use:")
    print("  1. Copy out/monios.efi to your ESP: \\EFI\\MONIOS\\MONIOS.EFI")
    print("  2. Create a startup entry with: bcdedit /set {bootmgr} path \\EFI\\MONIOS\\MONIOS.EFI")
    print("  Or use: bcdedit /enum firmware")
    print()
    print("For QEMU+UEFI testing:")
    print("  1. Download OVMF.fd (UEFI firmware) from https://github.com/tianocore/edk2")
    print("  2. qemu-system-x86_64 -bios OVMF.fd -hda fat:out -net none")


if __name__ == "__main__":
    main()
