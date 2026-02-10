; context_switch.asm - Context switching for x86_64
[bits 64]
global context_switch

; void context_switch(Context* old_ctx, Context* new_ctx)
; x86_64 calling convention: RDI = old_ctx, RSI = new_ctx
;
; Context layout (offsets in bytes, 8 bytes per register):
;   0: rax,   8: rbx,  16: rcx,  24: rdx
;  32: rsi,  40: rdi,  48: rsp,  56: rbp
;  64: rip,  72: rflags
;  80: r8,   88: r9,   96: r10, 104: r11
; 112: r12, 120: r13, 128: r14, 136: r15

context_switch:
    push rbp
    mov rbp, rsp
    pushfq                  ; Save flags (64-bit)
    
    ; Save all registers to old context (RDI = old_ctx)
    mov [rdi+8], rbx        ; Save RBX
    mov [rdi+16], rcx       ; Save RCX
    mov [rdi+24], rdx       ; Save RDX
    mov [rdi+32], rsi       ; Save RSI
    ; RDI saved later
    
    ; Save RSP (before we pushed rbp and flags)
    mov rax, rbp
    add rax, 24             ; Skip return address (8), old rbp (8), flags (8)
    mov [rdi+48], rax       ; Save RSP
    
    mov rax, [rbp]          ; Get caller's RBP
    mov [rdi+56], rax       ; Save RBP
    
    ; Save return address as RIP
    mov rax, [rbp+8]        ; Get return address
    mov [rdi+64], rax       ; Save RIP
    
    ; Save RFLAGS
    pop rax                 ; Get flags we pushed earlier
    mov [rdi+72], rax       ; Save RFLAGS
    
    ; Save extended registers
    mov [rdi+80], r8
    mov [rdi+88], r9
    mov [rdi+96], r10
    mov [rdi+104], r11
    mov [rdi+112], r12
    mov [rdi+120], r13
    mov [rdi+128], r14
    mov [rdi+136], r15
    
    ; Save RDI last (since we're using it)
    mov rax, [rbp+16]       ; Get original RDI from stack (first arg)
    mov [rdi+40], rax       ; Save RDI
    
    ; Now restore new context (RSI = new_ctx, but save it first)
    mov rax, rsi            ; Move new_ctx pointer to RAX
    
    ; Restore registers
    mov rbx, [rax+8]        ; Restore RBX
    mov rcx, [rax+16]       ; Restore RCX
    mov rdx, [rax+24]       ; Restore RDX
    mov rsi, [rax+32]       ; Restore RSI
    mov rdi, [rax+40]       ; Restore RDI
    
    ; Restore extended registers
    mov r8,  [rax+80]
    mov r9,  [rax+88]
    mov r10, [rax+96]
    mov r11, [rax+104]
    mov r12, [rax+112]
    mov r13, [rax+120]
    mov r14, [rax+128]
    mov r15, [rax+136]
    
    ; Restore RSP and RBP
    mov rsp, [rax+48]       ; Restore RSP
    mov rbp, [rax+56]       ; Restore RBP
    
    ; Restore RFLAGS
    push qword [rax+72]
    popfq
    
    ; Jump to new RIP
    push qword [rax+64]     ; Push new RIP as return address
    mov rax, [rax+0]        ; Restore RAX last
    ret                     ; Jump to new RIP