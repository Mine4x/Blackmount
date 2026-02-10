[bits 64]
; void x86_64_GDT_Load(GDTDescriptor* descriptor, uint16_t codeSegment, uint16_t dataSegment);
global x86_64_GDT_Load
x86_64_GDT_Load:
    ; make new call frame
    push rbp             ; save old call frame
    mov rbp, rsp         ; initialize new call frame
    
    ; load gdt (RDI = descriptor)
    lgdt [rdi]
    
    ; reload code segment
    ; RSI contains codeSegment (16-bit), RDX contains dataSegment (16-bit)
    push rsi             ; Push code segment selector
    lea rax, [rel .reload_cs]
    push rax             ; Push 64-bit return address
    retfq                ; Far return (64-bit)
    
.reload_cs:
    ; reload data segments
    mov ax, dx           ; RDX contains dataSegment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; restore old call frame
    mov rsp, rbp
    pop rbp
    ret