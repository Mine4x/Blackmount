#include "e9.h"
#include <arch/x86_64/io.h>

void e9_putc(char c)
{
    x86_64_outb(0xE9, c);
}
