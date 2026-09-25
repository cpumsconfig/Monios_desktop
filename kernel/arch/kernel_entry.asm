[bits 64]
[default rel]

%define GDT_USER_DATA_SELECTOR 0x18
%define GDT_USER_CODE_SELECTOR 0x20
%define GDT_KERNEL_DATA_SELECTOR 0x10

section .bss
align 16
StackSpace resb 65536
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
global syscall_entry
global exec_enter_user_mode
global exec_enter_kernel_mode
global fork_enter_user_mode
extern syscall_fast_dispatch
extern g_syscall_kernel_stack
extern g_syscall_user_rsp
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
extern exec_complete_from_syscall
extern exec_process_completed
extern gdb_stub_handle_exception
extern ftrace_record_entry
extern ftrace_record_exit
extern profiler_timer_tick
extern exec_resume_stack_pointer

_start:
    cli
    ; 调试：输出 '1' 表示进入内核入口
    mov al, '1'
    mov dx, 0x3F8
    out dx, al
    mov rsp, StackTop
    mov rbp, rsp
    ; 调试：输出 '2' 表示栈设置完成
    mov al, '2'
    mov dx, 0x3F8
    out dx, al
    lea rdi, [rel __bss_start]
    lea rcx, [rel __bss_end]
    sub rcx, rdi
    xor eax, eax
    rep stosb
    ; 调试：输出 '3' 表示 BSS 清零完成
    mov al, '3'
    mov dx, 0x3F8
    out dx, al
    mov rdi, r12
    mov rsi, r13
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
    cmp rax, 2
    je .Lpf_resume
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
.Lpf_resume:
    ; Page fault resolved by the demand-pager: drop vector+error code and
    ; iretq straight back to the faulting instruction.
    add rsp, 16
    iretq
.halt:
    hlt
    jmp .halt

EXCEPTION_NO_ERROR 0
; exception1 (#DB / single-step / hw breakpoint) handled below via gdb_stub

%macro GDB_EXCEPTION_ENTRY 1
exception%1_handler:
    cli
    cld
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
    mov rdi, rsp
    mov rsi, %1
    xor edx, edx
    call gdb_stub_handle_exception
    test rax, rax
    jz .L%1_not_handled
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
.L%1_not_handled:
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
    push 0
    push %1
    jmp exception_common
%endmacro

GDB_EXCEPTION_ENTRY 1
GDB_EXCEPTION_ENTRY 3

EXCEPTION_NO_ERROR 2
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
    mov rdi, rsp
    call profiler_timer_tick
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
    test al, al
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

; AMD64 SYSCALL 快速系统调用入口
syscall_entry:
    ; SYSCALL 执行后状态：
    ; RCX = 用户态 RIP（返回地址）
    ; R11 = 用户态 RFLAGS
    ; RSP 仍然指向用户栈（SYSCALL 不自动切换栈）
    ; CS/SS 已切换到内核态
    ; IF 已被清除（SFMASK 控制）

    ; 保存用户栈指针
    mov [rel g_syscall_user_rsp], rsp

    ; 切换到内核栈
    mov rsp, [rel g_syscall_kernel_stack]

    ; 保存所有通用寄存器（与中断处理顺序一致）
    push r15
    push r14
    push r13
    push r12
    push r11          ; R11 = 用户 RFLAGS
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx          ; RCX = 用户 RIP
    push rbx
    push rax

    ; 调用系统调用分发函数
    cld
    mov rdi, rsp
    call syscall_interrupt_dispatch

    ; 保存返回值到 rax 的位置
    mov [rsp], rax

    ; 恢复所有通用寄存器
    pop rax           ; 返回值
    pop rbx
    pop rcx           ; RCX = 用户 RIP（用于 SYSRET）
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11           ; R11 = 用户 RFLAGS（用于 SYSRET）
    pop r12
    pop r13
    pop r14
    pop r15

    ; 切换回用户栈
    mov rsp, [rel g_syscall_user_rsp]

    ; SYSRET 返回用户态
    ; RCX = 用户 RIP, R11 = 用户 RFLAGS, RAX = 返回值
    sysret

exec_enter_user_mode:
    cli
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    lea rax, [rel .resume]
    push rax
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

.resume:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

; fork_enter_user_mode: enter a forked child into user mode at its saved
;   register state, with RAX = 0 (the fork() child return value).
;   rdi = child rip, rsi = child user rsp, rdx = child rflags,
;   rcx = child kernel stack top, r8 = &resume_rsp slot.
fork_enter_user_mode:
    cli
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    lea rax, [rel .resume]
    push rax
    mov [r8], rsp
    mov rsp, rcx
    push qword (GDT_USER_DATA_SELECTOR | 3)
    push rsi              ; user rsp
    push rdx             ; rflags (saved from parent syscall frame)
    push qword (GDT_USER_CODE_SELECTOR | 3)
    push rdi             ; user rip
    xor eax, eax         ; child returns 0 from fork()
    mov ax, (GDT_USER_DATA_SELECTOR | 3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    iretq

.resume:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

exec_enter_kernel_mode:
    cli
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    lea rax, [rel .resume]
    push rax
    mov [rcx], rsp
    mov rsp, rdx
    and rsp, -16
    mov rax, rdi
    mov rdi, rsi
    sti
    call rax
    mov rdi, rax
    call exec_complete_from_syscall
    call exec_resume_stack_pointer
    mov rsp, rax
    sti
    ret

.resume:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret
