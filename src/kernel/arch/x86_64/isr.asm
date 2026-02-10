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
    ; Save all registers (pusha doesn't exist in 64-bit)
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
    
    ; Save segment registers
    xor rax, rax
    mov ax, ds
    push rax
    mov ax, es
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    
    ; Load kernel data segment
    mov ax, 0x10        ; use kernel data segment
    mov ds, ax
    mov es, ax
    ; Note: fs and gs are typically not modified in x86_64
    
    ; Align stack to 16 bytes before call (required by x86_64 ABI)
    mov rdi, rsp        ; pass pointer to stack as first argument
    and rsp, ~0x0F      ; align to 16 bytes
    
    call x86_64_ISR_Handler
    
    ; Restore original stack pointer (stored in registers struct)
    mov rsp, rdi
    
    ; Restore segment registers
    pop rax
    mov gs, ax
    pop rax
    mov fs, ax
    pop rax
    mov es, ax
    pop rax
    mov ds, ax
    
    ; Restore all registers
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
    
    add rsp, 16         ; remove error code and interrupt number
    iretq               ; 64-bit interrupt return (pops cs, rip, rflags, ss, rsp)