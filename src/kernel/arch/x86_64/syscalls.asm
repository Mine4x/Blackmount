[bits 64]

extern x86_64_Syscall_Dispatch
extern x86_64_Syscall_KernelStack

global x86_64_Syscall_Entry

section .data
align 8
saved_user_rsp: dq 0

section .text

x86_64_Syscall_Entry:

    ; Save user RSP
    mov [rel saved_user_rsp], rsp

    ; Switch to kernel stack
    mov rsp, [rel x86_64_Syscall_KernelStack]

    ; Build IRET frame
    push 0x1B                      ; SS (user data)
    push qword [rel saved_user_rsp]
    push r11                       ; RFLAGS
    push 0x23                      ; CS (user code)
    push rcx                       ; RIP

    ; Save callee-saved registers
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Convert syscall ABI → System V ABI
    push r9                        ; arg6

    mov r9, r8
    mov r8, r10
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax

    call x86_64_Syscall_Dispatch

    ; Remove arg6
    pop r9

    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ; Return to user
    iretq