#include <arch/x86_64/io.h>
#include <fb/framebuffer.h>
#include <util/rgb.h>
#include <fb/textrenderer.h>
#include <stdio.h>

// For non interrupt panics only(for example mounting root drive fails)

void panic(const char* module, const char* msg) {
    fb_clear(rgb(0, 68, 255));
    tr_set_color(rgb(255, 255, 255), rgb(0, 68, 255));
    printf("  x x \n\n");

    printf(" xxxxx\n");
    printf("x     x\n");

    printf("\nKERNEL PANIC\n");

    printf("A kernel panic got triggerd by %s:\n", module);
    printf("%s\n", msg);

    printf("\nYou should be able to safley reboot now.");

    x86_64_Panic();
}