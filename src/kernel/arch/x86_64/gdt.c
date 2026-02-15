#include "gdt.h"
#include <stdint.h>
#include <string.h>

typedef struct
{
    uint16_t LimitLow;                  // limit (bits 0-15)
    uint16_t BaseLow;                   // base (bits 0-15)
    uint8_t BaseMiddle;                 // base (bits 16-23)
    uint8_t Access;                     // access
    uint8_t FlagsLimitHi;               // limit (bits 16-19) | flags
    uint8_t BaseHigh;                   // base (bits 24-31)
} __attribute__((packed)) GDTEntry;

// TSS descriptor in x86_64 is 16 bytes (two GDT entries)
typedef struct
{
    uint16_t LimitLow;
    uint16_t BaseLow;
    uint8_t BaseMiddle;
    uint8_t Access;
    uint8_t FlagsLimitHi;
    uint8_t BaseHigh;
    uint32_t BaseUpper;                 // base (bits 32-63) - only for TSS in 64-bit
    uint32_t Reserved;
} __attribute__((packed)) TSSDescriptor;

typedef struct
{
    uint16_t Limit;                     // sizeof(gdt) - 1
    void* Ptr;                          // address of GDT (64-bit pointer)
} __attribute__((packed)) GDTDescriptor;

// x86_64 TSS structure
typedef struct
{
    uint32_t Reserved0;
    uint64_t RSP0;                      // Kernel stack pointer for privilege level 0
    uint64_t RSP1;                      // Stack pointer for privilege level 1 (unused)
    uint64_t RSP2;                      // Stack pointer for privilege level 2 (unused)
    uint64_t Reserved1;
    uint64_t IST[7];                    // Interrupt Stack Table
    uint64_t Reserved2;
    uint16_t Reserved3;
    uint16_t IOPBOffset;                // I/O Map Base Address
} __attribute__((packed)) TSS;

typedef enum
{
    GDT_ACCESS_CODE_READABLE                = 0x02,
    GDT_ACCESS_DATA_WRITEABLE               = 0x02,
    GDT_ACCESS_CODE_CONFORMING              = 0x04,
    GDT_ACCESS_DATA_DIRECTION_NORMAL        = 0x00,
    GDT_ACCESS_DATA_DIRECTION_DOWN          = 0x04,
    GDT_ACCESS_DATA_SEGMENT                 = 0x10,
    GDT_ACCESS_CODE_SEGMENT                 = 0x18,
    GDT_ACCESS_DESCRIPTOR_TSS               = 0x09,  // TSS (Available)
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

// TSS instance
static TSS g_TSS = {0};

// GDT with space for TSS descriptor (16 bytes = 2 entries)
static struct {
    GDTEntry entries[5];        // NULL, Kernel Code, Kernel Data, User Code, User Data
    TSSDescriptor tss;          // TSS (16 bytes)
} __attribute__((packed)) g_GDT = {
    .entries = {
        // NULL descriptor
        GDT_ENTRY(0, 0, 0, 0),
        
        // Kernel 64-bit code segment (0x08)
        GDT_ENTRY(0,
                  0,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE_SEGMENT | GDT_ACCESS_CODE_READABLE,
                  GDT_FLAG_64BIT | GDT_FLAG_GRANULARITY_4K),
        
        // Kernel 64-bit data segment (0x10)
        GDT_ENTRY(0,
                  0,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_DATA_SEGMENT | GDT_ACCESS_DATA_WRITEABLE,
                  GDT_FLAG_GRANULARITY_4K),
        
        // User 64-bit code segment (0x18)
        GDT_ENTRY(0,
                  0,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODE_SEGMENT | GDT_ACCESS_CODE_READABLE,
                  GDT_FLAG_64BIT | GDT_FLAG_GRANULARITY_4K),
        
        // User 64-bit data segment (0x20)
        GDT_ENTRY(0,
                  0,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_DATA_SEGMENT | GDT_ACCESS_DATA_WRITEABLE,
                  GDT_FLAG_GRANULARITY_4K),
    },
    .tss = {0}  // Will be initialized in x86_64_GDT_Initialize
};

GDTDescriptor g_GDTDescriptor = { sizeof(g_GDT) - 1, &g_GDT };

void x86_64_GDT_Load(GDTDescriptor* descriptor, uint16_t codeSegment, uint16_t dataSegment);

void x86_64_GDT_Initialize()
{
    // Initialize TSS
    memset(&g_TSS, 0, sizeof(TSS));
    g_TSS.IOPBOffset = sizeof(TSS);  // No I/O permission bitmap
    
    // Set up TSS descriptor
    uint64_t tss_base = (uint64_t)&g_TSS;
    uint64_t tss_limit = sizeof(TSS) - 1;
    
    g_GDT.tss.LimitLow = tss_limit & 0xFFFF;
    g_GDT.tss.BaseLow = tss_base & 0xFFFF;
    g_GDT.tss.BaseMiddle = (tss_base >> 16) & 0xFF;
    g_GDT.tss.Access = GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_DESCRIPTOR_TSS;
    g_GDT.tss.FlagsLimitHi = ((tss_limit >> 16) & 0x0F);  // No granularity flag for TSS
    g_GDT.tss.BaseHigh = (tss_base >> 24) & 0xFF;
    g_GDT.tss.BaseUpper = (tss_base >> 32) & 0xFFFFFFFF;
    g_GDT.tss.Reserved = 0;
    
    // Load GDT
    x86_64_GDT_Load(&g_GDTDescriptor, x86_64_GDT_CODE_SEGMENT, x86_64_GDT_DATA_SEGMENT);
    
    // Load TSS
    __asm__ volatile("ltr %0" : : "r"((uint16_t)x86_64_GDT_TSS_SEGMENT));
}

// Set kernel stack for privilege level 0 (used when entering kernel from user mode)
void x86_64_TSS_SetKernelStack(uint64_t stack)
{
    g_TSS.RSP0 = stack;
}