[BITS 64]

global syscall_handler_asm
extern syscall_dispatcher

section .text

syscall_handler_asm:
    ; On entry:
    ; RAX = syscall number
    ; RDI,RSI,RDX,R10,R8,R9 = args
    ; RCX = user RIP
    ; R11 = user RFLAGS

    ; Save return context
    push rcx
    push r11

    ; Save registers we will use
    push rax
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9

    ; Pass arguments to C (SysV ABI)
    ; rdi, rsi, rdx, rcx, r8, r9

    mov rdi, [rsp + 6*8]  ; syscall number (saved rax)
    mov rsi, [rsp + 5*8]  ; arg1 (saved rdi)
    mov rdx, [rsp + 4*8]  ; arg2 (saved rsi)
    mov rcx, [rsp + 3*8]  ; arg3 (saved rdx)
    mov r8,  [rsp + 2*8]  ; arg4 (saved r10)
    mov r9,  [rsp + 1*8]  ; arg5 (saved r8)

    call syscall_dispatcher

    ; RAX = return value

    ; Restore registers (except rax which holds return value)
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi
    add rsp, 8      ; discard saved rax

    ; Restore return state
    pop r11
    pop rcx

    sysretq