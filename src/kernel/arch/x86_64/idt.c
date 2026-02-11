#include "idt.h"
#include <stdint.h>
#include <util/binary.h>

typedef struct
{
    uint16_t BaseLow;           // Offset bits 0-15
    uint16_t SegmentSelector;   // Code segment selector
    uint8_t IST;                // Interrupt Stack Table (bits 0-2), rest reserved
    uint8_t Flags;              // Type and attributes
    uint16_t BaseMiddle;        // Offset bits 16-31
    uint32_t BaseHigh;          // Offset bits 32-63
    uint32_t Reserved;          // Reserved, must be zero
} __attribute__((packed)) IDTEntry;

typedef struct
{
    uint16_t Limit;
    IDTEntry* Ptr;              // 64-bit pointer
} __attribute__((packed)) IDTDescriptor;

IDTEntry g_IDT[256];

IDTDescriptor g_IDTDescriptor = { sizeof(g_IDT) - 1, g_IDT };

void x86_64_IDT_Load(IDTDescriptor* idtDescriptor);

void x86_64_IDT_SetGate(int interrupt, void* base, uint16_t segmentDescriptor, uint8_t flags)
{
    uint64_t baseAddr = (uint64_t)base;
    
    g_IDT[interrupt].BaseLow = baseAddr & 0xFFFF;
    g_IDT[interrupt].SegmentSelector = segmentDescriptor;
    g_IDT[interrupt].IST = 0;                           // No IST by default
    g_IDT[interrupt].Flags = flags;
    g_IDT[interrupt].BaseMiddle = (baseAddr >> 16) & 0xFFFF;
    g_IDT[interrupt].BaseHigh = (baseAddr >> 32) & 0xFFFFFFFF;
    g_IDT[interrupt].Reserved = 0;
}

void x86_64_IDT_SetGateWithIST(int interrupt, void* base, uint16_t segmentDescriptor, uint8_t flags, uint8_t ist)
{
    uint64_t baseAddr = (uint64_t)base;
    
    g_IDT[interrupt].BaseLow = baseAddr & 0xFFFF;
    g_IDT[interrupt].SegmentSelector = segmentDescriptor;
    g_IDT[interrupt].IST = ist & 0x07;                  // IST is only 3 bits (0-7)
    g_IDT[interrupt].Flags = flags;
    g_IDT[interrupt].BaseMiddle = (baseAddr >> 16) & 0xFFFF;
    g_IDT[interrupt].BaseHigh = (baseAddr >> 32) & 0xFFFFFFFF;
    g_IDT[interrupt].Reserved = 0;
}

void x86_64_IDT_EnableGate(int interrupt)
{
    FLAG_SET(g_IDT[interrupt].Flags, IDT_FLAG_PRESENT);
}

void x86_64_IDT_DisableGate(int interrupt)
{
    FLAG_UNSET(g_IDT[interrupt].Flags, IDT_FLAG_PRESENT);
}

void x86_64_IDT_Initialize()
{
    x86_64_IDT_Load(&g_IDTDescriptor);
}