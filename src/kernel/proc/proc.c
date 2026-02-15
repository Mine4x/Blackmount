#include "proc.h"
#include <string.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/gdt.h>
#include <memory.h>
#include <mem/vmm.h>
#include <debug.h>

typedef enum {
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    BLOCKED
} ProcState;

typedef struct {
    Proc_t proc;
    ProcState state;
    Registers context;
    
    // Kernel stack - used by all processes for interrupt/exception handling
    uint8_t kernel_stack[PROC_STACK_SIZE];
    uint64_t kernel_stack_top;
    
    // User space info (for user tasks)
    uint64_t user_code_base;    // Where user code starts
    uint64_t user_stack_base;   // Base address of user stack in virtual memory
    uint64_t user_stack_top;    // Top of user stack (initial RSP for user mode)
} PCB;

static PCB proc_table[MAX_PROCESSES];
static int current_proc = -1;
static uint32_t next_pid = 1;
static int scheduling_enabled = 0;
static uint64_t next_user_code_addr = USER_CODE_BASE;

extern void enter_usermode(uint64_t entry, uint64_t stack);

static void idle(void)
{
    while (1)
        __asm__ volatile("hlt");
}

static int find_next(void)
{
    if (current_proc < 0)
        return -1;

    int start = current_proc;
    int i = (start + 1) % MAX_PROCESSES;

    while (i != start) {
        if (proc_table[i].state == PROC_READY)
            return i;
        i = (i + 1) % MAX_PROCESSES;
    }

    if (proc_table[current_proc].state == PROC_RUNNING)
        return current_proc;

    return -1;
}

static int proc_create_internal(uint64_t entry, uint32_t priority, uint32_t parent, ProcType type)
{
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) {
            slot = i;
            break;
        }
    }

    if (slot < 0)
        return -1;

    PCB* pcb = &proc_table[slot];
    memset(pcb, 0, sizeof(PCB));

    pcb->proc.PID = next_pid++;
    pcb->proc.PPID = parent;
    pcb->proc.Priority = priority;
    pcb->proc.Type = type;
    pcb->state = PROC_READY;

    // Set up kernel stack (used by all processes)
    uint64_t kernel_stack_top = (uint64_t)(pcb->kernel_stack + PROC_STACK_SIZE);
    kernel_stack_top &= ~0xFULL;  // 16-byte align
    pcb->kernel_stack_top = kernel_stack_top;

    // Initialize context
    memset(&pcb->context, 0, sizeof(Registers));
    pcb->context.rip = entry;
    pcb->context.rflags = 0x202;  // IF (interrupts enabled)
    pcb->context.interrupt = 0;
    pcb->context.error = 0;

    if (type == PROC_TYPE_KERNEL) {
        // Kernel task - Ring 0
        pcb->context.cs = x86_64_GDT_CODE_SEGMENT;        // 0x08
        pcb->context.ss = x86_64_GDT_DATA_SEGMENT;        // 0x10
        pcb->context.rsp = kernel_stack_top;
        
        // No user space
        pcb->user_code_base = 0;
        pcb->user_stack_base = 0;
        pcb->user_stack_top = 0;
        
    } else {
        // User task - Ring 3
        pcb->context.cs = x86_64_GDT_USER_CODE_SEGMENT | 3;  // 0x1B
        pcb->context.ss = x86_64_GDT_USER_DATA_SEGMENT | 3;  // 0x23
        
        // Entry point is already a user virtual address
        pcb->user_code_base = entry;
        
        // Allocate user stack in user virtual memory
        // Each process gets its own user stack
        pcb->user_stack_base = USER_STACK_BASE - (slot * PROC_STACK_SIZE * 2);
        pcb->user_stack_top = pcb->user_stack_base;
        pcb->user_stack_top &= ~0xFULL;  // 16-byte align
        
        // User mode RSP points to user stack
        pcb->context.rsp = pcb->user_stack_top;
    }

    return pcb->proc.PID;
}

void proc_init(void)
{
    memset(proc_table, 0, sizeof(proc_table));
    current_proc = -1;
    next_pid = 1;
    scheduling_enabled = 0;
    next_user_code_addr = USER_CODE_BASE;

    // Create idle task as kernel task
    proc_create_kernel(idle, 0, 0);
}

void proc_start_scheduling(void)
{
    scheduling_enabled = 1;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_READY) {
            current_proc = i;
            proc_table[i].state = PROC_RUNNING;
            
            // If starting with a user task, set TSS kernel stack
            if (proc_table[i].proc.Type == PROC_TYPE_USER) {
                x86_64_TSS_SetKernelStack(proc_table[i].kernel_stack_top);
            }
            break;
        }
    }
}

int proc_create_kernel(void (*entry)(void), uint32_t priority, uint32_t parent)
{
    return proc_create_internal((uint64_t)entry, priority, parent, PROC_TYPE_KERNEL);
}


int proc_create_user(void (*entry)(void), void (*end_marker)(void), uint32_t priority, uint32_t parent)
{
    __asm__ volatile("cli");
    
    // Validate function pointers first
    if (!entry || !end_marker) {
        __asm__ volatile("sti");
        log_err("PROC", "Invalid function pointers!");
        return -1;
    }
    
    // Calculate size of the user program
    size_t code_size = (uint64_t)end_marker - (uint64_t)entry;
    
    // Validate size
    if (code_size == 0 || code_size > 0x10000) {  // Max 64KB per program
        __asm__ volatile("sti");
        log_err("PROC", "Invalid user program size: %d bytes", code_size);
        return -1;
    }
    
    // Round up to page boundary
    size_t pages_needed = (code_size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t alloc_size = pages_needed * PAGE_SIZE;
    
    // Find next available user space address
    uint64_t user_addr = next_user_code_addr;
    
    // Ensure we have space (don't overlap with user stack region)
    if (user_addr + alloc_size >= USER_STACK_BASE) {
        __asm__ volatile("sti");
        log_err("PROC", "Out of user space!");
        return -1;
    }
    
    // Store values in local variables before logging (stack safety)
    uint64_t safe_addr = user_addr;
    size_t safe_size = code_size;
    
    __asm__ volatile("sti");
    
    log_info("PROC", "Copying %d bytes of user code to 0x%lx", safe_size, safe_addr);
    
    __asm__ volatile("cli");
    
    // Copy code to user space
    memcpy((void*)user_addr, (void*)entry, code_size);
    
    // Zero out the rest of the allocated pages for security
    if (alloc_size > code_size) {
        memset((void*)(user_addr + code_size), 0, alloc_size - code_size);
    }
    
    // Create the user task
    int pid = proc_create_internal(user_addr, priority, parent, PROC_TYPE_USER);
    
    if (pid < 0) {
        __asm__ volatile("sti");
        log_err("PROC", "Failed to create user task!");
        return -1;
    }
    
    // Update next available address
    next_user_code_addr = user_addr + alloc_size;
    
    // Re-enable interrupts
    __asm__ volatile("sti");
    
    log_ok("PROC", "Created user task with PID %d at 0x%lx", pid, safe_addr);
    
    return pid;
}

int proc_load_user_code(int pid, const void* code, size_t size, uint64_t dest_addr)
{
    // Find the process
    PCB* pcb = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].proc.PID == pid && 
            proc_table[i].state != PROC_UNUSED &&
            proc_table[i].proc.Type == PROC_TYPE_USER) {
            pcb = &proc_table[i];
            break;
        }
    }
    
    if (!pcb)
        return -1;
    
    // Validate destination address
    if (dest_addr < USER_CODE_BASE || dest_addr + size >= USER_STACK_BASE) {
        return -1;
    }
    
    // Copy code to user space (pages already mapped by setup_user_space)
    memcpy((void*)dest_addr, code, size);
    
    return 0;
}

void proc_run_user_task(int pid)
{
    // Find the process
    PCB* pcb = NULL;
    int index = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].proc.PID == pid && 
            proc_table[i].state != PROC_UNUSED &&
            proc_table[i].proc.Type == PROC_TYPE_USER) {
            pcb = &proc_table[i];
            index = i;
            break;
        }
    }
    
    if (!pcb) {
        return;
    }
    
    // Set as current running process
    current_proc = index;
    pcb->state = PROC_RUNNING;
    
    // Set TSS kernel stack
    x86_64_TSS_SetKernelStack(pcb->kernel_stack_top);
    
    // Enable scheduling before entering user mode
    scheduling_enabled = 1;
    
    // Jump to user mode (this doesn't return normally, only via interrupt)
    enter_usermode(pcb->context.rip, pcb->context.rsp);
}

void proc_schedule_interrupt(Registers* frame)
{
    if (!scheduling_enabled)
        return;

    int old = current_proc;

    // Save current process context
    if (old >= 0 && proc_table[old].state == PROC_RUNNING) {
        proc_table[old].context = *frame;
        proc_table[old].state = PROC_READY;
    }

    int next = find_next();
    if (next < 0)
        return;

    current_proc = next;
    proc_table[next].state = PROC_RUNNING;

    // If switching to a user task, update TSS kernel stack
    // This tells the CPU where to switch when an interrupt occurs from user mode
    if (proc_table[next].proc.Type == PROC_TYPE_USER) {
        x86_64_TSS_SetKernelStack(proc_table[next].kernel_stack_top);
    }

    // Overwrite interrupt frame to switch context
    *frame = proc_table[next].context;
}

void proc_update_time(uint32_t ticks)
{
    if (current_proc >= 0)
        proc_table[current_proc].proc.CPUTime += ticks;
}

PCB* proc_get_current(void)
{
    if (current_proc < 0 || current_proc >= MAX_PROCESSES)
        return NULL;
    
    if (proc_table[current_proc].state == PROC_UNUSED)
        return NULL;
        
    return &proc_table[current_proc];
}

int proc_get_current_pid(void)
{
    if (current_proc < 0)
        return -1;
    
    return proc_table[current_proc].proc.PID;
}