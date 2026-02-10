[bits 64]

global x86_64_outb
x86_64_outb:
    mov dx, di          ; port in DI (first arg)
    mov al, sil         ; value in SIL (second arg, low byte of RSI)
    out dx, al
    ret

global x86_64_inb
x86_64_inb:
    mov dx, di          ; port in DI (first arg)
    xor eax, eax
    in al, dx
    ret

global x86_64_Panic
x86_64_Panic:
    cli
    hlt

global x86_64_EnableInterrupts
x86_64_EnableInterrupts:
    sti
    ret

global x86_64_DisableInterrupts
x86_64_DisableInterrupts:
    cli
    ret

global crash_me
crash_me:
    ; div by 0
    ; mov rcx, 0x1337
    ; mov rax, 0
    ; div rax
    int 0x80
    ret

global x86_64_inw
x86_64_inw:
    mov dx, di          ; port in DI (first arg)
    in ax, dx
    ret

global x86_64_outw
x86_64_outw:
    mov dx, di          ; port in DI (first arg)
    mov ax, si          ; value in SI (second arg)
    out dx, ax
    ret