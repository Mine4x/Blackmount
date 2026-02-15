[bits 64]

; void enter_usermode(uint64_t entry, uint64_t stack);
global enter_usermode
enter_usermode:
    ; RDI = entry point (RIP)
    ; RSI = user stack (RSP)
    
    cli                          ; Disable interrupts during setup
    
    ; Set up stack for iretq
    ; Stack layout (from top to bottom):
    ; SS, RSP, RFLAGS, CS, RIP
    
    mov rax, 0x23                ; User data segment (0x20 | 3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    push 0x23                    ; SS (user data segment with RPL=3)
    push rsi                     ; RSP (user stack)
    pushfq                       ; RFLAGS
    pop rax
    or rax, 0x200                ; Set IF (interrupt flag)
    push rax                     ; RFLAGS with IF set
    push 0x1B                    ; CS (user code segment with RPL=3)
    push rdi                     ; RIP (entry point)
    
    iretq                        ; "Return" to user mode