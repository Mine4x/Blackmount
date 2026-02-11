#ifndef LIMINE_REQUESTS_H
#define LIMINE_REQUESTS_H

#include "limine.h"
#include <stdint.h>

void limine_init(void);

extern volatile struct limine_bootloader_info_request bootloader_info_request;
extern volatile struct limine_hhdm_request hhdm_request;
extern volatile struct limine_memmap_request memmap_request;
extern volatile struct limine_framebuffer_request framebuffer_request;
extern volatile struct limine_smp_request smp_request;

extern uint64_t hhdm_offset;
extern struct limine_bootloader_info_response* bootloader_info;
extern struct limine_memmap_response* memmap;
extern struct limine_framebuffer_response* framebuffer;
extern struct limine_smp_response* smp_info;

#endif
