#include "gdt.h"
#include <stdint.h>

typedef struct
{
    uint16_t LimitLow;                  // limit (bits 0-15)
    uint16_t BaseLow;                   // base (bits 0-15)
    uint8_t BaseMiddle;                 // base (bits 16-23)
    uint8_t Access;                     // access
    uint8_t FlagsLimitHi;               // limit (bits 16-19) | flags
    uint8_t BaseHigh;                   // base (bits 24-31)
} __attribute__((packed)) GDTEntry;

typedef struct
{
    uint16_t Limit;                     // sizeof(gdt) - 1
    GDTEntry* Ptr;                      // address of GDT (64-bit pointer)
} __attribute__((packed)) GDTDescriptor;

typedef enum
{
    GDT_ACCESS_CODE_READABLE                = 0x02,
    GDT_ACCESS_DATA_WRITEABLE               = 0x02,
    GDT_ACCESS_CODE_CONFORMING              = 0x04,
    GDT_ACCESS_DATA_DIRECTION_NORMAL        = 0x00,
    GDT_ACCESS_DATA_DIRECTION_DOWN          = 0x04,
    GDT_ACCESS_DATA_SEGMENT                 = 0x10,
    GDT_ACCESS_CODE_SEGMENT                 = 0x18,
    GDT_ACCESS_DESCRIPTOR_TSS               = 0x00,
    GDT_ACCESS_RING0                        = 0x00,
    GDT_ACCESS_RING1                        = 0x20,
    GDT_ACCESS_RING2                        = 0x40,
    GDT_ACCESS_RING3                        = 0x60,
    GDT_ACCESS_PRESENT                      = 0x80,
} GDT_ACCESS;

typedef enum 
{
    GDT_FLAG_64BIT                          = 0x20,  // L bit (Long mode)
    GDT_FLAG_32BIT                          = 0x40,  // D/B bit
    GDT_FLAG_16BIT                          = 0x00,
    GDT_FLAG_GRANULARITY_1B                 = 0x00,
    GDT_FLAG_GRANULARITY_4K                 = 0x80,
} GDT_FLAGS;

// Helper macros
#define GDT_LIMIT_LOW(limit)                (limit & 0xFFFF)
#define GDT_BASE_LOW(base)                  (base & 0xFFFF)
#define GDT_BASE_MIDDLE(base)               ((base >> 16) & 0xFF)
#define GDT_FLAGS_LIMIT_HI(limit, flags)    (((limit >> 16) & 0xF) | (flags & 0xF0))
#define GDT_BASE_HIGH(base)                 ((base >> 24) & 0xFF)

#define GDT_ENTRY(base, limit, access, flags) {                     \
    GDT_LIMIT_LOW(limit),                                           \
    GDT_BASE_LOW(base),                                             \
    GDT_BASE_MIDDLE(base),                                          \
    access,                                                         \
    GDT_FLAGS_LIMIT_HI(limit, flags),                               \
    GDT_BASE_HIGH(base)                                             \
}

GDTEntry g_GDT[] = {
    // NULL descriptor
    GDT_ENTRY(0, 0, 0, 0),
    
    // Kernel 64-bit code segment
    GDT_ENTRY(0,
              0,  // Base and limit ignored in 64-bit long mode
              GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE_SEGMENT | GDT_ACCESS_CODE_READABLE,
              GDT_FLAG_64BIT | GDT_FLAG_GRANULARITY_4K),
    
    // Kernel 64-bit data segment
    GDT_ENTRY(0,
              0,  // Base and limit ignored in 64-bit long mode
              GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_DATA_SEGMENT | GDT_ACCESS_DATA_WRITEABLE,
              GDT_FLAG_GRANULARITY_4K),  // No 32-bit or 64-bit flag for data segments
};

GDTDescriptor g_GDTDescriptor = { sizeof(g_GDT) - 1, g_GDT };

void x86_64_GDT_Load(GDTDescriptor* descriptor, uint16_t codeSegment, uint16_t dataSegment);

void x86_64_GDT_Initialize()
{
    x86_64_GDT_Load(&g_GDTDescriptor, x86_64_GDT_CODE_SEGMENT, x86_64_GDT_DATA_SEGMENT);
}