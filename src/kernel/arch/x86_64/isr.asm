[bits 64]

extern x86_64_ISR_Handler

; cpu pushes to the stack: ss, rsp, rflags, cs, rip
%macro ISR_NOERRORCODE 1
global x86_64_ISR%1
x86_64_ISR%1:
    push 0              ; push dummy error code
    push %1             ; push interrupt number
    jmp isr_common
%endmacro

%macro ISR_ERRORCODE 1
global x86_64_ISR%1
x86_64_ISR%1:
    ; cpu pushes an error code to the stack
    push %1             ; push interrupt number
    jmp isr_common
%endmacro

%include "arch/x86_64/isrs_gen.inc"

isr_common:
    cld

    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rbp
    push rdx
    push rcx
    push rbx
    push rax

    mov rdi, rsp
    sub rsp, 8
    call x86_64_ISR_Handler
    add rsp, 8

    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rbp
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    add rsp, 16        ; error code + interrupt number
    iretq
