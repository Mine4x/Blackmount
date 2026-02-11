#ifndef PAGEFAULT_H
#define PAGEFAULT_H

#include "isr.h"

void x86_64_PageFault_Handler(Registers* regs);
void x86_64_DoubleFault_Handler(Registers* regs);
void x86_64_PageFault_Initialize(void);

#endif