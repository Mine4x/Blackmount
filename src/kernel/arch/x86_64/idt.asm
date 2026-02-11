[bits 64]
; void x86_64_IDT_Load(IDTDescriptor* idtDescriptor);
global x86_64_IDT_Load
x86_64_IDT_Load:
    ; make new call frame
    push rbp             ; save old call frame
    mov rbp, rsp         ; initialize new call frame
    
    ; load idt (RDI contains idtDescriptor pointer)
    lidt [rdi]
    
    ; restore old call frame
    mov rsp, rbp
    pop rbp
    ret