[bits 64]
[default rel]

%define GDT_USER_DATA_SELECTOR 0x18
%define GDT_USER_CODE_SELECTOR 0x20
%define GDT_KERNEL_DATA_SELECTOR 0x10

section .bss
align 16
StackSpace resb 8192
StackTop:

section .text
global _start
global StackTop
global default_interrupt_handler
global exception0_handler
global exception1_handler
global exception2_handler
global exception3_handler
global exception4_handler
global exception5_handler
global exception6_handler
global exception7_handler
global exception8_handler
global exception9_handler
global exception10_handler
global exception11_handler
global exception12_handler
global exception13_handler
global exception14_handler
global exception15_handler
global exception16_handler
global exception17_handler
global exception18_handler
global exception19_handler
global exception20_handler
global exception21_handler
global exception22_handler
global exception23_handler
global exception24_handler
global exception25_handler
global exception26_handler
global exception27_handler
global exception28_handler
global exception29_handler
global exception30_handler
global exception31_handler
global irq0_interrupt_handler
global irq1_interrupt_handler
global irq2_interrupt_handler
global irq3_interrupt_handler
global irq4_interrupt_handler
global irq5_interrupt_handler
global irq6_interrupt_handler
global irq7_interrupt_handler
global irq8_interrupt_handler
global irq9_interrupt_handler
global irq10_interrupt_handler
global irq11_interrupt_handler
global irq12_interrupt_handler
global irq13_interrupt_handler
global irq14_interrupt_handler
global irq15_interrupt_handler
global syscall_interrupt_handler
global exec_enter_user_mode
extern __bss_end
extern __bss_start
extern bsod_unhandled_interrupt
extern cpu_exception_dispatch
extern kernel_main
extern syscall_interrupt_dispatch
extern timer_interrupt_dispatch
extern keyboard_interrupt_dispatch_wrapper
extern mouse_interrupt_dispatch_wrapper
extern generic_irq_interrupt_dispatch
extern exec_process_completed
extern gdb_stub_handle_exception
extern ftrace_record_entry
extern ftrace_record_exit
extern exec_resume_stack_pointer

_start:
    cli
    mov rsp, StackTop
    mov rbp, rsp
    lea rdi, [rel __bss_start]
    lea rcx, [rel __bss_end]
    sub rcx, rdi
    xor eax, eax
    rep stosb
    call kernel_main

.halt:
    hlt
    jmp .halt

default_interrupt_handler:
    cli
    cld
    call bsod_unhandled_interrupt
    hlt
    jmp $

%macro EXCEPTION_NO_ERROR 1
exception%1_handler:
    cli
    cld
    push qword 0
    push qword %1
    jmp exception_common
%endmacro

%macro EXCEPTION_WITH_ERROR 1
exception%1_handler:
    cli
    cld
    push qword %1
    jmp exception_common
%endmacro

exception_common:
    mov rdi, rsp
    call cpu_exception_dispatch
    cmp rax, 0
    je .halt
    add rsp, 16
    call exec_resume_stack_pointer
    mov rsp, rax
    mov ax, GDT_KERNEL_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    sti
    ret
.halt:
    hlt
    jmp .halt

EXCEPTION_NO_ERROR 0
EXCEPTION_NO_ERROR 1

exception3_handler:
    cli
    cld
    mov rdi, rsp
    call gdb_stub_handle_exception
    cmp rax, 0
    je .halt
    add rsp, 16
    call exec_resume_stack_pointer
    mov rsp, rax
    mov ax, GDT_KERNEL_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    sti
    ret
.halt:
    hlt
    jmp .halt

EXCEPTION_NO_ERROR 2
; EXCEPTION_NO_ERROR 3 removed – handled by gdb_stub_exception3_handler
EXCEPTION_NO_ERROR 4
EXCEPTION_NO_ERROR 5
EXCEPTION_NO_ERROR 6
EXCEPTION_NO_ERROR 7
EXCEPTION_WITH_ERROR 8
EXCEPTION_NO_ERROR 9
EXCEPTION_WITH_ERROR 10
EXCEPTION_WITH_ERROR 11
EXCEPTION_WITH_ERROR 12
EXCEPTION_WITH_ERROR 13
EXCEPTION_WITH_ERROR 14
EXCEPTION_NO_ERROR 15
EXCEPTION_NO_ERROR 16
EXCEPTION_WITH_ERROR 17
EXCEPTION_NO_ERROR 18
EXCEPTION_NO_ERROR 19
EXCEPTION_NO_ERROR 20
EXCEPTION_WITH_ERROR 21
EXCEPTION_NO_ERROR 22
EXCEPTION_NO_ERROR 23
EXCEPTION_NO_ERROR 24
EXCEPTION_NO_ERROR 25
EXCEPTION_NO_ERROR 26
EXCEPTION_NO_ERROR 27
EXCEPTION_NO_ERROR 28
EXCEPTION_WITH_ERROR 29
EXCEPTION_WITH_ERROR 30
EXCEPTION_NO_ERROR 31

irq0_interrupt_handler:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
    cld
    call timer_interrupt_dispatch
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    iretq

irq1_interrupt_handler:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
    cld
    call keyboard_interrupt_dispatch_wrapper
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    iretq

irq12_interrupt_handler:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
    cld
    call mouse_interrupt_dispatch_wrapper
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    iretq

%macro GENERIC_IRQ_HANDLER 1
irq%1_interrupt_handler:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
    cld
    mov rdi, %1
    call generic_irq_interrupt_dispatch
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    iretq
%endmacro

GENERIC_IRQ_HANDLER 2
GENERIC_IRQ_HANDLER 3
GENERIC_IRQ_HANDLER 4
GENERIC_IRQ_HANDLER 5
GENERIC_IRQ_HANDLER 6
GENERIC_IRQ_HANDLER 7
GENERIC_IRQ_HANDLER 8
GENERIC_IRQ_HANDLER 9
GENERIC_IRQ_HANDLER 10
GENERIC_IRQ_HANDLER 11
GENERIC_IRQ_HANDLER 13
GENERIC_IRQ_HANDLER 14
GENERIC_IRQ_HANDLER 15

syscall_interrupt_handler:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
    cld
    mov rdi, rsp
    call syscall_interrupt_dispatch
    push rax
    call exec_process_completed
    cmp rax, 0
    pop rax
    je .return_to_user
    call exec_resume_stack_pointer
    mov rsp, rax
    mov ax, GDT_KERNEL_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    sti
    ret
.return_to_user:
    mov [rsp], rax
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    iretq

exec_enter_user_mode:
    cli
    mov [r8], rsp
    mov rsp, rcx
    push qword (GDT_USER_DATA_SELECTOR | 3)
    push rsi
    pushfq
    pop rax
    or rax, 0x200
    push rax
    push qword (GDT_USER_CODE_SELECTOR | 3)
    push rdi
    mov rdi, rdx
    mov ax, (GDT_USER_DATA_SELECTOR | 3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    iretq
