[BITS 32]

global syscall_handler_asm
extern syscall_dispatcher

section .text

syscall_handler_asm:
    ; Save all registers
    push ds
    push es
    push fs
    push gs
    pushad
    
    ; Load kernel data segment
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Push arguments for syscall_dispatcher in reverse order
    push edi
    push esi
    push edx
    push ecx
    push ebx
    push eax
    
    ; Call C dispatcher
    call syscall_dispatcher
    
    ; Clean up arguments (6 * 4 bytes)
    add esp, 24
    
    ; Store return value in saved EAX position
    mov [esp + 28], eax
    
    ; Restore all registers
    popad
    pop gs
    pop fs
    pop es
    pop ds
    
    ; Return to caller
    iret