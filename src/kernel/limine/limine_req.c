#include "limine_req.h"
#include <fb/framebuffer.h>
#include <fb/textrenderer.h>
#include <debug.h>

__attribute__((used, section(".limine_requests")))
volatile struct limine_bootloader_info_request bootloader_info_request = LIMINE_BOOTLOADER_INFO_REQUEST;

__attribute__((used, section(".limine_requests")))
volatile struct limine_hhdm_request hhdm_request = LIMINE_HHDM_REQUEST;

__attribute__((used, section(".limine_requests")))
volatile struct limine_memmap_request memmap_request = LIMINE_MEMMAP_REQUEST;

__attribute__((used, section(".limine_requests")))
volatile struct limine_framebuffer_request framebuffer_request = LIMINE_FRAMEBUFFER_REQUEST;

__attribute__((used, section(".limine_requests")))
volatile struct limine_smp_request smp_request = LIMINE_SMP_REQUEST;

uint64_t hhdm_offset = 0;
struct limine_bootloader_info_response* bootloader_info = 0;
struct limine_memmap_response* memmap = 0;
struct limine_framebuffer_response* framebuffer = 0;
struct limine_smp_response* smp_info = 0;

void limine_init(void) {

    if (bootloader_info_request.response)
        bootloader_info = bootloader_info_request.response;

    if (hhdm_request.response)
        hhdm_offset = hhdm_request.response->offset;

    if (memmap_request.response)
        memmap = memmap_request.response;

    if (framebuffer_request.response) {
        framebuffer = framebuffer_request.response;

        if (framebuffer->framebuffer_count > 0) {
            fb_init(framebuffer);
            fb_clear(0x000000);
            tr_init(0xFFFFFF, 0x000000);
        } else {
            log_crit("Limine", "UNABLE TO GET FB");
        }
    }

    if (smp_request.response)
        smp_info = smp_request.response;
}