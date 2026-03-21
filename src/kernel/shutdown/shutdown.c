#include <drivers/acpi/acpi.h>
#include <hal/vfs.h>
#include <stdio.h>
#include <arch/x86_64/io.h>

void prep_shutdown()
{
    x86_64_DisableInterrupts();
    VFS_Unmount();
}

void shutdown()
{
    prep_shutdown();
    printf("About to perform shutdown");

    acpi_debug_shutdown_info();
    acpi_shutdown();
}