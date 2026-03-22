#include "proc.h"
#include <user/user.h>
#include <string.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/gdt.h>
#include <memory.h>
#include <mem/vmm.h>
#include <mem/pmm.h>
#include <heap.h>
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

    int  vfork_parent;
    char cwd[256];
} PCB;

static PCB       proc_table[MAX_PROCESSES];
static int       current_proc           = -1;
static uint32_t  next_pid               = 1;
static int       scheduling_enabled     = 0;
static uint64_t  next_user_code_addr    = USER_CODE_BASE;
static Registers *current_syscall_frame = NULL;

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
        if (proc_table[i].proc.PID == (uint32_t)pid)
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

    pcb->proc.ExitCode = (uint32_t)exit_code;
    pcb->state = PROC_ZOMBIE;

    proc_kill_children(pid);

    if (pcb->vfork_parent > 0) {
        int vidx = proc_find_index(pcb->vfork_parent);
        if (vidx >= 0 && proc_table[vidx].state == PROC_BLOCKED) {
            proc_table[vidx].proc.WaitingFor = (uint32_t)-1;
            proc_table[vidx].state = PROC_READY;
        }
        pcb->address_space = NULL;
        pcb->vfork_parent  = -1;
    }

    int parent_idx = proc_find_index(pcb->proc.PPID);
    if (parent_idx >= 0) {
        PCB *parent = &proc_table[parent_idx];
        if (parent->proc.WaitingFor == (uint32_t)pid) {
            parent->proc.WaitingFor = (uint32_t)-1;
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
        size_t page_off = (uintptr_t)udst & (PAGE_SIZE - 1);
        size_t chunk    = PAGE_SIZE - page_off;
        if (chunk > remaining)
            chunk = remaining;

        void *page_va = (void *)((uintptr_t)udst & ~(uintptr_t)(PAGE_SIZE - 1));

        void *phys = vmm_get_physical(proc_space, page_va);
        if (!phys) {
            log_err("PROC", "proc_write_to_user: pid %d VA 0x%lx not mapped",
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

    uint64_t first_stack_page = (stack_top - 8 * PAGE_SIZE) & ~((uint64_t)PAGE_SIZE - 1);

    for (int p = 0; p < 8; p++) {
        uint64_t page_va = first_stack_page + (uint64_t)p * PAGE_SIZE;
        if (!map_user_page(proc_space, page_va, NULL, 0)) {
            log_err("PROC", "Failed to map initial stack page %d!", p);
            return -1;
        }
    }

    *out_stack_top   = stack_top;
    *out_stack_base  = stack_base;
    *out_initial_rsp = (stack_top - 16) & ~0xFULL;
    return slot;
}

static int proc_create_internal(uint64_t entry, uint32_t priority, uint32_t parent,
                                ProcType type, uint64_t code_start, uint64_t code_end,
                                uint64_t stack_base, uint64_t stack_top,
                                uint64_t initial_rsp, address_space_t *address_space,
                                uid_t owner)
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
    pcb->proc.WaitingFor = (uint32_t)-1;
    pcb->proc.PPID       = parent;
    pcb->proc.Priority   = priority;
    pcb->proc.Type       = type;
    pcb->proc.Owner      = owner;
    pcb->proc.EUID       = owner;
    pcb->proc.SavedUID   = owner;
    pcb->proc.Group      = user_get_gid(owner);
    pcb->proc.EGroup     = pcb->proc.Group;
    pcb->proc.SavedGID   = pcb->proc.Group;
    pcb->state           = PROC_READY;
    pcb->address_space   = address_space;
    pcb->vfork_parent    = -1;
    pcb->cwd[0] = '/';
    pcb->cwd[1] = '\0';

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

    return (int)pcb->proc.PID;
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
    current_proc            = -1;
    next_pid                = 1;
    scheduling_enabled      = 0;
    next_user_code_addr     = USER_CODE_BASE;
    current_syscall_frame   = NULL;
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
                                PROC_TYPE_KERNEL, 0, 0, 0, 0, 0, NULL, UID_ROOT);
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
        stack_base, stack_top, initial_rsp, NULL, UID_ROOT);

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

static uint64_t setup_user_stack_minimal(address_space_t *proc_space, uint64_t stack_top)
{
    uint64_t sp = (stack_top - 16) & ~0xFULL;

    uint8_t rand_bytes[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    sp -= 16;
    uint64_t rand_ptr = sp;
    write_bytes_to_user(proc_space, sp, rand_bytes, 16);

    sp -= 5;
    uint64_t name_ptr = sp;
    write_bytes_to_user(proc_space, sp, "prog", 5);

    sp &= ~0xFULL;

    push64_to_space(proc_space, &sp, 0);
    push64_to_space(proc_space, &sp, 0);
    push64_to_space(proc_space, &sp, rand_ptr);
    push64_to_space(proc_space, &sp, 25);
    push64_to_space(proc_space, &sp, 0x1000);
    push64_to_space(proc_space, &sp, 6);

    push64_to_space(proc_space, &sp, 0);

    push64_to_space(proc_space, &sp, 0);
    push64_to_space(proc_space, &sp, name_ptr);

    push64_to_space(proc_space, &sp, 1);

    return sp;
}

static uint64_t setup_user_stack_argv(address_space_t *proc_space,
                                       uint64_t stack_top,
                                       int argc, const char **argv,
                                       int envc, const char **envp)
{
    uint64_t sp = (stack_top - 16) & ~0xFULL;

    uint8_t rand_bytes[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    sp -= 16;
    uint64_t rand_ptr = sp;
    write_bytes_to_user(proc_space, sp, rand_bytes, 16);

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
    push64_to_space(proc_space, &sp, 0);
    push64_to_space(proc_space, &sp, rand_ptr);
    push64_to_space(proc_space, &sp, 25);
    push64_to_space(proc_space, &sp, 0x1000);
    push64_to_space(proc_space, &sp, 6);

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
            ? ((image_size - off < PAGE_SIZE) ? (image_size - off) : PAGE_SIZE) : 0;

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

    initial_rsp = setup_user_stack_minimal(proc_space, stack_top);

    int pid = proc_create_internal(
        entry_vaddr, priority, parent, PROC_TYPE_USER,
        load_vaddr, load_vaddr + image_size,
        stack_base, stack_top, initial_rsp,
        proc_space, UID_ROOT);

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
    int child_idx = proc_find_index((int)pid);
    if (child_idx < 0)
        return (uint64_t)-1;

    PCB *child = &proc_table[child_idx];

    if (child->state != PROC_ZOMBIE) {
        int parent_pid = proc_get_current_pid();
        int parent_idx = proc_find_index(parent_pid);
        if (parent_idx < 0)
            return (uint64_t)-1;

        PCB *parent = &proc_table[parent_idx];
        parent->proc.WaitingFor = (uint32_t)pid;
        parent->state = PROC_BLOCKED;

        proc_yield();

        child_idx = proc_find_index((int)pid);
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
    return (int)proc_table[current_proc].proc.PID;
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
    uint64_t old_page = (old_end + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t new_page = (new_brk + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);

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
        proc_space, UID_ROOT);

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

static bool proc_can_act(uid_t actor, int pid)
{
    if (!user_exists(actor))
        return false;

    int idx = proc_find_index(pid);
    if (idx < 0)
        return false;

    uid_t owner = proc_table[idx].proc.Owner;

    if (actor == owner)
        return true;

    return user_get_perm_level(actor) < user_get_perm_level(owner);
}

uid_t proc_get_owner(int pid)
{
    int idx = proc_find_index(pid);
    if (idx < 0)
        return -1;
    return proc_table[idx].proc.Owner;
}

int proc_set_owner(int pid, uid_t new_owner)
{
    if (!user_exists(new_owner))
        return -1;
    int idx = proc_find_index(pid);
    if (idx < 0)
        return -1;
    proc_table[idx].proc.Owner = new_owner;
    return 0;
}

int proc_kill_as(uid_t actor, int pid)
{
    if (!proc_can_act(actor, pid))
        return -1;

    int idx = proc_find_index(pid);
    if (idx < 0)
        return -1;

    __asm__ volatile("cli");
    proc_terminate(idx, 1);
    __asm__ volatile("sti");
    return 0;
}

int proc_block_as(uid_t actor, int pid)
{
    if (!proc_can_act(actor, pid))
        return -1;
    proc_block(pid);
    return 0;
}

int proc_unblock_as(uid_t actor, int pid)
{
    if (!proc_can_act(actor, pid))
        return -1;
    proc_unblock(pid);
    return 0;
}

uint64_t proc_wait_pid_as(uid_t actor, uint64_t pid)
{
    if (!proc_can_act(actor, (int)pid))
        return (uint64_t)-1;
    return proc_wait_pid(pid);
}

int proc_create_kernel_as(uid_t owner, void (*entry)(void),
                          uint32_t priority, uint32_t parent)
{
    if (!user_exists(owner))
        return -1;
    return proc_create_internal((uint64_t)entry, priority, parent,
                                PROC_TYPE_KERNEL, 0, 0, 0, 0, 0, NULL, owner);
}

int proc_create_user_as(uid_t owner, void (*entry)(void), void (*end_marker)(void),
                        uint32_t priority, uint32_t parent)
{
    if (!user_exists(owner))
        return -1;

    __asm__ volatile("cli");

    if (!entry || !end_marker) {
        __asm__ volatile("sti");
        return -1;
    }

    size_t code_size = (uint64_t)end_marker - (uint64_t)entry;

    if (code_size == 0 || code_size > (USER_CODE_LIMIT - USER_CODE_BASE)) {
        __asm__ volatile("sti");
        return -1;
    }

    size_t   alloc_size   = (code_size + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
    uint64_t user_code_va = next_user_code_addr;

    if (user_code_va + alloc_size >= USER_CODE_LIMIT) {
        __asm__ volatile("sti");
        return -1;
    }

    address_space_t *kspace = vmm_get_kernel_space();

    for (size_t off = 0; off < alloc_size; off += PAGE_SIZE) {
        void *mapped = vmm_alloc_page(kspace, (void *)(user_code_va + off), VMM_USER_PAGE);
        if (!mapped) {
            __asm__ volatile("sti");
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
        stack_base, stack_top, initial_rsp, NULL, owner);

    if (pid < 0) {
        __asm__ volatile("sti");
        return -1;
    }

    next_user_code_addr = user_code_va + alloc_size;
    __asm__ volatile("sti");
    log_ok("PROC", "Created user task PID %d owner=%d | code=[0x%lx,0x%lx)",
           pid, owner, user_code_va, user_code_va + code_size);
    return pid;
}

int proc_create_user_image_as(uid_t owner, const uint8_t *image, size_t image_size,
                              uint64_t load_vaddr, uint64_t entry_vaddr,
                              uint32_t priority, uint32_t parent)
{
    if (!user_exists(owner))
        return -1;

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

    initial_rsp = setup_user_stack_minimal(proc_space, stack_top);

    int pid = proc_create_internal(
        entry_vaddr, priority, parent, PROC_TYPE_USER,
        load_vaddr, load_vaddr + image_size,
        stack_base, stack_top, initial_rsp,
        proc_space, owner);

    if (pid < 0) {
        vmm_destroy_address_space(proc_space);
        __asm__ volatile("sti");
        return -1;
    }

    __asm__ volatile("sti");
    log_ok("PROC", "Created user task PID %d owner=%d | entry=0x%lx",
           pid, owner, entry_vaddr);
    return pid;
}

static bool clone_user_region(address_space_t *dst, address_space_t *src,
                               uint64_t va_start, uint64_t va_end)
{
    address_space_t *kspace = vmm_get_kernel_space();

    uint8_t *page_buf = (uint8_t *)kmalloc(PAGE_SIZE);
    if (!page_buf)
        return false;

    for (uint64_t va = va_start; va < va_end; va += PAGE_SIZE) {
        void *phys_src = vmm_get_physical(src, (void *)va);
        if (!phys_src)
            continue;

        if (!vmm_map(kspace, VMM_SCRATCH_VA, phys_src, VMM_KERNEL_PAGE)) {
            kfree(page_buf);
            return false;
        }

        memcpy(page_buf, VMM_SCRATCH_VA, PAGE_SIZE);
        vmm_unmap(kspace, VMM_SCRATCH_VA);
        vmm_invlpg(VMM_SCRATCH_VA);

        if (!map_user_page(dst, va, page_buf, PAGE_SIZE)) {
            kfree(page_buf);
            return false;
        }
    }

    kfree(page_buf);
    return true;
}

static void pcb_copy_layout(PCB *dst, const PCB *src)
{
    dst->code_vaddr_start = src->code_vaddr_start;
    dst->code_vaddr_end   = src->code_vaddr_end;
    dst->stack_vaddr_base = src->stack_vaddr_base;
    dst->stack_vaddr_top  = src->stack_vaddr_top;
    dst->heap_start       = src->heap_start;
    dst->heap_end         = src->heap_end;
}

static int pcb_alloc_slot(void)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED)
            return i;
    }
    return -1;
}

static void pcb_init_child(PCB *child, const PCB *parent,
                            address_space_t *space, const Registers *frame)
{
    memset(child, 0, offsetof(PCB, kernel_stack));

    child->proc.PID        = next_pid++;
    child->proc.PPID       = parent->proc.PID;
    child->proc.Priority   = parent->proc.Priority;
    child->proc.WaitingFor = (uint32_t)-1;
    child->proc.Type       = parent->proc.Type;
    child->proc.Owner      = parent->proc.Owner;
    child->proc.EUID       = parent->proc.EUID;
    child->proc.SavedUID   = parent->proc.SavedUID;
    child->proc.Group      = parent->proc.Group;
    child->proc.EGroup     = parent->proc.EGroup;
    child->proc.SavedGID   = parent->proc.SavedGID;
    child->state           = PROC_READY;
    child->address_space   = space;
    child->vfork_parent    = -1;

    child->kernel_stack_top =
        (uint64_t)(child->kernel_stack + PROC_STACK_SIZE) & ~0xFULL;

    child->context     = *frame;
    child->context.rax = 0;

    memcpy(child->cwd, parent->cwd, sizeof(child->cwd));
    pcb_copy_layout(child, parent);
}

int proc_fork(Registers *frame)
{
    __asm__ volatile("cli");

    if (current_proc < 0) { __asm__ volatile("sti"); return -1; }

    PCB *parent = &proc_table[current_proc];

    int slot = pcb_alloc_slot();
    if (slot < 0) { __asm__ volatile("sti"); return -1; }

    address_space_t *child_space = NULL;

    if (parent->address_space) {
        child_space = vmm_create_address_space();
        if (!child_space) { __asm__ volatile("sti"); return -1; }

        uint64_t code_end_pg =
            (parent->code_vaddr_end + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
        uint64_t heap_end_pg =
            (parent->heap_end + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);

        bool ok = true;

        if (parent->code_vaddr_start < code_end_pg)
            ok &= clone_user_region(child_space, parent->address_space,
                                    parent->code_vaddr_start, code_end_pg);

        if (ok && parent->stack_vaddr_base < parent->stack_vaddr_top)
            ok &= clone_user_region(child_space, parent->address_space,
                                    parent->stack_vaddr_base, parent->stack_vaddr_top);

        if (ok && parent->heap_start < heap_end_pg)
            ok &= clone_user_region(child_space, parent->address_space,
                                    parent->heap_start, heap_end_pg);

        if (!ok) {
            vmm_destroy_address_space(child_space);
            __asm__ volatile("sti");
            return -1;
        }
    }

    PCB *child = &proc_table[slot];
    pcb_init_child(child, parent, child_space, frame);

    int child_pid = (int)child->proc.PID;
    __asm__ volatile("sti");
    log_ok("PROC", "fork: PID %d -> child PID %d", parent->proc.PID, child_pid);
    return child_pid;
}

int proc_vfork(Registers *frame)
{
    __asm__ volatile("cli");

    if (current_proc < 0) { __asm__ volatile("sti"); return -1; }

    PCB *parent = &proc_table[current_proc];

    int slot = pcb_alloc_slot();
    if (slot < 0) { __asm__ volatile("sti"); return -1; }

    PCB *child = &proc_table[slot];
    pcb_init_child(child, parent, parent->address_space, frame);
    child->vfork_parent = (int)parent->proc.PID;

    parent->context         = *frame;
    parent->context.rax     = (uint64_t)child->proc.PID;
    parent->state           = PROC_BLOCKED;
    parent->proc.WaitingFor = child->proc.PID;

    int child_pid = (int)child->proc.PID;
    __asm__ volatile("sti");
    log_ok("PROC", "vfork: PID %d -> child PID %d (parent blocked)",
           parent->proc.PID, child_pid);
    return child_pid;
}

int proc_clone(Registers *frame, uint64_t flags, uint64_t child_stack)
{
    if (flags & CLONE_VFORK) {
        int pid = proc_vfork(frame);
        if (pid > 0 && child_stack) {
            int idx = proc_find_index(pid);
            if (idx >= 0)
                proc_table[idx].context.rsp = child_stack;
        }
        return pid;
    }

    if (flags & CLONE_VM) {
        __asm__ volatile("cli");

        if (current_proc < 0) { __asm__ volatile("sti"); return -1; }

        PCB *parent = &proc_table[current_proc];

        int slot = pcb_alloc_slot();
        if (slot < 0) { __asm__ volatile("sti"); return -1; }

        PCB *child = &proc_table[slot];
        pcb_init_child(child, parent, parent->address_space, frame);

        if (child_stack)
            child->context.rsp = child_stack;

        int child_pid = (int)child->proc.PID;
        __asm__ volatile("sti");
        log_ok("PROC", "clone(CLONE_VM): PID %d -> child PID %d",
               parent->proc.PID, child_pid);
        return child_pid;
    }

    int fork_pid = proc_fork(frame);
    if (fork_pid > 0 && child_stack) {
        int idx = proc_find_index(fork_pid);
        if (idx >= 0)
            proc_table[idx].context.rsp = child_stack;
    }
    return fork_pid;
}

void proc_set_syscall_frame(Registers *frame)
{
    current_syscall_frame = frame;
}

Registers *proc_get_syscall_frame(void)
{
    return current_syscall_frame;
}

int proc_getppid(void)
{
    if (current_proc < 0)
        return -1;
    return (int)proc_table[current_proc].proc.PPID;
}

uid_t proc_getuid(void)
{
    if (current_proc < 0) return (uid_t)-1;
    return proc_table[current_proc].proc.Owner;
}

uid_t proc_geteuid(void)
{
    if (current_proc < 0) return (uid_t)-1;
    return proc_table[current_proc].proc.EUID;
}

int proc_setuid(uid_t uid)
{
    if (current_proc < 0) return -1;
    PCB *pcb = &proc_table[current_proc];
    if (user_is_root(pcb->proc.EUID)) {
        pcb->proc.Owner    = uid;
        pcb->proc.EUID     = uid;
        pcb->proc.SavedUID = uid;
        return 0;
    }
    if (uid == pcb->proc.Owner || uid == pcb->proc.SavedUID) {
        pcb->proc.EUID = uid;
        return 0;
    }
    return -1;
}

int proc_seteuid(uid_t uid)
{
    if (current_proc < 0) return -1;
    PCB *pcb = &proc_table[current_proc];
    if (user_is_root(pcb->proc.EUID)) {
        pcb->proc.EUID = uid;
        return 0;
    }
    if (uid == pcb->proc.Owner || uid == pcb->proc.SavedUID) {
        pcb->proc.EUID = uid;
        return 0;
    }
    return -1;
}

int proc_setreuid(uid_t ruid, uid_t euid)
{
    if (current_proc < 0) return -1;
    PCB  *pcb     = &proc_table[current_proc];
    bool  root    = user_is_root(pcb->proc.EUID);
    uid_t old_r   = pcb->proc.Owner;

    if (ruid != (uid_t)-1) {
        if (!root && ruid != pcb->proc.Owner && ruid != pcb->proc.EUID)
            return -1;
        pcb->proc.Owner = ruid;
    }
    if (euid != (uid_t)-1) {
        if (!root && euid != old_r && euid != pcb->proc.EUID && euid != pcb->proc.SavedUID)
            return -1;
        pcb->proc.EUID = euid;
    }
    if (ruid != (uid_t)-1 || (euid != (uid_t)-1 && euid != old_r))
        pcb->proc.SavedUID = pcb->proc.EUID;
    return 0;
}

int proc_apply_setuid_exec(uid_t file_owner, uint16_t file_mode)
{
    if (current_proc < 0) return -1;
    PCB *pcb = &proc_table[current_proc];
    if (file_mode & 0x0800) {
        pcb->proc.SavedUID = file_owner;
        pcb->proc.EUID     = file_owner;
    }
    return 0;
}

gid_t proc_getgid(void)
{
    if (current_proc < 0) return (gid_t)-1;
    return proc_table[current_proc].proc.Group;
}

gid_t proc_getegid(void)
{
    if (current_proc < 0) return (gid_t)-1;
    return proc_table[current_proc].proc.EGroup;
}

int proc_setgid(gid_t gid)
{
    if (current_proc < 0) return -1;
    PCB *pcb = &proc_table[current_proc];
    if (user_is_root(pcb->proc.EUID)) {
        pcb->proc.Group    = gid;
        pcb->proc.EGroup   = gid;
        pcb->proc.SavedGID = gid;
        return 0;
    }
    if (gid == pcb->proc.Group || gid == pcb->proc.SavedGID) {
        pcb->proc.EGroup = gid;
        return 0;
    }
    return -1;
}

int proc_setegid(gid_t gid)
{
    if (current_proc < 0) return -1;
    PCB *pcb = &proc_table[current_proc];
    if (user_is_root(pcb->proc.EUID)) {
        pcb->proc.EGroup = gid;
        return 0;
    }
    if (gid == pcb->proc.Group || gid == pcb->proc.SavedGID) {
        pcb->proc.EGroup = gid;
        return 0;
    }
    return -1;
}

int proc_setregid(gid_t rgid, gid_t egid)
{
    if (current_proc < 0) return -1;
    PCB  *pcb   = &proc_table[current_proc];
    bool  root  = user_is_root(pcb->proc.EUID);
    gid_t old_r = pcb->proc.Group;

    if (rgid != (gid_t)-1) {
        if (!root && rgid != pcb->proc.Group && rgid != pcb->proc.EGroup)
            return -1;
        pcb->proc.Group = rgid;
    }
    if (egid != (gid_t)-1) {
        if (!root && egid != old_r && egid != pcb->proc.EGroup && egid != pcb->proc.SavedGID)
            return -1;
        pcb->proc.EGroup = egid;
    }
    if (rgid != (gid_t)-1 || (egid != (gid_t)-1 && egid != old_r))
        pcb->proc.SavedGID = pcb->proc.EGroup;
    return 0;
}

int proc_setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
    if (current_proc < 0) return -1;
    PCB  *pcb  = &proc_table[current_proc];
    bool  root = user_is_root(pcb->proc.EUID);

    if (!root) {
        gid_t r = pcb->proc.Group;
        gid_t e = pcb->proc.EGroup;
        gid_t s = pcb->proc.SavedGID;
        if (rgid != (gid_t)-1 && rgid != r && rgid != e && rgid != s) return -1;
        if (egid != (gid_t)-1 && egid != r && egid != e && egid != s) return -1;
        if (sgid != (gid_t)-1 && sgid != r && sgid != e && sgid != s) return -1;
    }

    if (rgid != (gid_t)-1) pcb->proc.Group    = rgid;
    if (egid != (gid_t)-1) pcb->proc.EGroup   = egid;
    if (sgid != (gid_t)-1) pcb->proc.SavedGID = sgid;
    return 0;
}

int proc_getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid)
{
    if (current_proc < 0) return -1;
    PCB *pcb = &proc_table[current_proc];
    if (rgid) *rgid = pcb->proc.Group;
    if (egid) *egid = pcb->proc.EGroup;
    if (sgid) *sgid = pcb->proc.SavedGID;
    return 0;
}

int proc_getcwd(char *buf, size_t size)
{
    if (!buf || size == 0 || current_proc < 0)
        return -1;
    const char *cwd = proc_table[current_proc].cwd;
    size_t len = strlen(cwd);
    if (len + 1 > size)
        return -1;
    memcpy(buf, cwd, len + 1);
    return 0;
}

int proc_chdir(const char *path)
{
    if (!path || current_proc < 0)
        return -1;
    size_t len = strlen(path);
    if (len >= sizeof(proc_table[0].cwd))
        return -1;
    memcpy(proc_table[current_proc].cwd, path, len + 1);
    return 0;
}

bool proc_read_from_user(int pid, void *dst, const void *user_src, size_t n)
{
    if (!dst || !user_src || n == 0) return false;

    int idx = proc_find_index(pid);
    if (idx < 0) return false;

    address_space_t *proc_space = proc_table[idx].address_space;
    if (!proc_space) {
        memcpy(dst, user_src, n);
        return true;
    }

    address_space_t *kspace = vmm_get_kernel_space();
    uint8_t         *kdst   = (uint8_t *)dst;
    const uint8_t   *usrc   = (const uint8_t *)user_src;
    size_t           remaining = n;

    while (remaining > 0) {
        size_t page_off = (uintptr_t)usrc & (PAGE_SIZE - 1);
        size_t chunk    = PAGE_SIZE - page_off;
        if (chunk > remaining) chunk = remaining;

        uint64_t page_va = (uintptr_t)usrc & ~(uintptr_t)(PAGE_SIZE - 1);

        void *phys = vmm_get_physical(proc_space, (void *)page_va);
        if (!phys) {
            log_err("PROC", "read_from_user: no phys for VA=0x%lx", page_va);
            return false;
        }

        vmm_map(kspace, VMM_SCRATCH_VA, phys, VMM_KERNEL_PAGE);
        memcpy(kdst, (uint8_t *)VMM_SCRATCH_VA + page_off, chunk);
        vmm_unmap(kspace, VMM_SCRATCH_VA);
        vmm_invlpg(VMM_SCRATCH_VA);

        kdst      += chunk;
        usrc      += chunk;
        remaining -= chunk;
    }

    return true;
}