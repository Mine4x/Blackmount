#include <stdio.h>
#include <stdint.h>

static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, 
                         uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(0));
}

// Get CPU vendor string (e.g., "GenuineIntel", "AuthenticAMD")
void get_cpu_vendor(char *vendor) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, &eax, &ebx, &ecx, &edx);
    
    *(uint32_t *)(vendor + 0) = ebx;
    *(uint32_t *)(vendor + 4) = edx;
    *(uint32_t *)(vendor + 8) = ecx;
    vendor[12] = '\0';
}

// Get CPU brand string (e.g., "Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz")
void get_cpu_brand(char *brand) {
    uint32_t *p = (uint32_t *)brand;
    
    // Check if extended CPUID is available
    uint32_t eax, ebx, ecx, edx;
    cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    
    if (eax < 0x80000004) {
        brand[0] = '\0';
        return;
    }
    
    // Get brand string from leaves 0x80000002-0x80000004
    for (uint32_t i = 0; i < 3; i++) {
        cpuid(0x80000002 + i, p, p + 1, p + 2, p + 3);
        p += 4;
    }
    brand[48] = '\0';
}

// Get processor family, model, and stepping
void get_cpu_signature(uint32_t *family, uint32_t *model, uint32_t *stepping) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    
    *stepping = eax & 0xF;
    *model = (eax >> 4) & 0xF;
    *family = (eax >> 8) & 0xF;
    
    // Extended family and model
    if (*family == 0xF) {
        *family += (eax >> 20) & 0xFF;
    }
    if (*family == 0x6 || *family == 0xF) {
        *model += ((eax >> 16) & 0xF) << 4;
    }
}

void print_cpu_info(void) {
    char vendor[13];
    char brand[49];
    uint32_t family, model, stepping;
    
    get_cpu_vendor(vendor);
    get_cpu_brand(brand);
    get_cpu_signature(&family, &model, &stepping);
    
    // Use your kernel's print function (printf, terminal_write, etc.)
    printf("CPU Vendor: %s\n", vendor);
    printf("CPU Brand: %s\n", brand);
    printf("CPU Family: %u, Model: %u, Stepping: %u\n", family, model, stepping);
}

void osfetch_start() {
    char vendor[13];
    char brand[49];
    uint32_t family, model, stepping;
    
    get_cpu_vendor(vendor);
    get_cpu_brand(brand);
    get_cpu_signature(&family, &model, &stepping);

    logo();
    printf("\n[CPU]\n");
    printf("    Vendor : %s\n", vendor);
    printf("    Brand  : %s\n", brand);
    printf("    Family : %u\n", family);
    printf("    Model  : %u\n", model);
    printf("    Stepping : %u\n", stepping);
}

void logo() {
    printf("\x1b[30;47m");
    printf("                             @   @    @ @                          \n");
    printf(" @@@@@  @@             @@    @   @    @ @                          \n");
    printf(" @   @@ @@  @@@    @@@ @@  @ @          @  @@@   @   @  @ @@@ @@@@ \n");
    printf(" @@@@@  @@     @  @    @@@@  @  @@@@@@@ @ @   @  @   @  @   @  @   \n");
    printf(" @    @ @@ @   @  @    @@ @  @  @     @ @@@   @  @   @  @   @  @   \n");
    printf(" @@@@@  @@ @@@@@  @@@@ @@  @ @@ @     @ @ @@@@   @@@@@  @   @   @@ \n");
    printf("\n\x1b[36;40m");
    printf("                                               @@@  @@   @@@  @    \n");
    printf("                                              @@     @@  @@        \n");
    printf("                                             @@      @@   @@%      \n");
    printf("                                             @@      @@     @@     \n");
    printf("                                             @@     @@       @     \n");
    printf("                                               @@@@@    @@@@@      \n");
}