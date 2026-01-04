#ifndef PAGEFAULT_H
#define PAGEFAULT_H

#include "isr.h"

void i686_PageFault_Handler(Registers* regs);
void i686_DoubleFault_Handler(Registers* regs);
void i686_PageFault_Initialize(void);

#endif