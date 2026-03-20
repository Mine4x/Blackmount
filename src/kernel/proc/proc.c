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

    address_space_t *address_space;

    uint8_t  kernel_stack[PROC_STACK_SIZE];
    uint64_t kernel_stack_top;

    uint64_t code_vaddr_start;
    uint64_t code_vaddr_end;
    uint64_t stack_vaddr_base;
    uint64_t stack_vaddr_top;

    uint64_t heap_start;
    uint64_t heap_end;
} PCB;

static PCB      proc_table[MAX_PROCESSES];
static int      current_proc        = -1;
static uint32_t next_pid            = 1;
static int      scheduling_enabled  = 0;
static uint64_t next_user_code_addr = USER_CODE_BASE;

extern void enter_usermode(uint64_t entry, uint64_t stack);
extern void resume_kernel_context(Registers *ctx);

#define VMM_SCRATCH_VA  ((void *)0xFFFFFFFF80F00000ULL)

static inline address_space_t *pcb_space(const PCB *pcb)
{
    return pcb->address_space ? pcb->address_space : vmm_get_kernel_space();
}

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
    if (index < 0) { log_err("PROC", "Unable to get index from pid: %d", pid); return false; }
    return proc_table[index].state == PROC_BLOCKED;
}

void proc_block(int pid)
{
    int index = proc_find_index(pid);
    if (index < 0) { log_err("PROC", "Unable to get index from pid: %d", pid); return; }
    proc_table[index].state = PROC_BLOCKED;
}

void proc_unblock(int pid)
{
    int index = proc_find_index(pid);
    if (index < 0) { log_err("PROC", "Unable to get index from pid: %d", pid); return; }
    proc_table[index].state = PROC_READY;
}

static void proc_kill_children(uint32_t parent_pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        PCB *child = &proc_table[i];

        if (child->state == PROC_UNUSED || child->state == PROC_ZOMBIE)
            continue;

        if (child->proc.PPID != parent_pid)
            continue;

        proc_kill_children(child->proc.PID);

        log_info("PROC", "Killing child PID %d (parent PID %d)",
                 child->proc.PID, parent_pid);

        if (child->address_space) {
            vmm_destroy_address_space(child->address_space);
            child->address_space = NULL;
        }

        memset(child, 0, offsetof(PCB, kernel_stack));
        child->state = PROC_UNUSED;
    }
}

static void proc_terminate(int index, uint64_t exit_code)
{
    PCB *pcb = &proc_table[index];
    int pid  = pcb->proc.PID;

    log_info("PROC", "Terminating PID %d", pid);

    pcb->proc.ExitCode = exit_code;
    pcb->state = PROC_ZOMBIE;

    proc_kill_children(pid);

    int parent_idx = proc_find_index(pcb->proc.PPID);
    if (parent_idx >= 0) {
        PCB *parent = &proc_table[parent_idx];

        if (parent->proc.WaitingFor == pid) {
            parent->proc.WaitingFor = -1;
            proc_unblock(parent->proc.PID);
        }
    }
}

static void proc_reap(int index)
{
    PCB *pcb = &proc_table[index];

    log_info("PROC", "Reaping PID %d", pcb->proc.PID);

    if (pcb->address_space) {
        vmm_destroy_address_space(pcb->address_space);
        pcb->address_space = NULL;
    }

    memset(pcb, 0, offsetof(PCB, kernel_stack));
    pcb->state = PROC_UNUSED;
}

bool proc_is_valid_demand_addr(uint64_t vaddr)
{
    if (current_proc < 0)
        return false;

    PCB *pcb = &proc_table[current_proc];

    if (vaddr >= pcb->stack_vaddr_base && vaddr < pcb->stack_vaddr_top)
        return true;

    uint64_t code_fence = (pcb->code_vaddr_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (vaddr >= pcb->code_vaddr_start && vaddr < code_fence)
        return true;

    if (vaddr >= pcb->heap_start && vaddr < pcb->heap_end)
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

static bool map_user_page(address_space_t *proc_space, uint64_t user_va,
                           const uint8_t *src, size_t copy_len)
{
    address_space_t *kspace = vmm_get_kernel_space();

    void *kptr = vmm_alloc_page(kspace, VMM_SCRATCH_VA, VMM_KERNEL_PAGE);
    if (!kptr) {
        log_err("PROC", "map_user_page: scratch alloc failed (user_va=0x%lx)", user_va);
        return false;
    }

    if (copy_len > 0 && src)
        memcpy(kptr, src, copy_len);
    if (copy_len < PAGE_SIZE)
        memset((uint8_t *)kptr + copy_len, 0, PAGE_SIZE - copy_len);

    void *phys = vmm_get_physical(kspace, VMM_SCRATCH_VA);
    vmm_unmap(kspace, VMM_SCRATCH_VA);
    vmm_invlpg(VMM_SCRATCH_VA);

    if (!vmm_map(proc_space, (void *)user_va, phys, VMM_USER_PAGE)) {
        log_err("PROC", "map_user_page: vmm_map failed (user_va=0x%lx)", user_va);
        return false;
    }

    return true;
}

bool proc_write_to_user(int pid, void *user_dst, const void *src, size_t n)
{
    if (!user_dst || !src || n == 0)
        return false;

    int idx = proc_find_index(pid);
    if (idx < 0) {
        log_err("PROC", "proc_write_to_user: unknown pid %d", pid);
        return false;
    }

    address_space_t *proc_space = proc_table[idx].address_space;
    if (!proc_space) {
        memcpy(user_dst, src, n);
        return true;
    }

    address_space_t *kspace    = vmm_get_kernel_space();
    const uint8_t   *ksrc      = (const uint8_t *)src;
    uint8_t         *udst      = (uint8_t *)user_dst;
    size_t           remaining = n;

    while (remaining > 0) {
        size_t page_off    = (uintptr_t)udst & (PAGE_SIZE - 1);
        size_t chunk       = PAGE_SIZE - page_off;
        if (chunk > remaining)
            chunk = remaining;

        void *page_va = (void *)((uintptr_t)udst & ~(uintptr_t)(PAGE_SIZE - 1));

        void *phys = vmm_get_physical(proc_space, page_va);
        if (!phys) {
            log_err("PROC",
                    "proc_write_to_user: pid %d VA 0x%lx not mapped",
                    pid, (uint64_t)page_va);
            return false;
        }

        if (!vmm_map(kspace, VMM_SCRATCH_VA, phys, VMM_KERNEL_PAGE)) {
            log_err("PROC", "proc_write_to_user: scratch map failed");
            return false;
        }

        memcpy((uint8_t *)VMM_SCRATCH_VA + page_off, ksrc, chunk);

        vmm_unmap(kspace, VMM_SCRATCH_VA);
        vmm_invlpg(VMM_SCRATCH_VA);

        ksrc      += chunk;
        udst      += chunk;
        remaining -= chunk;
    }

    return true;
}

static int user_map_stack(address_space_t *proc_space,
                           uint64_t *out_stack_top,
                           uint64_t *out_stack_base,
                           uint64_t *out_initial_rsp)
{
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) { slot = i; break; }
    }
    if (slot < 0) { log_err("PROC", "No free PCB slot!"); return -1; }

    uint64_t stack_top  = USER_STACK_ARENA + (uint64_t)(slot + 1) * USER_STACK_VSIZE;
    uint64_t stack_base = stack_top - USER_STACK_VSIZE;

    if (stack_top > USER_SPACE_END) {
        log_err("PROC", "Stack region overflows user address space!");
        return -1;
    }

    uint64_t initial_stack_page = (stack_top - PAGE_SIZE) & ~((uint64_t)PAGE_SIZE - 1);

    if (!map_user_page(proc_space, initial_stack_page, NULL, 0)) {
        log_err("PROC", "Failed to map initial stack page!");
        return -1;
    }

    *out_stack_top   = stack_top;
    *out_stack_base  = stack_base;
    *out_initial_rsp = (stack_top - 16) & ~0xFULL;
    return slot;
}

static int proc_create_internal(uint64_t entry, uint32_t priority, uint32_t parent,
                                ProcType type, uint64_t code_start, uint64_t code_end,
                                uint64_t stack_base, uint64_t stack_top,
                                uint64_t initial_rsp, address_space_t *address_space)
{
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) { slot = i; break; }
    }
    if (slot < 0)
        return -1;

    PCB *pcb = &proc_table[slot];
    memset(pcb, 0, sizeof(PCB));

    pcb->proc.PID        = next_pid++;
    pcb->proc.WaitingFor = -1;
    pcb->proc.PPID       = parent;
    pcb->proc.Priority   = priority;
    pcb->proc.Type       = type;
    pcb->state           = PROC_READY;
    pcb->address_space   = address_space;

    uint64_t kstack_top = (uint64_t)(pcb->kernel_stack + PROC_STACK_SIZE) & ~0xFULL;
    pcb->kernel_stack_top = kstack_top;

    memset(&pcb->context, 0, sizeof(Registers));
    pcb->context.rip       = entry;
    pcb->context.rflags    = 0x202;
    pcb->context.interrupt = 0;
    pcb->context.error     = 0;

    if (type == PROC_TYPE_KERNEL) {
        pcb->context.cs  = x86_64_GDT_CODE_SEGMENT;
        pcb->context.ss  = x86_64_GDT_DATA_SEGMENT;
        pcb->context.rsp = kstack_top;
        pcb->code_vaddr_start = 0; pcb->code_vaddr_end  = 0;
        pcb->stack_vaddr_base = 0; pcb->stack_vaddr_top = 0;
    } else {
        pcb->context.cs  = x86_64_GDT_USER_CODE_SEGMENT | 3;
        pcb->context.ss  = x86_64_GDT_USER_DATA_SEGMENT | 3;
        pcb->context.rsp      = initial_rsp;
        pcb->code_vaddr_start = code_start;
        pcb->code_vaddr_end   = code_end;
        pcb->stack_vaddr_base = stack_base;
        pcb->stack_vaddr_top  = stack_top;
        pcb->heap_start = (code_end + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
        pcb->heap_end   = pcb->heap_start;
    }

    return pcb->proc.PID;
}

void proc_yield(void)
{
    if (!scheduling_enabled || current_proc < 0)
        return;
    __asm__ volatile("int $0xEF");
}

void proc_enter_syscall(void) { proc_table[current_proc].proc.Type = PROC_TYPE_KERNEL; }
void proc_exit_syscall(void)  { proc_table[current_proc].proc.Type = PROC_TYPE_USER;   }

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
            vmm_switch_space(pcb_space(&proc_table[i]));
            log_info("PROC", "Starting user task PID %d rip=0x%lx rsp=0x%lx",
                     proc_table[i].proc.PID,
                     proc_table[i].context.rip,
                     proc_table[i].context.rsp);
            enter_usermode(proc_table[i].context.rip, proc_table[i].context.rsp);
            return;
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
                                PROC_TYPE_KERNEL, 0, 0, 0, 0, 0, NULL);
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

    size_t code_size  = (uint64_t)end_marker - (uint64_t)entry;

    if (code_size == 0 || code_size > (USER_CODE_LIMIT - USER_CODE_BASE)) {
        __asm__ volatile("sti");
        log_err("PROC", "Invalid user program size: %zu bytes", code_size);
        return -1;
    }

    size_t   alloc_size   = (code_size + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
    uint64_t user_code_va = next_user_code_addr;

    if (user_code_va + alloc_size >= USER_CODE_LIMIT) {
        __asm__ volatile("sti");
        log_err("PROC", "Out of user code space!");
        return -1;
    }

    address_space_t *kspace = vmm_get_kernel_space();

    for (size_t off = 0; off < alloc_size; off += PAGE_SIZE) {
        void *mapped = vmm_alloc_page(kspace, (void *)(user_code_va + off), VMM_USER_PAGE);
        if (!mapped) {
            __asm__ volatile("sti");
            log_err("PROC", "Failed to map code page at offset %zu", off);
            return -1;
        }
        size_t copy_len = (off < code_size)
            ? ((code_size - off < PAGE_SIZE) ? (code_size - off) : PAGE_SIZE) : 0;
        if (copy_len)
            memcpy((void *)(user_code_va + off), (uint8_t *)entry + off, copy_len);
        if (copy_len < PAGE_SIZE)
            memset((void *)(user_code_va + off + copy_len), 0, PAGE_SIZE - copy_len);
    }

    uint64_t stack_top, stack_base, initial_rsp;
    if (user_map_stack(kspace, &stack_top, &stack_base, &initial_rsp) < 0) {
        __asm__ volatile("sti");
        return -1;
    }

    int pid = proc_create_internal(
        user_code_va, priority, parent, PROC_TYPE_USER,
        user_code_va, user_code_va + code_size,
        stack_base, stack_top, initial_rsp, NULL);

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

int proc_create_user_image(const uint8_t *image, size_t image_size,
                           uint64_t load_vaddr, uint64_t entry_vaddr,
                           uint32_t priority, uint32_t parent)
{
    __asm__ volatile("cli");

    if (!image || image_size == 0) {
        __asm__ volatile("sti");
        log_err("PROC", "proc_create_user_image: null or empty image");
        return -1;
    }

    if (load_vaddr < USER_CODE_BASE || load_vaddr >= USER_CODE_LIMIT) {
        __asm__ volatile("sti");
        log_err("PROC", "load_vaddr 0x%lx outside user code range [0x%lx, 0x%lx)",
                load_vaddr, USER_CODE_BASE, USER_CODE_LIMIT);
        return -1;
    }

    if (entry_vaddr < load_vaddr || entry_vaddr >= load_vaddr + image_size) {
        __asm__ volatile("sti");
        log_err("PROC", "entry_vaddr 0x%lx outside image [0x%lx, 0x%lx)",
                entry_vaddr, load_vaddr, load_vaddr + image_size);
        return -1;
    }

    size_t alloc_size = ((image_size + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1));

    if (load_vaddr + alloc_size > USER_CODE_LIMIT) {
        __asm__ volatile("sti");
        log_err("PROC", "Image overflows USER_CODE_LIMIT");
        return -1;
    }

    address_space_t *proc_space = vmm_create_address_space();
    if (!proc_space) {
        __asm__ volatile("sti");
        log_err("PROC", "Failed to create address space!");
        return -1;
    }

    for (size_t off = 0; off < alloc_size; off += PAGE_SIZE) {
        const uint8_t *src      = (off < image_size) ? (image + off) : NULL;
        size_t         copy_len = (off < image_size)
            ? ((image_size - off < PAGE_SIZE) ? (image_size - off) : PAGE_SIZE)
            : 0;

        if (!map_user_page(proc_space, load_vaddr + off, src, copy_len)) {
            vmm_destroy_address_space(proc_space);
            __asm__ volatile("sti");
            log_err("PROC", "Failed to map code page at offset %zu", off);
            return -1;
        }
    }

    uint64_t stack_top, stack_base, initial_rsp;
    if (user_map_stack(proc_space, &stack_top, &stack_base, &initial_rsp) < 0) {
        vmm_destroy_address_space(proc_space);
        __asm__ volatile("sti");
        return -1;
    }

    int pid = proc_create_internal(
        entry_vaddr, priority, parent, PROC_TYPE_USER,
        load_vaddr, load_vaddr + image_size,
        stack_base, stack_top, initial_rsp,
        proc_space);

    if (pid < 0) {
        vmm_destroy_address_space(proc_space);
        __asm__ volatile("sti");
        log_err("PROC", "Failed to create user task!");
        return -1;
    }

    __asm__ volatile("sti");
    log_ok("PROC",
           "Created user task PID %d | code=[0x%lx,0x%lx) entry=0x%lx stack=[0x%lx,0x%lx)",
           pid, load_vaddr, load_vaddr + image_size,
           entry_vaddr, stack_base, initial_rsp);
    return pid;
}

uint64_t proc_wait_pid(uint64_t pid)
{
    int child_idx = proc_find_index(pid);
    if (child_idx < 0)
        return (uint64_t)-1;

    PCB *child = &proc_table[child_idx];

    if (child->state != PROC_ZOMBIE) {
        int parent_pid = proc_get_current_pid();
        int parent_idx = proc_find_index(parent_pid);
        if (parent_idx < 0)
            return (uint64_t)-1;

        PCB *parent = &proc_table[parent_idx];
        parent->proc.WaitingFor = pid;
        parent->state = PROC_BLOCKED;

        proc_yield();

        child_idx = proc_find_index(pid);
        if (child_idx < 0)
            return (uint64_t)-1;
        child = &proc_table[child_idx];
    }

    uint64_t exit_code = child->proc.ExitCode;
    proc_reap(child_idx);
    return exit_code;
}

void __attribute__((noreturn)) proc_exit(uint64_t exit_code)
{
    __asm__ volatile("cli");

    if (current_proc < 0)
        goto halt;

    int exiting = current_proc;

    proc_terminate(exiting, exit_code);

    address_space_t *old_space = proc_table[exiting].address_space;

    int next = find_next();

    if (next < 0)
        goto halt;

    current_proc = next;
    proc_table[next].state = PROC_RUNNING;

    x86_64_TSS_SetKernelStack(proc_table[next].kernel_stack_top);
    vmm_switch_space(pcb_space(&proc_table[next]));

    if (old_space && old_space != proc_table[next].address_space)
        vmm_destroy_address_space(old_space);

    resume_kernel_context(&proc_table[next].context);

    __builtin_unreachable();

halt:
    if (current_proc >= 0 && proc_table[current_proc].address_space) {
        address_space_t *dying = proc_table[current_proc].address_space;
        vmm_switch_space(vmm_get_kernel_space());
        vmm_destroy_address_space(dying);
    }
    log_info("PROC", "All tasks exited, idling");
    current_proc = -1;
    __asm__ volatile("sti");
    while (1) __asm__ volatile("hlt");
    __builtin_unreachable();
}

void proc_schedule_interrupt(Registers *frame)
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

    x86_64_TSS_SetKernelStack(proc_table[next].kernel_stack_top);

    vmm_switch_space(pcb_space(&proc_table[next]));

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

uint64_t proc_brk(uint64_t new_brk)
{
    if (current_proc < 0)
        return (uint64_t)-1;

    PCB *pcb = &proc_table[current_proc];

    if (new_brk == 0)
        return pcb->heap_end;

    uint64_t limit = pcb->stack_vaddr_base ? pcb->stack_vaddr_base : USER_CODE_LIMIT;

    if (new_brk < pcb->heap_start || new_brk > limit)
        return (uint64_t)-1;

    uint64_t old_end  = pcb->heap_end;
    uint64_t old_page = (old_end  + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t new_page = (new_brk  + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);

    if (new_brk > old_end) {
        for (uint64_t va = old_page; va < new_page; va += PAGE_SIZE) {
            if (pcb->address_space) {
                if (!map_user_page(pcb->address_space, va, NULL, 0))
                    return (uint64_t)-1;
            } else {
                if (!vmm_alloc_page(vmm_get_kernel_space(), (void *)va, VMM_USER_PAGE))
                    return (uint64_t)-1;
            }
        }
    } else if (new_brk < old_end) {
        address_space_t *space = pcb_space(pcb);
        for (uint64_t va = new_page; va < old_page; va += PAGE_SIZE) {
            vmm_unmap(space, (void *)va);
            vmm_invlpg((void *)va);
        }
    }

    pcb->heap_end = new_brk;
    return old_end;
}

int64_t proc_sbrk(int64_t increment)
{
    if (current_proc < 0)
        return (int64_t)-1;

    PCB     *pcb     = &proc_table[current_proc];
    uint64_t old_brk = pcb->heap_end;

    if (increment == 0)
        return (int64_t)old_brk;

    uint64_t new_brk;
    if (increment > 0) {
        new_brk = old_brk + (uint64_t)increment;
        if (new_brk < old_brk)
            return (int64_t)-1;
    } else {
        uint64_t dec = (uint64_t)(-increment);
        if (dec > old_brk - pcb->heap_start)
            return (int64_t)-1;
        new_brk = old_brk - dec;
    }

    if (proc_brk(new_brk) == (uint64_t)-1)
        return (int64_t)-1;

    return (int64_t)old_brk;
}

static bool write_bytes_to_user(address_space_t *proc_space,
                                 uint64_t dst, const void *src, size_t len)
{
    address_space_t *kspace = vmm_get_kernel_space();
    const uint8_t   *s      = (const uint8_t *)src;

    while (len > 0) {
        uint64_t page_va  = dst & ~(uint64_t)(PAGE_SIZE - 1);
        size_t   page_off = (size_t)(dst - page_va);
        size_t   chunk    = PAGE_SIZE - page_off;
        if (chunk > len) chunk = len;

        void *phys = vmm_get_physical(proc_space, (void *)page_va);
        if (!phys) {
            if (!map_user_page(proc_space, page_va, NULL, 0))
                return false;
            phys = vmm_get_physical(proc_space, (void *)page_va);
            if (!phys)
                return false;
        }

        vmm_map(kspace, VMM_SCRATCH_VA, phys, VMM_KERNEL_PAGE);
        memcpy((uint8_t *)VMM_SCRATCH_VA + page_off, s, chunk);
        vmm_unmap(kspace, VMM_SCRATCH_VA);
        vmm_invlpg(VMM_SCRATCH_VA);

        dst += chunk;
        s   += chunk;
        len -= chunk;
    }

    return true;
}

static void push64_to_space(address_space_t *proc_space, uint64_t *sp, uint64_t val)
{
    *sp -= 8;
    write_bytes_to_user(proc_space, *sp, &val, 8);
}

static uint64_t setup_user_stack_argv(address_space_t *proc_space,
                                       uint64_t stack_top,
                                       int argc, const char **argv,
                                       int envc, const char **envp)
{
    uint64_t sp = (stack_top - 16) & ~0xFULL;

    uint64_t *env_ptrs = (uint64_t *)kmalloc((envc + 1) * sizeof(uint64_t));
    uint64_t *arg_ptrs = (uint64_t *)kmalloc((argc + 1) * sizeof(uint64_t));

    if (!env_ptrs || !arg_ptrs) {
        kfree(env_ptrs);
        kfree(arg_ptrs);
        return sp;
    }

    for (int i = envc - 1; i >= 0; i--) {
        size_t len = strlen(envp[i]) + 1;
        sp -= len;
        write_bytes_to_user(proc_space, sp, envp[i], len);
        env_ptrs[i] = sp;
    }

    for (int i = argc - 1; i >= 0; i--) {
        size_t len = strlen(argv[i]) + 1;
        sp -= len;
        write_bytes_to_user(proc_space, sp, argv[i], len);
        arg_ptrs[i] = sp;
    }

    sp &= ~0xFULL;

    push64_to_space(proc_space, &sp, 0);
    for (int i = envc - 1; i >= 0; i--)
        push64_to_space(proc_space, &sp, env_ptrs[i]);

    push64_to_space(proc_space, &sp, 0);
    for (int i = argc - 1; i >= 0; i--)
        push64_to_space(proc_space, &sp, arg_ptrs[i]);

    push64_to_space(proc_space, &sp, (uint64_t)argc);

    kfree(env_ptrs);
    kfree(arg_ptrs);
    return sp;
}

int proc_create_user_image_argv(const uint8_t *image, size_t image_size,
                                 uint64_t load_vaddr, uint64_t entry_vaddr,
                                 uint32_t priority, uint32_t parent,
                                 int argc, const char **argv,
                                 int envc, const char **envp)
{
    __asm__ volatile("cli");

    if (!image || image_size == 0) { __asm__ volatile("sti"); return -1; }

    if (load_vaddr < USER_CODE_BASE || load_vaddr >= USER_CODE_LIMIT) {
        __asm__ volatile("sti"); return -1;
    }

    if (entry_vaddr < load_vaddr || entry_vaddr >= load_vaddr + image_size) {
        __asm__ volatile("sti"); return -1;
    }

    size_t alloc_size = (image_size + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);

    if (load_vaddr + alloc_size > USER_CODE_LIMIT) {
        __asm__ volatile("sti"); return -1;
    }

    address_space_t *proc_space = vmm_create_address_space();
    if (!proc_space) { __asm__ volatile("sti"); return -1; }

    for (size_t off = 0; off < alloc_size; off += PAGE_SIZE) {
        const uint8_t *src      = (off < image_size) ? (image + off) : NULL;
        size_t         copy_len = (off < image_size)
            ? ((image_size - off < PAGE_SIZE) ? (image_size - off) : PAGE_SIZE) : 0;

        if (!map_user_page(proc_space, load_vaddr + off, src, copy_len)) {
            vmm_destroy_address_space(proc_space);
            __asm__ volatile("sti");
            return -1;
        }
    }

    uint64_t stack_top, stack_base, initial_rsp;
    if (user_map_stack(proc_space, &stack_top, &stack_base, &initial_rsp) < 0) {
        vmm_destroy_address_space(proc_space);
        __asm__ volatile("sti");
        return -1;
    }

    initial_rsp = setup_user_stack_argv(proc_space, stack_top,
                                         argc, argv ? argv : (const char **)0,
                                         envc, envp ? envp : (const char **)0);

    int pid = proc_create_internal(
        entry_vaddr, priority, parent, PROC_TYPE_USER,
        load_vaddr, load_vaddr + image_size,
        stack_base, stack_top, initial_rsp,
        proc_space);

    if (pid < 0) {
        vmm_destroy_address_space(proc_space);
        __asm__ volatile("sti");
        return -1;
    }

    __asm__ volatile("sti");
    log_ok("PROC", "Created user task PID %d | entry=0x%lx argc=%d envc=%d",
           pid, entry_vaddr, argc, envc);
    return pid;
}