#include "lapic.h"
#include <timer/timer.h>
#include <debug.h>
#include <stdint.h>

#define MSR_APIC_BASE       0x0000001B
#define MSR_X2APIC_ID       0x00000802
#define MSR_X2APIC_VERSION  0x00000803
#define MSR_X2APIC_TPR      0x00000808
#define MSR_X2APIC_EOI      0x0000080B
#define MSR_X2APIC_SVR      0x0000080F   // spurious vector register
#define MSR_X2APIC_ESR      0x00000828   // error status register
#define MSR_X2APIC_ICR      0x00000830   // interrupt command (64-bit in x2APIC)
#define MSR_X2APIC_LVT_TIMER   0x00000832
#define MSR_X2APIC_LVT_LINT0   0x00000835
#define MSR_X2APIC_LVT_LINT1   0x00000836
#define MSR_X2APIC_LVT_ERROR   0x00000837
#define MSR_X2APIC_TIMER_ICR   0x00000838   // initial count
#define MSR_X2APIC_TIMER_CCR   0x00000839   // current count (RO)
#define MSR_X2APIC_TIMER_DCR   0x0000083E   // divide config

#define APIC_BASE_BSP       (1 << 8)   // bootstrap processor flag
#define APIC_BASE_EXTD      (1 << 10)  // x2APIC enable
#define APIC_BASE_EN        (1 << 11)  // xAPIC global enable

#define SVR_APIC_ENABLE     (1 << 8)

#define LVT_MASKED          (1 << 16)
#define LVT_TIMER_PERIODIC  (1 << 17)
#define LVT_TIMER_ONESHOT   (0 << 17)

#define ICR_DELIV_FIXED     (0 << 8)
#define ICR_LEVEL_ASSERT    (1 << 14)
#define ICR_DEST_PHYSICAL   (0 << 11)

#define TIMER_DIVIDE_BY_16  0x3

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    __asm__ volatile("wrmsr" :: "c"(msr), "a"((uint32_t)val),
                                "d"((uint32_t)(val >> 32)));
}

static int cpuid_has_x2apic(void) {
    uint32_t ecx = 0;
    __asm__ volatile(
        "cpuid"
        : "=c"(ecx)
        : "a"(1), "c"(0)
        : "ebx", "edx"
    );
    return (ecx >> 21) & 1;  // ECX bit 21 = x2APIC support
}

static uint32_t lapic_ticks_per_ms = 0;  // calibrated against PIT

#define CALIB_MS 10

static void lapic_calibrate(void) {
    // Set divide config and a large initial count
    wrmsr(MSR_X2APIC_TIMER_DCR, TIMER_DIVIDE_BY_16);
    wrmsr(MSR_X2APIC_TIMER_ICR, 0xFFFFFFFF);

    // Mask the LVT timer so it doesn't fire during calibration
    wrmsr(MSR_X2APIC_LVT_TIMER, LVT_MASKED | LAPIC_TIMER_VECTOR);

    // Let the PIT sleep for CALIB_MS
    timer_sleep_ms(CALIB_MS);

    // Read remaining ticks
    uint32_t remaining = (uint32_t)rdmsr(MSR_X2APIC_TIMER_CCR);

    // Stop timer
    wrmsr(MSR_X2APIC_TIMER_ICR, 0);

    uint32_t elapsed = 0xFFFFFFFF - remaining;
    lapic_ticks_per_ms = elapsed / CALIB_MS;

    log_info("LAPIC", "Calibration: %u ticks/ms (%u ticks in %d ms)",
             lapic_ticks_per_ms, elapsed, CALIB_MS);
}

void lapic_init(void) {
    if (!cpuid_has_x2apic()) {
        log_crit("LAPIC", "x2APIC not supported by CPU");
        return;
    }

    // Enable x2APIC mode: set EN + EXTD in IA32_APIC_BASE
    uint64_t base = rdmsr(MSR_APIC_BASE);
    base |= APIC_BASE_EN | APIC_BASE_EXTD;
    wrmsr(MSR_APIC_BASE, base);
    log_info("LAPIC", "x2APIC enabled, APIC ID: %u", lapic_id());

    // Clear ESR before touching LVT
    wrmsr(MSR_X2APIC_ESR, 0);
    wrmsr(MSR_X2APIC_ESR, 0);

    // Set TPR to 0: accept all interrupts
    wrmsr(MSR_X2APIC_TPR, 0);

    // Mask LINT0/LINT1
    wrmsr(MSR_X2APIC_LVT_LINT0, LVT_MASKED);
    wrmsr(MSR_X2APIC_LVT_LINT1, LVT_MASKED);

    // Set up error LVT
    wrmsr(MSR_X2APIC_LVT_ERROR, LAPIC_ERROR_VECTOR);

    // Enable APIC via spurious vector register (SVR)
    // Bit 8 must be set to actually enable the LAPIC
    wrmsr(MSR_X2APIC_SVR, SVR_APIC_ENABLE | LAPIC_SPURIOUS_VECTOR);
    log_ok("LAPIC", "LAPIC enabled, spurious vector: 0x%x",
           LAPIC_SPURIOUS_VECTOR);

    // Calibrate timer against PIT
    lapic_calibrate();

    log_ok("LAPIC", "Init complete on CPU %u", lapic_id());
}

void lapic_eoi(void) {
    // Writing 0 to EOI MSR signals end-of-interrupt
    wrmsr(MSR_X2APIC_EOI, 0);
}

uint32_t lapic_id(void) {
    return (uint32_t)rdmsr(MSR_X2APIC_ID);
}

void lapic_timer_start(LAPICTimerMode mode, uint32_t ms) {
    if (!lapic_ticks_per_ms) {
        log_warn("LAPIC", "Timer not calibrated, call lapic_init() first");
        return;
    }

    uint32_t count = lapic_ticks_per_ms * ms;

    wrmsr(MSR_X2APIC_TIMER_DCR, TIMER_DIVIDE_BY_16);

    uint32_t lvt = LAPIC_TIMER_VECTOR;
    if (mode == LAPIC_TIMER_PERIODIC)
        lvt |= LVT_TIMER_PERIODIC;

    wrmsr(MSR_X2APIC_LVT_TIMER, lvt);
    wrmsr(MSR_X2APIC_TIMER_ICR, count);

    log_info("LAPIC", "Timer started: %s, %u ms, count=%u",
             mode == LAPIC_TIMER_PERIODIC ? "periodic" : "oneshot",
             ms, count);
}

void lapic_timer_stop(void) {
    wrmsr(MSR_X2APIC_TIMER_ICR, 0);
    wrmsr(MSR_X2APIC_LVT_TIMER, LVT_MASKED | LAPIC_TIMER_VECTOR);
    log_info("LAPIC", "Timer stopped");
}

void lapic_send_ipi(uint32_t dest_apic_id, uint8_t vector) {
    // x2APIC ICR is a single 64-bit MSR write (atomic, no need for two writes)
    // High 32 bits = destination, low 32 bits = delivery info
    uint64_t icr = ((uint64_t)dest_apic_id << 32) |
                   ICR_LEVEL_ASSERT       |
                   ICR_DEST_PHYSICAL      |
                   ICR_DELIV_FIXED        |
                   vector;
    wrmsr(MSR_X2APIC_ICR, icr);
    log_info("LAPIC", "IPI → APIC %u vector 0x%x", dest_apic_id, vector);
}

void lapic_timer_handler(void) {
    lapic_eoi();
}