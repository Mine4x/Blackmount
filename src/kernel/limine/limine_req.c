#include "limine_req.h"
#include <fb/font/fontloader.h>

#include "limine.h"
#include <stdint.h>
#include <string.h>

#include <fb/framebuffer.h>
#include <fb/textrenderer.h>
#include <debug.h>

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_requests_start[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
volatile struct limine_bootloader_info_request bootloader_info_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST_ID,
    .revision = 0,
    .response = NULL,
    .flags = 0
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0,
    .response = NULL,
    .internal_module_count = 0,
    .internal_modules = NULL
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0,
    .response = NULL
};


__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_requests_end[] =
    LIMINE_REQUESTS_END_MARKER;


uint64_t hhdm_offset = 0;

struct limine_bootloader_info_response* bootloader_info = NULL;
struct limine_memmap_response* memmap = NULL;
struct limine_framebuffer_response* framebuffer = NULL;
struct limine_mp_response* mp_info = NULL;
struct limine_module_response* modules = NULL;
struct limine_rsdp_response* rsdp_res = NULL;

void* rsdp = NULL;

void limine_init(void) {

    if (bootloader_info_request.response) {
        bootloader_info = bootloader_info_request.response;
    }

    if (rsdp_request.response) {
        rsdp_res = rsdp_request.response;
        rsdp = rsdp_res->address;
    }

    if (hhdm_request.response) {
        hhdm_offset = hhdm_request.response->offset;
    }

    if (memmap_request.response) {
        memmap = memmap_request.response;
    }

    if (mp_request.response) {
        mp_info = mp_request.response;
    }

    if (module_request.response) {
        modules = module_request.response;

        log_info("Limine", "Modules detected");
        log_info("Limine", "Module count: %d", modules->module_count);
    }

    if (framebuffer_request.response &&
        framebuffer_request.response->framebuffer_count > 0) {

        framebuffer = framebuffer_request.response;
        log_ok("Limine", "Got Framebuffer");
    } else {
        log_crit("Limine", "UNABLE TO GET FRAMEBUFFER");
    }
}

struct limine_framebuffer_response* limine_get_fb() {
    return framebuffer;
}

void* limine_get_module(const char* name, uint64_t* out_size) {

    if (!modules)
        return NULL;

    for (uint64_t i = 0; i < modules->module_count; i++) {
        struct limine_file* mod = modules->modules[i];

        log_info("Module", mod->path);

        if (strstr(mod->path, name)) {
            if (out_size)
                *out_size = mod->size;

            return mod->address;
        }
    }

    return NULL;
}
