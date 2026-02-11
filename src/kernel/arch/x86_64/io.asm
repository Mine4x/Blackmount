[bits 64]

global x86_64_outb
x86_64_outb:
    mov dx, di
    mov al, sil
    out dx, al
    ret

global x86_64_inb
x86_64_inb:
    mov dx, di
    xor eax, eax
    in al, dx
    ret

global x86_64_outw
x86_64_outw:
    mov dx, di
    mov ax, si
    out dx, ax
    ret

global x86_64_inw
x86_64_inw:
    mov dx, di
    in ax, dx
    ret

global x86_64_outl
x86_64_outl:
    mov dx, di
    mov eax, esi
    out dx, eax
    ret

global x86_64_inl
x86_64_inl:
    mov dx, di
    in eax, dx
    ret

global x86_64_EnableInterrupts
x86_64_EnableInterrupts:
    sti
    ret

global x86_64_DisableInterrupts
x86_64_DisableInterrupts:
    cli
    ret

global x86_64_Panic
x86_64_Panic:
    cli
    hlt
    jmp x86_64_Panic