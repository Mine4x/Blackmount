[bits 64]
global context_switch

context_switch:
    ; RDI = old
    ; RSI = new

    ; Save old context
    mov [rdi + 0], rax
    mov [rdi + 8], rbx
    mov [rdi + 16], rcx
    mov [rdi + 24], rdx
    mov [rdi + 32], rsi
    mov [rdi + 40], rdi
    mov [rdi + 48], rsp
    mov [rdi + 56], rbp

    lea rax, [rel .resume]
    mov [rdi + 64], rax

    pushfq
    pop qword [rdi + 72]

    mov [rdi + 80], r8
    mov [rdi + 88], r9
    mov [rdi + 96], r10
    mov [rdi + 104], r11
    mov [rdi + 112], r12
    mov [rdi + 120], r13
    mov [rdi + 128], r14
    mov [rdi + 136], r15

    ; Load new context
    mov rdx, rsi    ; rdx = new context pointer

    mov rax, [rdx + 0]
    mov rbx, [rdx + 8]
    mov rcx, [rdx + 16]
    mov r8,  [rdx + 80]
    mov r9,  [rdx + 88]
    mov r10, [rdx + 96]
    mov r11, [rdx + 104]
    mov r12, [rdx + 112]
    mov r13, [rdx + 120]
    mov r14, [rdx + 128]
    mov r15, [rdx + 136]

    mov rsi, [rdx + 32]
    mov rdi, [rdx + 40]
    mov rbp, [rdx + 56]
    mov rsp, [rdx + 48]

    push qword [rdx + 72]
    popfq

    jmp qword [rdx + 64]

.resume:
    ret
