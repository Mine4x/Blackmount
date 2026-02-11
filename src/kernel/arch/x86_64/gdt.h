#pragma once

#define x86_64_GDT_CODE_SEGMENT 0x08
#define x86_64_GDT_DATA_SEGMENT 0x10

void x86_64_GDT_Initialize();