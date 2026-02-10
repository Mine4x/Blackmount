#pragma once

void halt(void) {
    for (;;) {
        __asm__ volatile("cli");
        __asm__ volatile("hlt");
    }
}