#include <stdint.h>
#include "stdio.h"
#include "memory.h"
#include <hal/hal.h>
#include <arch/i686/irq.h>
#include <debug.h>
#include <heap.h>
#include <fs/fs.h>

extern uint8_t __bss_start;
extern uint8_t __end;

void crash_me();

void timer(Registers* regs)
{
    printf(".");
}

void test() {
    log_debug("FS-test" ,"Reading file: /test/test.txt");

    char buffer[256];
    int bytes_read = read_file("/test/test.txt", buffer, sizeof(buffer));

    if (bytes_read > 0) {
        log_debug("FS-test" ,"Read %d bytes from file\n");

        buffer[bytes_read] = '\0';
        log_ok("FS-test" ,"Read file: %s\n", buffer);
    } else {
        log_err("FS-test", "Couldn't read file or file is empty!\n");
    }
}

void testfs() {
    log_debug("FS-test", "Starting FS Tests");

    create_dir("/test");
    create_dir("/x");

    char testdata[] = "Hello, World!";

    create_file("/x/test.txt");

    write_file("/x/test.txt", testdata, 13);

    delete_dir("/x"); //Will always fail since the dir is not empty

    delete_file("/x/test.txt");
    delete_dir("/x");
    
    create_file("/test/callback.x");
    create_file("/test/test.txt");

    void (*callback)() = &test;

    char data[] = "Hello, World!";

    write_file("/test/test.txt", data, 13);
    set_file_callback("/test/callback.x", callback);

    execute_file("/test/callback.x");

    get_dir_cont("/test");

    log_debug("FS-test", "Cleaning up...");
    
    delete_file("/test/test.txt");
    delete_file("/test/callback.x");

    delete_dir("/test");

}

void __attribute__((section(".entry"))) start(uint16_t bootDrive)
{
    memset(&__bss_start, 0, (&__end) - (&__bss_start));
    log_ok("Boot", "Did memset");

    HAL_Initialize();
    log_ok("Boot", "Initialized HAL");

    init_heap();
    log_ok("Boot", "Initialized Heap");

    init_fs();
    log_ok("Boot", "Initialized RamFS");

    testfs();

    printf("\n\nWelcome to \x1b[30;47mBlackmount\x1b[36;40m OS\n");

    //i686_IRQ_RegisterHandler(0, timer);

    //crash_me();

end:
    for (;;);
}
