; entry.asm - x86_64 Limine kernel entry point

[BITS 64]

section .entry
global start
extern kmain

; Stack size (16KB)
STACK_SIZE equ 16384

start:
    ; Disable interrupts
    cli

    ; Set up stack
    mov rsp, stack_top
    mov rbp, rsp

    ; Clear direction flag (required by System V ABI)
    cld

    ; Clear BSS section (if not already done)
    ; extern __bss_start, __bss_end
    ; mov rdi, __bss_start
    ; mov rcx, __bss_end
    ; sub rcx, rdi
    ; xor al, al
    ; rep stosb

    ; Call the kernel main function
    call kmain

    ; If kmain returns, halt the system
.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb STACK_SIZE
stack_top: