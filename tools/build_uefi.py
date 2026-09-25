#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
import subprocess
import tempfile
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parent.parent
KERNEL_LOAD_PHYS = 0x2000000
KERNEL_MAX_PHYS = 0x3FFFFFFF
KERNEL_VIRT_BASE = 0xFFFF800002000000
KERNEL_MAX_BYTES = 0x400000
KERNEL_IMAGE_MAX_BYTES = 0x3000000
KERNEL_HEADER_PAGE_COUNT = 1
KERNEL_HEADER_READ_BYTES = 0x1000
KERNEL_HEADER_MAX_ADDRESS = 0x7FFFFFFF
FONT_REGION_PHYS = 0x6000000
FONT_HEADER_SIZE = 0x1000
FONT_MAX_BYTES = 0x1400000
FONT_DATA_PHYS = FONT_REGION_PHYS + FONT_HEADER_SIZE
FONT_PAGE_COUNT = (FONT_HEADER_SIZE + FONT_MAX_BYTES + 0xFFF) // 0x1000
BOOT_FONT_MAGIC = 0x544E464D
KERNEL_HEAP_SIZE = 0x2000000
KERNEL_HEAP_FALLBACK_SIZE = 0x1000000
KERNEL_HEAP_MIN_SIZE = 0x800000

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
%define BS_EXIT                         0xd8
%define BS_STALL                        0xf8
%define BS_SET_WATCHDOG_TIMER           0x100
%define EFI_ALLOCATE_ANY_PAGES          0
%define EFI_ALLOCATE_MAX_ADDRESS        1
%define EFI_ALLOCATE_ADDRESS            2
%define EFI_LOADER_DATA                 2
%define EFI_FILE_MODE_READ              1
%define EFI_TIMEOUT                     0x8000000000000012
%define EFI_LOADED_IMAGE_DEVICE_HANDLE  0x18
%define EFI_SYSTEM_TABLE_CON_IN         0x30
%define EFI_SYSTEM_TABLE_CON_OUT        0x40
%define EFI_SYSTEM_TABLE_CONFIG_COUNT   0x68
%define EFI_SYSTEM_TABLE_CONFIG_TABLE   0x70

%define SIMPLE_TEXT_INPUT_READ_KEY      0x08
%define SIMPLE_TEXT_OUTPUT_STRING       0x08
%define SIMPLE_TEXT_OUTPUT_CLEAR_SCREEN 0x30

%define SFS_OPEN_VOLUME                 0x08
%define FILE_OPEN                       0x08
%define FILE_CLOSE                      0x10
%define FILE_READ                       0x20
%define FILE_SET_POSITION               0x38

%define KERNEL_LOAD_PHYS                {KERNEL_LOAD_PHYS:#x}
%define KERNEL_MAX_PHYS                 {KERNEL_MAX_PHYS:#x}
%define KERNEL_VIRT_BASE                {KERNEL_VIRT_BASE:#x}
%define KERNEL_MAX_BYTES                {KERNEL_MAX_BYTES:#x}
%define KERNEL_IMAGE_MAX_BYTES          {KERNEL_IMAGE_MAX_BYTES:#x}
%define KERNEL_HEADER_PAGE_COUNT        {KERNEL_HEADER_PAGE_COUNT}
%define KERNEL_HEADER_READ_BYTES        {KERNEL_HEADER_READ_BYTES:#x}
%define KERNEL_HEADER_MAX_ADDRESS       {KERNEL_HEADER_MAX_ADDRESS:#x}
%define FONT_REGION_PHYS                {FONT_REGION_PHYS:#x}
%define FONT_HEADER_SIZE                {FONT_HEADER_SIZE:#x}
%define FONT_DATA_PHYS                  {FONT_DATA_PHYS:#x}
%define FONT_MAX_BYTES                  {FONT_MAX_BYTES:#x}
%define FONT_PAGE_COUNT                 {FONT_PAGE_COUNT}
%define BOOT_FONT_MAGIC                 {BOOT_FONT_MAGIC:#x}
%define KERNEL_HEAP_SIZE                {KERNEL_HEAP_SIZE:#x}
%define KERNEL_HEAP_FALLBACK_SIZE       {KERNEL_HEAP_FALLBACK_SIZE:#x}
%define KERNEL_HEAP_MIN_SIZE            {KERNEL_HEAP_MIN_SIZE:#x}
%define ISO_WAIT_POLL_USEC              1000000
%define ISO_WAIT_TIMEOUT_TICKS          10

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

    call find_acpi_rsdp
    call wait_for_iso_key_optional
    call iso_boot_animation

    ; root_dir->Open(root_dir, &kernel_file, L"\\KERNEL.EXE", READ, 0)
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

    ; Allocate one temporary page for the PE headers.
    mov rcx, EFI_ALLOCATE_MAX_ADDRESS
    mov rdx, EFI_LOADER_DATA
    mov r8, KERNEL_HEADER_PAGE_COUNT
    lea r9, [kernel_header_addr]
    sub rsp, 40
    call qword [rbx + BS_ALLOCATE_PAGES]
    add rsp, 40
    test rax, rax
    jnz fail_alloc_kernel_file

    ; Read only the PE headers to discover SizeOfImage.
    mov rcx, [kernel_file]
    lea rdx, [header_read_size]
    mov r8, [kernel_header_addr]
    mov rax, rcx
    sub rsp, 40
    call qword [rax + FILE_READ]
    add rsp, 40
    test rax, rax
    jnz fail_read_kernel

    ; Parse SizeOfImage and reserve that many pages in available memory.
    call kernel_image_size
    test rax, rax
    jz fail_kernel_pe
    mov [kernel_image_size_bytes], rax
    add rax, 0xfff
    shr rax, 12
    mov r8, rax
    mov qword [kernel_addr], KERNEL_LOAD_PHYS
    mov rcx, EFI_ALLOCATE_ADDRESS
    mov rdx, EFI_LOADER_DATA
    lea r9, [kernel_addr]
    sub rsp, 40
    call qword [rbx + BS_ALLOCATE_PAGES]
    add rsp, 40
    test rax, rax
    jz .kernel_allocated

    mov qword [kernel_addr], KERNEL_MAX_PHYS
    mov rcx, EFI_ALLOCATE_MAX_ADDRESS
    mov rdx, EFI_LOADER_DATA
    lea r9, [kernel_addr]
    sub rsp, 40
    call qword [rbx + BS_ALLOCATE_PAGES]
    add rsp, 40
    test rax, rax
    jnz fail_alloc_kernel
.kernel_allocated:

    ; Rewind the file and read the full PE into the allocated image.
    mov rcx, [kernel_file]
    xor edx, edx
    mov rax, rcx
    sub rsp, 40
    call qword [rax + FILE_SET_POSITION]
    add rsp, 40
    test rax, rax
    jnz fail_read_kernel

    mov qword [read_size], KERNEL_MAX_BYTES
    mov rcx, [kernel_file]
    lea rdx, [read_size]
    mov r8, [kernel_addr]
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

    ; Normalize the PE sections in place and compute the direct entrypoint.
    call load_kernel_pe
    test rax, rax
    jz fail_kernel_pe
    mov [kernel_entry], rax

    call load_font_optional
    call allocate_kernel_heap
    test rax, rax
    jz fail_kernel_heap
    call patch_kernel_config

    mov rcx, [root_dir]
    mov rax, rcx
    sub rsp, 40
    call qword [rax + FILE_CLOSE]
    add rsp, 40

    mov al, 'K'
    call serial_putc
    cli
    mov r12d, 1
    mov r13, [acpi_rsdp]
    mov rax, [kernel_entry]
    jmp rax

fail_alloc_kernel_file:
    mov al, 'B'
    jmp fail

fail_alloc_kernel:
    mov al, 'A'
    jmp fail

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
fail_read_kernel:
    mov al, 'R'
    jmp fail
fail_kernel_pe:
    mov al, 'E'
    jmp fail
fail_kernel_heap:
    mov al, 'Q'
    jmp fail

kernel_image_size:
    mov r10, [kernel_header_addr]
    mov r11, [header_read_size]
    cmp r11, 0x100
    jb .invalid
    cmp word [r10], 0x5a4d
    jne .invalid

    mov eax, [r10 + 0x3c]
    cmp rax, r11
    jae .invalid
    lea r9, [r10 + rax]
    cmp dword [r9], 0x4550
    jne .invalid

    movzx edx, word [r9 + 20]
    cmp edx, 112
    jb .invalid
    lea r8, [r9 + 24]
    cmp word [r8], 0x20b
    jne .invalid
    cmp qword [r8 + 24], KERNEL_LOAD_PHYS
    jne .invalid

    mov eax, [r8 + 56]
    test eax, eax
    jz .invalid
    cmp eax, KERNEL_IMAGE_MAX_BYTES
    ja .invalid
    mov edx, [r8 + 16]
    cmp edx, eax
    jae .invalid
    mov r12d, eax

    mov eax, [r8 + 152]
    mov [kernel_reloc_rva], eax
    mov eax, [r8 + 156]
    mov [kernel_reloc_size], eax

    movzx ecx, word [r9 + 6]
    movzx edx, word [r9 + 20]
    mov eax, [r10 + 0x3c]
    add eax, 24
    add eax, edx
    mov edx, eax
    mov eax, ecx
    imul eax, 40
    add edx, eax
    jc .invalid
    cmp rdx, r11
    ja .invalid

    lea rdi, [r9 + 24]
    movzx eax, word [r9 + 20]
    add rdi, rax
    xor eax, eax
.section_scan:
    test ecx, ecx
    jz .section_scan_done
    cmp qword [rdi], 0x6769666e6f636b2e
    jne .section_scan_next
    mov eax, [rdi + 12]
    mov [kernel_config_rva], eax
    jmp .section_scan_done
.section_scan_next:
    add rdi, 40
    dec ecx
    jmp .section_scan
.section_scan_done:
    mov eax, r12d
    ret

.invalid:
    xor eax, eax
    ret

load_kernel_pe:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov r15, [kernel_addr]
    mov rbp, [read_size]
    mov r14, [kernel_addr]
    cmp rbp, 0x100
    jb .invalid
    cmp word [r15], 0x5a4d
    jne .invalid

    mov eax, [r15 + 0x3c]
    cmp rax, rbp
    jae .invalid
    lea rbx, [r15 + rax]
    cmp dword [rbx], 0x4550
    jne .invalid

    movzx r12d, word [rbx + 6]
    test r12d, r12d
    jz .invalid
    movzx edx, word [rbx + 20]
    cmp edx, 112
    jb .invalid
    lea rsi, [rbx + 24]
    cmp word [rsi], 0x20b
    jne .invalid

    cmp qword [rsi + 24], KERNEL_LOAD_PHYS
    jne .invalid
    mov r13d, [rsi + 16]
    mov r8d, [rsi + 56]
    test r8d, r8d
    jz .invalid
    cmp r8d, KERNEL_IMAGE_MAX_BYTES
    ja .invalid
    cmp r13d, r8d
    jae .invalid

    lea rbx, [rbx + rdx + 24]
    mov eax, [r15 + 0x3c]
    add eax, 24
    add eax, edx
    mov edx, eax
    mov eax, r12d
    imul eax, 40
    add edx, eax
    jc .invalid
    cmp rdx, rbp
    ja .invalid

    mov r9d, r12d
    dec r9d
.section_loop:
    mov eax, r9d
    imul eax, 40
    lea rdi, [rbx + rax]
    mov r10d, [rdi + 8]
    mov r11d, [rdi + 12]
    mov r12d, [rdi + 16]
    mov edx, [rdi + 20]

    mov eax, r10d
    cmp eax, r12d
    jae .section_size_ready
    mov eax, r12d
.section_size_ready:
    test eax, eax
    jz .section_next
    add eax, r11d
    jc .invalid
    cmp eax, r8d
    ja .invalid

    test r12d, r12d
    jz .section_zero
    cmp edx, ebp
    jae .invalid
    mov eax, edx
    add eax, r12d
    jc .invalid
    cmp rax, rbp
    ja .invalid

    mov rsi, r15
    add rsi, rdx
    mov rdi, r14
    add rdi, r11
    cmp rdi, rsi
    jbe .copy_forward

    mov eax, r12d
    add rsi, rax
    add rdi, rax
    dec rsi
    dec rdi
    mov ecx, r12d
    std
    rep movsb
    cld
    jmp .section_zero

.copy_forward:
    mov ecx, r12d
    cld
    rep movsb

.section_zero:
    cmp r10d, r12d
    jbe .section_next
    sub r10d, r12d
    mov rdi, r14
    add rdi, r11
    mov eax, r12d
    add rdi, rax
    xor eax, eax
    mov ecx, r10d
    cld
    rep stosb

.section_next:
    dec r9d
    jns .section_loop

.entry:
    call apply_kernel_relocations
    test eax, eax
    jz .invalid
    mov eax, r13d
    add rax, r14
    jc .invalid
    mov r8, rax
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    mov rax, r8
    ret

.invalid:
    xor eax, eax
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

apply_kernel_relocations:
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov r14, [kernel_addr]
    mov r8, r14
    sub r8, KERNEL_LOAD_PHYS
    mov r13d, [kernel_reloc_size]
    test r13d, r13d
    jz .no_reloc
    mov eax, [kernel_reloc_rva]
    lea rbx, [r14 + rax]
    xor r12d, r12d

.block_loop:
    cmp r12d, r13d
    jae .done
    mov eax, r13d
    sub eax, r12d
    cmp eax, 8
    jb .invalid
    mov r10d, [rbx + r12]
    mov r11d, [rbx + r12 + 4]
    cmp r11d, 8
    jb .invalid
    mov eax, r11d
    add eax, r12d
    jc .invalid
    cmp eax, r13d
    ja .invalid
    lea rdi, [rbx + r12 + 8]
    mov ecx, r11d
    sub ecx, 8
    shr ecx, 1

.entry_loop:
    test ecx, ecx
    jz .block_next
    movzx eax, word [rdi]
    add rdi, 2
    mov edx, eax
    shr edx, 12
    and eax, 0xfff
    test edx, edx
    jz .entry_next
    cmp edx, 3
    je .highlow
    cmp edx, 10
    je .dir64
    jmp .invalid

.highlow:
    lea rsi, [r14 + r10]
    add rsi, rax
    mov eax, [rsi]
    add eax, r8d
    mov [rsi], eax
    jmp .entry_next

.dir64:
    lea rsi, [r14 + r10]
    add rsi, rax
    mov rax, [rsi]
    add rax, r8
    mov [rsi], rax

.entry_next:
    dec ecx
    jmp .entry_loop

.block_next:
    add r12d, r11d
    jmp .block_loop

.no_reloc:
    test r8, r8
    jnz .invalid
.done:
    mov eax, 1
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

.invalid:
    xor eax, eax
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

patch_kernel_config:
    mov eax, [kernel_config_rva]
    test eax, eax
    jz .done
    mov rdx, [kernel_addr]
    add rdx, rax
    mov rax, [kernel_addr]
    mov [rdx], rax
    mov rax, KERNEL_VIRT_BASE
    mov [rdx + 8], rax
    mov rax, [kernel_heap_addr]
    mov [rdx + 24], rax
    mov rax, [kernel_heap_size]
    mov [rdx + 32], rax
.done:
    ret

allocate_kernel_heap:
    mov qword [kernel_heap_size], KERNEL_HEAP_SIZE
    mov rcx, EFI_ALLOCATE_ANY_PAGES
    mov rdx, EFI_LOADER_DATA
    mov r8, KERNEL_HEAP_SIZE
    add r8, 0xfff
    shr r8, 12
    lea r9, [kernel_heap_addr]
    sub rsp, 40
    call qword [rbx + BS_ALLOCATE_PAGES]
    add rsp, 40
    test rax, rax
    jz .ready

    mov qword [kernel_heap_size], KERNEL_HEAP_FALLBACK_SIZE
    mov rcx, EFI_ALLOCATE_ANY_PAGES
    mov rdx, EFI_LOADER_DATA
    mov r8, KERNEL_HEAP_FALLBACK_SIZE
    add r8, 0xfff
    shr r8, 12
    lea r9, [kernel_heap_addr]
    sub rsp, 40
    call qword [rbx + BS_ALLOCATE_PAGES]
    add rsp, 40
    test rax, rax
    jz .ready

    mov qword [kernel_heap_size], KERNEL_HEAP_MIN_SIZE
    mov rax, [kernel_addr]
    add rax, [kernel_image_size_bytes]
    add rax, 0x1fffff
    and rax, 0xffffffffffe00000
    mov [kernel_heap_addr], rax
    mov rcx, EFI_ALLOCATE_ADDRESS
    mov rdx, EFI_LOADER_DATA
    mov r8, KERNEL_HEAP_MIN_SIZE
    add r8, 0xfff
    shr r8, 12
    lea r9, [kernel_heap_addr]
    sub rsp, 40
    call qword [rbx + BS_ALLOCATE_PAGES]
    add rsp, 40
    test rax, rax
    jnz .failed
.ready:
    mov al, 'H'
    call serial_putc
    mov eax, 1
    ret
.failed:
    xor eax, eax
    ret

find_acpi_rsdp:
    mov qword [acpi_rsdp], 0
    mov rcx, [r15 + EFI_SYSTEM_TABLE_CONFIG_COUNT]
    mov rdx, [r15 + EFI_SYSTEM_TABLE_CONFIG_TABLE]
    test rdx, rdx
    jz .done
.loop:
    test rcx, rcx
    jz .done
    ; ACPI 2.0 table GUID: 8868e871-e4f1-11d3-bc22-0080c73c8881
    cmp dword [rdx + 0], 0x8868e871
    jne .check_acpi10
    cmp word [rdx + 4], 0xe4f1
    jne .check_acpi10
    cmp word [rdx + 6], 0x11d3
    jne .check_acpi10
    cmp dword [rdx + 8], 0x800022bc
    jne .check_acpi10
    cmp dword [rdx + 12], 0x81883cc7
    je .found_acpi20
.check_acpi10:
    ; ACPI 1.0 table GUID: eb9d2d30-2d88-11d3-9a16-0090273fc14d
    cmp dword [rdx + 0], 0xeb9d2d30
    jne .next
    cmp word [rdx + 4], 0x2d88
    jne .next
    cmp word [rdx + 6], 0x11d3
    jne .next
    cmp dword [rdx + 8], 0x9000169a
    jne .next
    cmp dword [rdx + 12], 0x4dc13f27
    jne .next
    cmp qword [acpi_rsdp], 0
    jne .next
    mov rax, [rdx + 16]
    mov [acpi_rsdp], rax
    jmp .next
.found_acpi20:
    mov rax, [rdx + 16]
    mov [acpi_rsdp], rax
    ret
.next:
    add rdx, 24
    dec rcx
    jmp .loop
.done:
    ret

clear_firmware_screen:
    mov rcx, [r15 + EFI_SYSTEM_TABLE_CON_OUT]
    test rcx, rcx
    jz .cfs_done
    mov rax, rcx
    sub rsp, 40
    call qword [rax + SIMPLE_TEXT_OUTPUT_CLEAR_SCREEN]
    add rsp, 40
.cfs_done:
    ret

iso_boot_animation:
    call clear_firmware_screen
    mov rcx, [r15 + EFI_SYSTEM_TABLE_CON_OUT]
    test rcx, rcx
    jz .iba_done
    lea rdx, [iso_boot_title]
    mov rax, rcx
    sub rsp, 40
    call qword [rax + SIMPLE_TEXT_OUTPUT_STRING]
    add rsp, 40
    lea rdx, [iso_boot_stage0]
    mov rax, rcx
    sub rsp, 40
    call qword [rax + SIMPLE_TEXT_OUTPUT_STRING]
    add rsp, 40
    mov rcx, 350000
    sub rsp, 40
    call qword [rbx + BS_STALL]
    add rsp, 40
    mov rcx, [r15 + EFI_SYSTEM_TABLE_CON_OUT]
    lea rdx, [iso_boot_stage1]
    mov rax, rcx
    sub rsp, 40
    call qword [rax + SIMPLE_TEXT_OUTPUT_STRING]
    add rsp, 40
    mov rcx, 350000
    sub rsp, 40
    call qword [rbx + BS_STALL]
    add rsp, 40
    mov rcx, [r15 + EFI_SYSTEM_TABLE_CON_OUT]
    lea rdx, [iso_boot_stage2]
    mov rax, rcx
    sub rsp, 40
    call qword [rax + SIMPLE_TEXT_OUTPUT_STRING]
    add rsp, 40
    mov rcx, 350000
    sub rsp, 40
    call qword [rbx + BS_STALL]
    add rsp, 40
    mov rcx, [r15 + EFI_SYSTEM_TABLE_CON_OUT]
    lea rdx, [iso_boot_stage3]
    mov rax, rcx
    sub rsp, 40
    call qword [rax + SIMPLE_TEXT_OUTPUT_STRING]
    add rsp, 40
.iba_done:
    mov al, 'A'
    call serial_putc
    ret

wait_for_iso_key_optional:
    mov rcx, [root_dir]
    test rcx, rcx
    jz .wi_done
    lea rdx, [wait_file]
    lea r8, [wait_flag_name]
    mov r9, EFI_FILE_MODE_READ
    sub rsp, 40
    mov qword [rsp + 32], 0
    mov rax, rcx
    call qword [rax + FILE_OPEN]
    add rsp, 40
    test rax, rax
    jnz .wi_done
    mov rcx, [wait_file]
    mov rax, rcx
    sub rsp, 40
    call qword [rax + FILE_CLOSE]
    add rsp, 40
    mov al, 'W'
    call serial_putc
    call clear_firmware_screen
    mov rcx, [r15 + EFI_SYSTEM_TABLE_CON_OUT]
    test rcx, rcx
    jz .wi_skip_print
    lea rdx, [any_key_prompt]
    mov rax, rcx
    sub rsp, 40
    call qword [rax + SIMPLE_TEXT_OUTPUT_STRING]
    add rsp, 40
.wi_skip_print:
    mov rcx, [r15 + EFI_SYSTEM_TABLE_CON_IN]
    test rcx, rcx
    jz .wi_timeout
    xor r12d, r12d
.wi_read_loop:
    mov rcx, [r15 + EFI_SYSTEM_TABLE_CON_IN]
    test rcx, rcx
    jz .wi_timeout
    lea rdx, [input_key]
    mov rax, rcx
    sub rsp, 40
    call qword [rax + SIMPLE_TEXT_INPUT_READ_KEY]
    add rsp, 40
    test rax, rax
    jnz .wi_wait_more
    call clear_firmware_screen
    jmp .wi_done
.wi_wait_more:
    mov rcx, [r15 + EFI_SYSTEM_TABLE_CON_OUT]
    test rcx, rcx
    jz .wi_stall
    lea rdx, [poll_dot]
    mov rax, rcx
    sub rsp, 40
    call qword [rax + SIMPLE_TEXT_OUTPUT_STRING]
    add rsp, 40
.wi_stall:
    mov rcx, ISO_WAIT_POLL_USEC
    sub rsp, 40
    call qword [rbx + BS_STALL]
    add rsp, 40
    inc r12d
    cmp r12d, ISO_WAIT_TIMEOUT_TICKS
    jae .wi_timeout
    jmp .wi_read_loop
.wi_timeout:
    call clear_firmware_screen
    mov al, 'T'
    call serial_putc
    mov rcx, r14
    mov rdx, EFI_TIMEOUT
    xor r8d, r8d
    xor r9d, r9d
    sub rsp, 40
    call qword [rbx + BS_EXIT]
    add rsp, 40
    mov al, 'X'
    jmp fail
.wi_done:
    mov al, 'G'
    call serial_putc
    ret

load_font_optional:
    ; Reserve a boot-font region and copy \\FONTS\\MSYH.TTC into it for the kernel.
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
    dw 0x005c, 'K', 'E', 'R', 'N', 'E', 'L', '.', 'E', 'X', 'E', 0
font_name:
    dw 0x005c, 'F', 'O', 'N', 'T', 'S', 0x005c, 'M', 'S', 'Y', 'H', '.', 'T', 'T', 'C', 0

wait_flag_name:
    dw 0x005c, 'B', 'O', 'O', 'T', 'W', 'A', 'I', 'T', '.', 'F', 'L', 'G', 0
any_key_prompt:
    dw 'P', 'r', 'e', 's', 's', ' ', 'a', 'n', 'y', ' ', 'k', 'e', 'y', ' ', 't', 'o', ' ', 'b', 'o', 'o', 't', ' ', 'M', 'o', 'n', 'i', 'O', 'S', ' ', 'I', 'S', 'O', 13, 10, 0
poll_dot:
    dw '.', 0
iso_boot_title:
    dw 13, 10, 'M', 'o', 'n', 'i', 'O', 'S', ' ', 'I', 'S', 'O', 13, 10, 13, 10, 0
iso_boot_stage0:
    dw '[', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ']', ' ', 'Preparing firmware', 13, 10, 0
iso_boot_stage1:
    dw '[', '=', '=', '=', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ']', ' ', 'Loading kernel', 13, 10, 0
iso_boot_stage2:
    dw '[', '=', '=', '=', '=', '=', '=', ' ', ' ', ' ', ' ', ']', ' ', 'Staging UI resources', 13, 10, 0
iso_boot_stage3:
    dw '[', '=', '=', '=', '=', '=', '=', '=', '=', '=', '=', ']', ' ', 'Starting MoniOS', 13, 10, 0

align 8
loaded_image:
    dq 0
simple_fs:
    dq 0
root_dir:
    dq 0
kernel_file:
    dq 0
kernel_header_addr:
    dq KERNEL_HEADER_MAX_ADDRESS
header_read_size:
    dq KERNEL_HEADER_READ_BYTES
font_file:
    dq 0
wait_file:
    dq 0
kernel_entry:
    dq 0
kernel_addr:
    dq 0
kernel_image_size_bytes:
    dq 0
kernel_heap_addr:
    dq 0
kernel_heap_size:
    dq KERNEL_HEAP_SIZE
read_size:
    dq KERNEL_MAX_BYTES
kernel_reloc_rva:
    dq 0
kernel_reloc_size:
    dq 0
kernel_config_rva:
    dq 0
font_region_addr:
    dq FONT_REGION_PHYS
font_read_size:
    dq FONT_MAX_BYTES
input_key:
    dd 0
align 8
acpi_rsdp:
    dq 0
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
