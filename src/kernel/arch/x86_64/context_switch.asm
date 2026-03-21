; void resume_kernel_context(Registers *ctx)

%define REG_R15       0
%define REG_R14       8
%define REG_R13       16
%define REG_R12       24
%define REG_R11       32
%define REG_R10       40
%define REG_R9        48
%define REG_R8        56
%define REG_RBP       64
%define REG_RDI       72
%define REG_RSI       80
%define REG_RDX       88
%define REG_RCX       96
%define REG_RBX       104
%define REG_RAX       112
%define REG_INTERRUPT 120
%define REG_ERROR     128
%define REG_RIP       136
%define REG_CS        144
%define REG_RFLAGS    152
%define REG_RSP       160
%define REG_SS        168

global resume_kernel_context

resume_kernel_context:
    ; rdi = Registers*

    ; Build the iretq frame on the stack: ss, rsp, rflags, cs, rip
    mov rax, [rdi + REG_SS]
    push rax
    mov rax, [rdi + REG_RSP]
    push rax
    mov rax, [rdi + REG_RFLAGS]
    push rax
    mov rax, [rdi + REG_CS]
    push rax
    mov rax, [rdi + REG_RIP]
    push rax

    ; Restore GPRs (restore rdi last since it holds our struct pointer)
    mov r15, [rdi + REG_R15]
    mov r14, [rdi + REG_R14]
    mov r13, [rdi + REG_R13]
    mov r12, [rdi + REG_R12]
    mov r11, [rdi + REG_R11]
    mov r10, [rdi + REG_R10]
    mov r9,  [rdi + REG_R9]
    mov r8,  [rdi + REG_R8]
    mov rbp, [rdi + REG_RBP]
    mov rsi, [rdi + REG_RSI]
    mov rdx, [rdi + REG_RDX]
    mov rcx, [rdi + REG_RCX]
    mov rbx, [rdi + REG_RBX]
    mov rax, [rdi + REG_RAX]
    mov rdi, [rdi + REG_RDI]

    iretq