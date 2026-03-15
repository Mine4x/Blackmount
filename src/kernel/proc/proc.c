#include "proc.h"
#include <string.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/gdt.h>
#include <memory.h>
#include <mem/vmm.h>
#include <mem/pmm.h>
#include <debug.h>

typedef struct {
    Proc_t    proc;
    ProcState state;
    Registers context;

    uint8_t  kernel_stack[PROC_STACK_SIZE];
    uint64_t kernel_stack_top;

    uint64_t code_vaddr_start;
    uint64_t code_vaddr_end;
    uint64_t stack_vaddr_base;
    uint64_t stack_vaddr_top;
} PCB;

static PCB      proc_table[MAX_PROCESSES];
static int      current_proc        = -1;
static uint32_t next_pid            = 1;
static int      scheduling_enabled  = 0;
static uint64_t next_user_code_addr = USER_CODE_BASE;
static int      should_save         = -1;

extern void enter_usermode(uint64_t entry, uint64_t stack);

static int proc_find_index(int pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (proc_table[i].proc.PID == pid)
            return i;
    return -1;
}

bool proc_is_blocked(int pid)
{
    int index = proc_find_index(pid);
    if (index < 0) {
        log_err("PROC", "Unable to get index from pid: %d", pid);
        return false;
    }
    return proc_table[index].state == PROC_BLOCKED;
}

void proc_block(int pid)
{
    int index = proc_find_index(pid);
    if (index < 0) {
        log_err("PROC", "Unable to get index from pid: %d", pid);
        return;
    }
    proc_table[index].state = PROC_BLOCKED;
}

void proc_unblock(int pid)
{
    int index = proc_find_index(pid);
    if (index < 0) {
        log_err("PROC", "Unable to get index from pid: %d", pid);
        return;
    }
    proc_table[index].state = PROC_READY;
}

bool proc_is_valid_demand_addr(uint64_t vaddr)
{
    if (current_proc < 0)
        return false;

    PCB* pcb = &proc_table[current_proc];

    if (vaddr >= pcb->stack_vaddr_base && vaddr < pcb->stack_vaddr_top)
        return true;

    uint64_t code_fence = (pcb->code_vaddr_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (vaddr >= pcb->code_vaddr_start && vaddr < code_fence)
        return true;

    return false;
}

static void idle(void)
{
    while (1)
        __asm__ volatile("hlt");
}

static int find_next(void)
{
    int start = current_proc;
    for (int i = 1; i <= MAX_PROCESSES; i++) {
        int idx = (start + i) % MAX_PROCESSES;
        if (proc_table[idx].state == PROC_READY)
            return idx;
    }
    return -1;
}

static int proc_create_internal(uint64_t entry, uint32_t priority, uint32_t parent,
                                ProcType type, uint64_t code_start, uint64_t code_end,
                                uint64_t stack_base, uint64_t stack_top)
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

    pcb->proc.PID      = next_pid++;
    pcb->proc.PPID     = parent;
    pcb->proc.Priority = priority;
    pcb->proc.Type     = type;
    pcb->state         = PROC_READY;

    uint64_t kstack_top = (uint64_t)(pcb->kernel_stack + PROC_STACK_SIZE) & ~0xFULL;
    pcb->kernel_stack_top = kstack_top;

    memset(&pcb->context, 0, sizeof(Registers));
    pcb->context.rip    = entry;
    pcb->context.rflags = 0x202;
    pcb->context.interrupt = 0;
    pcb->context.error     = 0;

    if (type == PROC_TYPE_KERNEL) {
        pcb->context.cs  = x86_64_GDT_CODE_SEGMENT;
        pcb->context.ss  = x86_64_GDT_DATA_SEGMENT;
        pcb->context.rsp = kstack_top;
        pcb->code_vaddr_start = 0;
        pcb->code_vaddr_end   = 0;
        pcb->stack_vaddr_base = 0;
        pcb->stack_vaddr_top  = 0;
    } else {
        pcb->context.cs  = x86_64_GDT_USER_CODE_SEGMENT | 3;
        pcb->context.ss  = x86_64_GDT_USER_DATA_SEGMENT | 3;
        pcb->context.rsp = stack_top;
        pcb->code_vaddr_start = code_start;
        pcb->code_vaddr_end   = code_end;
        pcb->stack_vaddr_base = stack_base;
        pcb->stack_vaddr_top  = stack_top;
    }

    return pcb->proc.PID;
}

void proc_yield(void)
{
    if (!scheduling_enabled || current_proc < 0)
        return;
    should_save = 1;
    __asm__ volatile("int $0xEF");
}

void proc_enter_syscall(void)
{
    proc_table[current_proc].proc.Type = PROC_TYPE_KERNEL;
}

void proc_exit_syscall(void)
{
    proc_table[current_proc].proc.Type = PROC_TYPE_USER;
}

void proc_init(void)
{
    memset(proc_table, 0, sizeof(proc_table));
    current_proc        = -1;
    next_pid            = 1;
    scheduling_enabled  = 0;
    next_user_code_addr = USER_CODE_BASE;

    proc_create_kernel(idle, 0, 0);
}

void proc_start_scheduling(void)
{
    scheduling_enabled = 1;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_READY &&
            proc_table[i].proc.Type == PROC_TYPE_USER) {
            current_proc = i;
            proc_table[i].state = PROC_RUNNING;
            x86_64_TSS_SetKernelStack(proc_table[i].kernel_stack_top);
            log_info("PROC", "Starting user task PID %d rip=0x%lx rsp=0x%lx",
                     proc_table[i].proc.PID,
                     proc_table[i].context.rip,
                     proc_table[i].context.rsp);
            enter_usermode(proc_table[i].context.rip, proc_table[i].context.rsp);
        }
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_READY) {
            current_proc = i;
            proc_table[i].state = PROC_RUNNING;
            break;
        }
    }
}

int proc_create_kernel(void (*entry)(void), uint32_t priority, uint32_t parent)
{
    return proc_create_internal((uint64_t)entry, priority, parent,
                                PROC_TYPE_KERNEL, 0, 0, 0, 0);
}

int proc_create_user(void (*entry)(void), void (*end_marker)(void),
                     uint32_t priority, uint32_t parent)
{
    __asm__ volatile("cli");

    if (!entry || !end_marker) {
        __asm__ volatile("sti");
        log_err("PROC", "Invalid function pointers!");
        return -1;
    }

    size_t code_size = (uint64_t)end_marker - (uint64_t)entry;

    if (code_size == 0 || code_size > (USER_CODE_LIMIT - USER_CODE_BASE)) {
        __asm__ volatile("sti");
        log_err("PROC", "Invalid user program size: %zu bytes", code_size);
        return -1;
    }

    size_t   pages_needed = (code_size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t   alloc_size   = pages_needed * PAGE_SIZE;
    uint64_t user_code_va = next_user_code_addr;

    if (user_code_va + alloc_size >= USER_CODE_LIMIT) {
        __asm__ volatile("sti");
        log_err("PROC", "Out of code space!");
        return -1;
    }

    address_space_t* kspace = vmm_get_kernel_space();

    for (size_t off = 0; off < alloc_size; off += PAGE_SIZE) {
        void* mapped = vmm_alloc_page(kspace, (void*)(user_code_va + off), VMM_USER_PAGE);
        if (!mapped) {
            __asm__ volatile("sti");
            log_err("PROC", "Failed to map code page at offset %zu", off);
            return -1;
        }

        size_t copy_len = (off < code_size)
                        ? ((code_size - off < PAGE_SIZE) ? (code_size - off) : PAGE_SIZE)
                        : 0;

        if (copy_len)
            memcpy((void*)(user_code_va + off), (uint8_t*)entry + off, copy_len);

        if (copy_len < PAGE_SIZE)
            memset((void*)(user_code_va + off + copy_len), 0, PAGE_SIZE - copy_len);
    }

    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        __asm__ volatile("sti");
        log_err("PROC", "No free PCB slot!");
        return -1;
    }

    uint64_t stack_top  = USER_STACK_ARENA + (uint64_t)(slot + 1) * USER_STACK_VSIZE;
    uint64_t stack_base = stack_top - USER_STACK_VSIZE;

    if (stack_top > USER_SPACE_END) {
        __asm__ volatile("sti");
        log_err("PROC", "Stack region overflows user address space!");
        return -1;
    }

    uint64_t initial_stack_page = (stack_top - PAGE_SIZE) & ~((uint64_t)PAGE_SIZE - 1);
    void* mapped = vmm_alloc_page(kspace, (void*)initial_stack_page, VMM_USER_PAGE);
    if (!mapped) {
        __asm__ volatile("sti");
        log_err("PROC", "Failed to map initial stack page!");
        return -1;
    }
    memset((void*)initial_stack_page, 0, PAGE_SIZE);

    uint64_t initial_rsp = (stack_top - 16) & ~0xFULL;

    int pid = proc_create_internal(
        user_code_va, priority, parent, PROC_TYPE_USER,
        user_code_va, user_code_va + code_size,
        stack_base, initial_rsp
    );

    if (pid < 0) {
        __asm__ volatile("sti");
        log_err("PROC", "Failed to create user task!");
        return -1;
    }

    next_user_code_addr = user_code_va + alloc_size;

    __asm__ volatile("sti");
    log_ok("PROC", "Created user task PID %d | code=[0x%lx,0x%lx) stack=[0x%lx,0x%lx)",
           pid, user_code_va, user_code_va + code_size, stack_base, initial_rsp);
    return pid;
}

void __attribute__((noreturn)) proc_exit(uint64_t exit_code)
{
    if (current_proc < 0)
        goto halt;

    int exiting = current_proc;
    int pid = proc_table[exiting].proc.PID;
    log_info("PROC", "Process PID %d exiting", pid);
    proc_table[exiting].state = PROC_ZOMBIE;

    int next = -1;
    for (int i = 1; i <= MAX_PROCESSES; i++) {
        int candidate = (exiting + i) % MAX_PROCESSES;
        if (proc_table[candidate].state == PROC_READY &&
            proc_table[candidate].state != PROC_BLOCKED) {
            next = candidate;
            break;
        }
    }

    memset(&proc_table[exiting], 0, sizeof(PCB));

    log_info("PROC", "Process %d exited with status %d", pid, exit_code);
    printf("Process %d exited with status %d\n", pid, exit_code);

    if (next < 0)
        goto halt;

    current_proc = next;
    proc_table[next].state = PROC_RUNNING;

    if (proc_table[next].proc.Type == PROC_TYPE_USER) {
        x86_64_TSS_SetKernelStack(proc_table[next].kernel_stack_top);
        enter_usermode(proc_table[next].context.rip, proc_table[next].context.rsp);
    }

halt:
    log_info("PROC", "All user tasks exited, idling");
    current_proc = next;
    __asm__ volatile("sti");
    while (1) __asm__ volatile("hlt");

    __builtin_unreachable();
}

void proc_schedule_interrupt(Registers* frame)
{
    if (!scheduling_enabled)
        return;

    int old = current_proc;
    if (old >= 0 && proc_table[old].state != PROC_UNUSED) {
        proc_table[old].context = *frame;
        if (proc_table[old].state == PROC_RUNNING)
            proc_table[old].state = PROC_READY;
    }

    int next = find_next();
    if (next < 0)
        return;

    current_proc = next;
    proc_table[next].state = PROC_RUNNING;
    if (proc_table[next].proc.Type == PROC_TYPE_USER)
        x86_64_TSS_SetKernelStack(proc_table[next].kernel_stack_top);
    *frame = proc_table[next].context;
}

void proc_update_time(uint32_t ticks)
{
    if (current_proc >= 0)
        proc_table[current_proc].proc.CPUTime += ticks;
}

int proc_get_current_pid(void)
{
    if (current_proc < 0)
        return -1;
    return proc_table[current_proc].proc.PID;
}