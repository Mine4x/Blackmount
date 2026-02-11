OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(start)

PHDRS
{
    text    PT_LOAD    FLAGS((1 << 0) | (1 << 2)) ;
    rodata  PT_LOAD    FLAGS((1 << 2)) ;
    data    PT_LOAD    FLAGS((1 << 1) | (1 << 2)) ;
    dynamic PT_DYNAMIC FLAGS((1 << 1) | (1 << 2)) ;
}

SECTIONS
{
    . = 0xffffffff80000000;
    
    kernel_start = .;
    kernel_text_start = .;
    
    .limine_requests : {
        KEEP(*(.limine_requests_start_marker))
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end_marker))
        KEEP(*(.limine_requests_end))
    } :text
    
    .entry : {
        *(.entry)
    } :text
    
    .text : {
        *(.text .text.*)
    } :text
    
    kernel_text_end = .;
    
    . += CONSTANT(MAXPAGESIZE);
    
    kernel_rodata_start = .;
    .rodata : {
        *(.rodata .rodata.*)
    } :rodata
    kernel_rodata_end = .;
    
    . += CONSTANT(MAXPAGESIZE);
    
    kernel_data_start = .;
    .data : {
        *(.data .data.*)
    } :data
    
    .dynamic : {
        *(.dynamic)
    } :data :dynamic
    
    __bss_start = .;
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data
    __bss_end = .;
    
    kernel_data_end = .;
    
    /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
    }
    
    kernel_end = .;
    __end = .;
}