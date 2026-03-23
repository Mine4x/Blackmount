extern int main(int argc, char **argv, char **envp);

void __attribute__((naked)) _start()
{
    __asm__ volatile (
        "xor  %%rbp, %%rbp\n"
        "pop  %%rdi\n"
        "mov  %%rsp, %%rsi\n"
        "lea  8(%%rsi,%%rdi,8), %%rdx\n"
        "and  $-16, %%rsp\n"
        "call main\n"
        "mov  %%eax, %%edi\n"
        "call exit\n"
        "hlt\n"
        ::: "memory"
    );
}