#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
import subprocess
import tempfile
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parent.parent
KERNEL_LOAD_PHYS = 0x200000
KERNEL_MAX_BYTES = 0x400000
FONT_REGION_PHYS = 0x4000000
FONT_HEADER_SIZE = 0x1000
FONT_MAX_BYTES = 0x1800000
FONT_DATA_PHYS = FONT_REGION_PHYS + FONT_HEADER_SIZE
FONT_PAGE_COUNT = (FONT_HEADER_SIZE + FONT_MAX_BYTES + 0xFFF) // 0x1000
BOOT_FONT_MAGIC = 0x544E464D

MACHINE_X64 = 0x8664
PE32_PLUS_MAGIC = 0x20B
SUBSYSTEM_EFI_APPLICATION = 10

IMAGE_FILE_EXECUTABLE_IMAGE = 0x0002
IMAGE_FILE_LARGE_ADDRESS_AWARE = 0x0020
IMAGE_SCN_CNT_CODE = 0x00000020
IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_READ = 0x40000000
IMAGE_SCN_MEM_WRITE = 0x80000000


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def uefi_boot_source() -> str:
    return f"""
BITS 64
default rel

%define EFI_BOOT_SERVICES_OFFSET        0x60
%define BS_ALLOCATE_PAGES               0x28
%define BS_HANDLE_PROTOCOL              0x98
%define BS_SET_WATCHDOG_TIMER           0x100
%define EFI_ALLOCATE_ADDRESS            0
%define EFI_LOADER_DATA                 2
%define EFI_FILE_MODE_READ              1
%define EFI_LOADED_IMAGE_DEVICE_HANDLE  0x18

%define SFS_OPEN_VOLUME                 0x08
%define FILE_OPEN                       0x08
%define FILE_CLOSE                      0x10
%define FILE_READ                       0x20

%define KERNEL_LOAD_PHYS                {KERNEL_LOAD_PHYS:#x}
%define KERNEL_MAX_BYTES                {KERNEL_MAX_BYTES:#x}
%define KERNEL_PAGE_COUNT               {(KERNEL_MAX_BYTES + 0xfff) // 0x1000}
%define FONT_REGION_PHYS                {FONT_REGION_PHYS:#x}
%define FONT_HEADER_SIZE                {FONT_HEADER_SIZE:#x}
%define FONT_DATA_PHYS                  {FONT_DATA_PHYS:#x}
%define FONT_MAX_BYTES                  {FONT_MAX_BYTES:#x}
%define FONT_PAGE_COUNT                 {FONT_PAGE_COUNT}
%define BOOT_FONT_MAGIC                 {BOOT_FONT_MAGIC:#x}

section .text
global _start

_start:
    push rbp
    mov rbp, rsp
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15

    mov r14, rcx                    ; EFI_HANDLE ImageHandle
    mov r15, rdx                    ; EFI_SYSTEM_TABLE *SystemTable
    mov rbx, [r15 + EFI_BOOT_SERVICES_OFFSET]

    ; Disable the firmware watchdog before the kernel takes over.
    xor ecx, ecx
    xor edx, edx
    xor r8d, r8d
    xor r9d, r9d
    sub rsp, 40
    call qword [rbx + BS_SET_WATCHDOG_TIMER]
    add rsp, 40

    ; HandleProtocol(ImageHandle, LoadedImageProtocol, &loaded_image)
    mov rcx, r14
    lea rdx, [loaded_image_guid]
    lea r8, [loaded_image]
    sub rsp, 40
    call qword [rbx + BS_HANDLE_PROTOCOL]
    add rsp, 40
    test rax, rax
    jnz fail_loaded_image

    ; HandleProtocol(LoadedImage->DeviceHandle, SimpleFileSystem, &simple_fs)
    mov rax, [loaded_image]
    mov rcx, [rax + EFI_LOADED_IMAGE_DEVICE_HANDLE]
    lea rdx, [simple_file_system_guid]
    lea r8, [simple_fs]
    sub rsp, 40
    call qword [rbx + BS_HANDLE_PROTOCOL]
    add rsp, 40
    test rax, rax
    jnz fail_simple_fs

    ; simple_fs->OpenVolume(simple_fs, &root_dir)
    mov rcx, [simple_fs]
    lea rdx, [root_dir]
    mov rax, rcx
    sub rsp, 40
    call qword [rax + SFS_OPEN_VOLUME]
    add rsp, 40
    test rax, rax
    jnz fail_open_volume

    ; root_dir->Open(root_dir, &kernel_file, L"\\KERNEL.BIN", READ, 0)
    mov rcx, [root_dir]
    lea rdx, [kernel_file]
    lea r8, [kernel_name]
    mov r9, EFI_FILE_MODE_READ
    sub rsp, 40
    mov qword [rsp + 32], 0
    mov rax, rcx
    call qword [rax + FILE_OPEN]
    add rsp, 40
    test rax, rax
    jnz fail_open_kernel

    ; AllocatePages(AllocateAddress, EfiLoaderData, pages, &kernel_addr)
    mov rcx, EFI_ALLOCATE_ADDRESS
    mov rdx, EFI_LOADER_DATA
    mov r8, KERNEL_PAGE_COUNT
    lea r9, [kernel_addr]
    sub rsp, 40
    call qword [rbx + BS_ALLOCATE_PAGES]
    add rsp, 40
    test rax, rax
    jnz fail_alloc_kernel

    ; kernel_file->Read(kernel_file, &read_size, (void *)0x200000)
    mov rcx, [kernel_file]
    lea rdx, [read_size]
    mov r8, KERNEL_LOAD_PHYS
    mov rax, rcx
    sub rsp, 40
    call qword [rax + FILE_READ]
    add rsp, 40
    test rax, rax
    jnz fail_read_kernel

    mov rcx, [kernel_file]
    mov rax, rcx
    sub rsp, 40
    call qword [rax + FILE_CLOSE]
    add rsp, 40

    call load_font_optional

    mov rcx, [root_dir]
    mov rax, rcx
    sub rsp, 40
    call qword [rax + FILE_CLOSE]
    add rsp, 40

    mov al, 'K'
    call serial_putc
    cli
    mov rax, KERNEL_LOAD_PHYS
    jmp rax

fail_loaded_image:
    mov al, 'L'
    jmp fail
fail_simple_fs:
    mov al, 'S'
    jmp fail
fail_open_volume:
    mov al, 'V'
    jmp fail
fail_open_kernel:
    mov al, 'O'
    jmp fail
fail_alloc_kernel:
    mov al, 'A'
    jmp fail
fail_read_kernel:
    mov al, 'R'
    jmp fail

load_font_optional:
    ; Reserve a boot-font region and copy \\MSYH.TTC into it for the kernel.
    mov qword [font_region_addr], FONT_REGION_PHYS
    mov rcx, EFI_ALLOCATE_ADDRESS
    mov rdx, EFI_LOADER_DATA
    mov r8, FONT_PAGE_COUNT
    lea r9, [font_region_addr]
    sub rsp, 40
    call qword [rbx + BS_ALLOCATE_PAGES]
    add rsp, 40
    test rax, rax
    jnz .done

    mov rcx, [root_dir]
    lea rdx, [font_file]
    lea r8, [font_name]
    mov r9, EFI_FILE_MODE_READ
    sub rsp, 40
    mov qword [rsp + 32], 0
    mov rax, rcx
    call qword [rax + FILE_OPEN]
    add rsp, 40
    test rax, rax
    jnz .done

    mov qword [font_read_size], FONT_MAX_BYTES
    mov rcx, [font_file]
    lea rdx, [font_read_size]
    mov r8, FONT_DATA_PHYS
    mov rax, rcx
    sub rsp, 40
    call qword [rax + FILE_READ]
    add rsp, 40
    test rax, rax
    jnz .close_done

    mov rdi, FONT_REGION_PHYS
    mov dword [rdi + 0], BOOT_FONT_MAGIC
    mov dword [rdi + 4], FONT_HEADER_SIZE
    mov qword [rdi + 8], FONT_DATA_PHYS
    mov rax, [font_read_size]
    mov qword [rdi + 16], rax
    mov qword [rdi + 24], 0
    mov al, 'F'
    call serial_putc

.close_done:
    mov rcx, [font_file]
    mov rax, rcx
    sub rsp, 40
    call qword [rax + FILE_CLOSE]
    add rsp, 40
.done:
    ret

fail:
    call serial_putc
.hang:
    hlt
    jmp .hang

serial_putc:
    push rdx
    mov dx, 0x03f8
    out dx, al
    pop rdx
    ret

align 8
loaded_image_guid:
    db 0xa1,0x31,0x1b,0x5b,0x62,0x95,0xd2,0x11,0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b
simple_file_system_guid:
    db 0x22,0x5b,0x4e,0x96,0x59,0x64,0xd2,0x11,0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b
kernel_name:
    dw 0x005c, 'K', 'E', 'R', 'N', 'E', 'L', '.', 'B', 'I', 'N', 0
font_name:
    dw 0x005c, 'M', 'S', 'Y', 'H', '.', 'T', 'T', 'C', 0

align 8
loaded_image:
    dq 0
simple_fs:
    dq 0
root_dir:
    dq 0
kernel_file:
    dq 0
font_file:
    dq 0
kernel_addr:
    dq KERNEL_LOAD_PHYS
read_size:
    dq KERNEL_MAX_BYTES
font_region_addr:
    dq FONT_REGION_PHYS
font_read_size:
    dq FONT_MAX_BYTES
"""


def assemble_nasm(source: str) -> bytes:
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_path = Path(temp_dir)
        src = temp_path / "bootx64.asm"
        out = temp_path / "bootx64.bin"
        src.write_text(source, encoding="ascii")
        result = subprocess.run(
            ["nasm", "-f", "bin", "-o", str(out), str(src)],
            capture_output=True,
            text=True,
            timeout=30,
        )
        if result.returncode != 0:
            raise RuntimeError(result.stderr.strip() or "nasm failed")
        return out.read_bytes()


def build_pe32_plus_efi(text: bytes) -> bytes:
    dos_stub_size = 0x80
    file_alignment = 0x200
    section_alignment = 0x1000
    entry_rva = 0x1000
    optional_header_size = 0xF0
    section_header_size = 40
    coff_header_size = 20
    section_count = 1

    headers_size = align_up(
        dos_stub_size + 4 + coff_header_size + optional_header_size + section_header_size * section_count,
        file_alignment,
    )
    text_raw_size = align_up(len(text), file_alignment)
    size_of_image = align_up(entry_rva + len(text), section_alignment)
    image = bytearray(headers_size + text_raw_size)

    image[0:2] = b"MZ"
    struct.pack_into("<I", image, 0x3C, dos_stub_size)
    image[dos_stub_size:dos_stub_size + 4] = b"PE\0\0"

    coff = dos_stub_size + 4
    struct.pack_into("<H", image, coff + 0, MACHINE_X64)
    struct.pack_into("<H", image, coff + 2, section_count)
    struct.pack_into("<I", image, coff + 4, 0)
    struct.pack_into("<I", image, coff + 8, 0)
    struct.pack_into("<I", image, coff + 12, 0)
    struct.pack_into("<H", image, coff + 16, optional_header_size)
    struct.pack_into("<H", image, coff + 18, IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE)

    opt = coff + coff_header_size
    struct.pack_into("<H", image, opt + 0, PE32_PLUS_MAGIC)
    image[opt + 2] = 1
    struct.pack_into("<I", image, opt + 4, text_raw_size)
    struct.pack_into("<I", image, opt + 8, 0)
    struct.pack_into("<I", image, opt + 12, 0)
    struct.pack_into("<I", image, opt + 16, entry_rva)
    struct.pack_into("<I", image, opt + 20, entry_rva)
    struct.pack_into("<Q", image, opt + 24, 0)
    struct.pack_into("<I", image, opt + 32, section_alignment)
    struct.pack_into("<I", image, opt + 36, file_alignment)
    struct.pack_into("<H", image, opt + 40, 0)
    struct.pack_into("<H", image, opt + 42, 0)
    struct.pack_into("<H", image, opt + 44, 0)
    struct.pack_into("<H", image, opt + 46, 0)
    struct.pack_into("<H", image, opt + 48, 2)
    struct.pack_into("<H", image, opt + 50, 0)
    struct.pack_into("<I", image, opt + 52, 0)
    struct.pack_into("<I", image, opt + 56, size_of_image)
    struct.pack_into("<I", image, opt + 60, headers_size)
    struct.pack_into("<I", image, opt + 64, 0)
    struct.pack_into("<H", image, opt + 68, SUBSYSTEM_EFI_APPLICATION)
    struct.pack_into("<H", image, opt + 70, 0)
    struct.pack_into("<Q", image, opt + 72, 0x100000)
    struct.pack_into("<Q", image, opt + 80, 0x1000)
    struct.pack_into("<Q", image, opt + 88, 0x100000)
    struct.pack_into("<Q", image, opt + 96, 0x1000)
    struct.pack_into("<I", image, opt + 104, 0)
    struct.pack_into("<I", image, opt + 108, 16)

    sec = opt + optional_header_size
    image[sec:sec + 8] = b".text\0\0\0"
    struct.pack_into("<I", image, sec + 8, len(text))
    struct.pack_into("<I", image, sec + 12, entry_rva)
    struct.pack_into("<I", image, sec + 16, text_raw_size)
    struct.pack_into("<I", image, sec + 20, headers_size)
    struct.pack_into("<I", image, sec + 24, 0)
    struct.pack_into("<I", image, sec + 28, 0)
    struct.pack_into("<H", image, sec + 32, 0)
    struct.pack_into("<H", image, sec + 34, 0)
    struct.pack_into(
        "<I",
        image,
        sec + 36,
        IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE,
    )

    image[headers_size:headers_size + len(text)] = text
    return bytes(image)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build the MoniOS x64 UEFI boot application")
    parser.add_argument("--output", type=Path, default=PROJECT_DIR / "out" / "monios.efi")
    args = parser.parse_args()

    code = assemble_nasm(uefi_boot_source())
    image = build_pe32_plus_efi(code)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    print(f"UEFI: {args.output} ({len(image)} bytes, loader {len(code)} bytes)")


if __name__ == "__main__":
    main()
