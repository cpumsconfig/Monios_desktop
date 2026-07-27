with open('tools/build_uefi.py', 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Add wait_for_iso_enter_optional function call after load_font_optional
#    (between "call load_font_optional" and "mov al, 'K'")
old_call_wait = '''    call load_font_optional

    mov rcx, [root_dir]
    mov rax, rcx
    sub rsp, 40
    call qword [rax + FILE_CLOSE]
    add rsp, 40

    mov al, 'K'
    call serial_putc'''

new_call_wait = '''    call load_font_optional

    mov rcx, [root_dir]
    mov rax, rcx
    sub rsp, 40
    call qword [rax + FILE_CLOSE]
    add rsp, 40

    call wait_for_iso_enter_optional

    mov al, 'K'
    call serial_putc'''

content = content.replace(old_call_wait, new_call_wait)

# 2. Add wait_for_iso_enter_optional function before load_font_optional
old_font_func = '''load_font_optional:
    ; Reserve a boot-font region and copy \\MSYH.TTC into it for the kernel.'''

new_font_func = '''wait_for_iso_enter_optional:
    ; Check BOOTWAIT.FLG. If missing, show prompt and wait ~3s via pure TSC.
    ; ReadKeyStroke is avoided - crashes on some UEFI firmware.
    push r12
    push r13
    push r14

    mov rcx, [root_dir]
    test rcx, rcx
    jz .done
    lea rdx, [wait_file]
    lea r8, [wait_flag_name]
    mov r9, EFI_FILE_MODE_READ
    sub rsp, 40
    mov qword [rsp + 32], 0
    mov rax, rcx
    call qword [rax + FILE_OPEN]
    add rsp, 40
    test rax, rax
    jnz .done_flag

    mov rcx, [wait_file]
    mov rax, rcx
    sub rsp, 40
    call qword [rax + FILE_CLOSE]
    add rsp, 40

    mov al, 'W'
    call serial_putc

    mov rcx, [r15 + EFI_SYSTEM_TABLE_CON_OUT]
    test rcx, rcx
    jz .skip_print
    lea rdx, [anykey_prompt]
    mov rax, [rcx]
    sub rsp, 40
    call qword [rax + SIMPLE_TEXT_OUTPUT_STRING]
    add rsp, 40
.skip_print:

    ; TSC deadline = now + ~3s
    mov r14, 8000000000
    rdtsc
    shl rdx, 32
    or rax, rdx
    mov r13, rax            ; r13 = start TSC

.poll_loop:
    rdtsc
    shl rdx, 32
    or rax, rdx
    sub rax, r13            ; elapsed
    cmp rax, r14
    jae .done

    ; Delay: ~20ms at 3GHz via PAUSE loop
    mov r12, 60000000
.delay:
    pause
    dec r12
    jnz .delay
    jmp .poll_loop

.done_flag:
    mov al, 'X'
    call serial_putc
.done:
    mov al, 'G'
    call serial_putc
    pop r14
    pop r13
    pop r12
    ret

load_font_optional:
    ; Reserve a boot-font region and copy \\MSYH.TTC into it for the kernel.'''

content = content.replace(old_font_func, new_font_func)

# 3. Add wait data to data section (after font_name)
old_data_end = '''font_name:
    dw 0x005c, 'M', 'S', 'Y', 'H', '.', 'T', 'T', 'C', 0

align 8'''

new_data_end = '''font_name:
    dw 0x005c, 'M', 'S', 'Y', 'H', '.', 'T', 'T', 'C', 0

wait_flag_name:
    dw 0x005c, 'B', 'O', 'O', 'T', 'W', 'A', 'I', 'T', '.', 'F', 'L', 'G', 0
anykey_prompt:
    db 'Press any key to continue', 13, 10, 0

align 8'''

content = content.replace(old_data_end, new_data_end)

# 4. Add wait_file handle to data section
old_vars = '''font_file:
    dq 0
kernel_addr:'''

new_vars = '''font_file:
    dq 0
wait_file:
    dq 0
kernel_addr:'''

content = content.replace(old_vars, new_vars)

with open('tools/build_uefi.py', 'w', encoding='utf-8') as f:
    f.write(content)

print("done")
