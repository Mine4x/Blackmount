#pragma once
#include <stdint.h>

#define x86_64_GDT_CODE_SEGMENT      0x08
#define x86_64_GDT_DATA_SEGMENT      0x10
#define x86_64_GDT_USER_CODE_SEGMENT 0x18
#define x86_64_GDT_USER_DATA_SEGMENT 0x20
#define x86_64_GDT_TSS_SEGMENT       0x28

void x86_64_GDT_Initialize();
void x86_64_TSS_SetKernelStack(uint64_t stack);