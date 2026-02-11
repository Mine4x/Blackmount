[BITS 64]

global syscall_handler_asm
extern syscall_dispatcher

section .text

syscall_handler_asm:
    ; Save general-purpose registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; SysV ABI arguments
    ; syscall_dispatcher(rax, rbx, rcx, rdx, rsi, rdi)
    mov rdi, rax
    mov rsi, rbx
    mov rdx, rcx
    mov rcx, rdx
    mov r8,  rsi
    mov r9,  rdi

    call syscall_dispatcher

    ; Return value already in RAX

    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    iretq
