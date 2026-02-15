#include <arch/x86_64/io.h>
#include <stdio.h>
#include <debug.h>
#include <fb/framebuffer.h>
#include <fb/textrenderer.h>
#include <util/rgb.h>

void panic(const char* module, const char* message)
{
    x86_64_DisableInterrupts();

    fb_clear(rgb(0, 120, 215));
    tr_set_color(rgb(255, 255, 255), rgb(0, 120, 215));
    log_crit("PANIC", "Panic triggerd by %s:\n%s\n", module, message);

    printf("Kernel panic triggerd by %s:\n%s\n", module, message);
    x86_64_Panic();
}