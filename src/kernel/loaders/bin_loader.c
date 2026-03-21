#include "bin_loader.h"

#include <hal/vfs.h>
#include <proc/proc.h>
#include <heap.h>
#include <memory.h>
#include <debug.h>

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;
typedef uint8_t  Elf64_Byte;

#define EI_NIDENT    16
#define ELFMAG0      0x7Fu
#define ELFMAG1      'E'
#define ELFMAG2      'L'
#define ELFMAG3      'F'
#define ELFCLASS64   2
#define ELFDATA2LSB  1
#define ET_EXEC      2
#define EM_X86_64    62
#define PT_LOAD      1

typedef struct __attribute__((packed)) {
    Elf64_Byte  e_ident[EI_NIDENT];
    Elf64_Half  e_type;
    Elf64_Half  e_machine;
    Elf64_Word  e_version;
    Elf64_Addr  e_entry;
    Elf64_Off   e_phoff;
    Elf64_Off   e_shoff;
    Elf64_Word  e_flags;
    Elf64_Half  e_ehsize;
    Elf64_Half  e_phentsize;
    Elf64_Half  e_phnum;
    Elf64_Half  e_shentsize;
    Elf64_Half  e_shnum;
    Elf64_Half  e_shstrndx;
} Elf64_Ehdr;

typedef struct __attribute__((packed)) {
    Elf64_Word  p_type;
    Elf64_Word  p_flags;
    Elf64_Off   p_offset;
    Elf64_Addr  p_vaddr;
    Elf64_Addr  p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
} Elf64_Phdr;

#define BIN_MAX_IMAGE_SIZE  0x20000000ULL
#define BIN_MAX_PHDRS       64u

static int elf_validate(const Elf64_Ehdr *ehdr)
{
    if (ehdr->e_ident[0] != ELFMAG0 ||
        ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 ||
        ehdr->e_ident[3] != ELFMAG3) {
        log_err("BIN", "Not an ELF file (bad magic)");
        return -1;
    }
    if (ehdr->e_ident[4] != ELFCLASS64) {
        log_err("BIN", "Not a 64-bit ELF (EI_CLASS=%d)", (int)ehdr->e_ident[4]);
        return -1;
    }
    if (ehdr->e_ident[5] != ELFDATA2LSB) {
        log_err("BIN", "Not a little-endian ELF (EI_DATA=%d)", (int)ehdr->e_ident[5]);
        return -1;
    }
    if (ehdr->e_type != ET_EXEC) {
        log_err("BIN", "Not an executable ELF (e_type=%d). "
                       "Link with -static and -no-pie.", (int)ehdr->e_type);
        return -1;
    }
    if (ehdr->e_machine != EM_X86_64) {
        log_err("BIN", "Wrong target ISA (e_machine=%d, expected %d for x86-64)",
                (int)ehdr->e_machine, EM_X86_64);
        return -1;
    }
    if (ehdr->e_phnum == 0) {
        log_err("BIN", "No program headers in ELF");
        return -1;
    }
    if (ehdr->e_phentsize < sizeof(Elf64_Phdr)) {
        log_err("BIN", "Program header entry too small: %d < %d",
                (int)ehdr->e_phentsize, (int)sizeof(Elf64_Phdr));
        return -1;
    }
    if (ehdr->e_phnum > BIN_MAX_PHDRS) {
        log_err("BIN", "Too many program headers: %d (limit %d)",
                (int)ehdr->e_phnum, BIN_MAX_PHDRS);
        return -1;
    }
    return 0;
}

static int elf_read_at(int fd, uint32_t file_offset, void *buf, size_t size)
{
    if (VFS_Set_Pos(fd, file_offset, true) < 0) {
        log_err("BIN", "VFS_Set_Pos(0x%x) failed", file_offset);
        return -1;
    }
    int got = VFS_Read(fd, size, buf);
    if (got < 0 || (size_t)got < size) {
        log_err("BIN", "Short read: wanted %d bytes at offset 0x%x, got %d",
                (int)size, file_offset, got);
        return -1;
    }
    return 0;
}

static int elf_parse_load_range(const uint8_t *phdrs, const Elf64_Ehdr *ehdr,
                                 uint64_t *out_base, uint64_t *out_end)
{
    uint64_t load_base = UINT64_MAX;
    uint64_t load_end  = 0;
    int      n_load    = 0;

    for (int i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr *ph =
            (const Elf64_Phdr *)(phdrs + (size_t)i * ehdr->e_phentsize);

        if (ph->p_type != PT_LOAD || ph->p_memsz == 0)
            continue;

        n_load++;

        if (ph->p_vaddr < load_base)
            load_base = ph->p_vaddr;

        uint64_t seg_end = ph->p_vaddr + ph->p_memsz;
        if (seg_end > load_end)
            load_end = seg_end;

        if (ph->p_filesz > ph->p_memsz) {
            log_err("BIN", "Phdr %d: p_filesz (%lu) > p_memsz (%lu) - corrupt ELF",
                    i, (unsigned long)ph->p_filesz, (unsigned long)ph->p_memsz);
            return -1;
        }
    }

    if (n_load == 0) {
        log_err("BIN", "ELF has no loadable PT_LOAD segments");
        return -1;
    }

    *out_base = load_base;
    *out_end  = load_end;
    return n_load;
}

static int elf_read_segments(int fd, const Elf64_Ehdr *ehdr,
                              const uint8_t *phdrs,
                              uint8_t *flat_img, size_t flat_size,
                              uint64_t load_base)
{
    for (int i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr *ph =
            (const Elf64_Phdr *)(phdrs + (size_t)i * ehdr->e_phentsize);

        if (ph->p_type != PT_LOAD || ph->p_memsz == 0)
            continue;

        size_t buf_off = (size_t)(ph->p_vaddr - load_base);
        if (buf_off + ph->p_memsz > flat_size) {
            log_err("BIN", "Phdr %d: segment overflows flat image buffer", i);
            return -1;
        }

        if (ph->p_filesz > 0) {
            if (ph->p_offset > 0xFFFFFFFFu) {
                log_err("BIN", "Phdr %d: p_offset 0x%lx exceeds 32-bit VFS limit",
                        i, ph->p_offset);
                return -1;
            }
            if (elf_read_at(fd, (uint32_t)ph->p_offset,
                            flat_img + buf_off, (size_t)ph->p_filesz) < 0) {
                log_err("BIN", "Failed to read segment %d data", i);
                return -1;
            }
        }

        log_info("BIN", "  Segment %d: vaddr=0x%lx  filesz=%lu  memsz=%lu%s",
                 i, ph->p_vaddr,
                 (unsigned long)ph->p_filesz,
                 (unsigned long)ph->p_memsz,
                 (ph->p_memsz > ph->p_filesz) ? "  (BSS tail zeroed)" : "");
    }
    return 0;
}

static int elf_open_and_load(const char *path,
                              uint8_t **out_flat_img, size_t *out_flat_size,
                              uint64_t *out_load_base, uint64_t *out_entry)
{
    int     ret    = -1;
    uint8_t *phdrs = NULL;
    uint8_t *flat_img = NULL;

    int fd = VFS_Open(path, true);
    if (fd < 0) {
        log_err("BIN", "Failed to open '%s' (VFS error %d)", path, fd);
        return -1;
    }

    Elf64_Ehdr ehdr;
    if (elf_read_at(fd, 0, &ehdr, sizeof(Elf64_Ehdr)) < 0)
        goto out_close;

    if (elf_validate(&ehdr) < 0)
        goto out_close;

    size_t phdrs_bytes = (size_t)ehdr.e_phnum * ehdr.e_phentsize;
    phdrs = (uint8_t *)kmalloc(phdrs_bytes);
    if (!phdrs) {
        log_err("BIN", "kmalloc(%d) for phdrs failed", (int)phdrs_bytes);
        goto out_close;
    }

    if (ehdr.e_phoff > 0xFFFFFFFFu) {
        log_err("BIN", "e_phoff 0x%lx exceeds 32-bit VFS limit", ehdr.e_phoff);
        goto out_phdrs;
    }

    if (elf_read_at(fd, (uint32_t)ehdr.e_phoff, phdrs, phdrs_bytes) < 0)
        goto out_phdrs;

    uint64_t load_base, load_end;
    int n_load = elf_parse_load_range(phdrs, &ehdr, &load_base, &load_end);
    if (n_load < 0)
        goto out_phdrs;

    size_t flat_size = (size_t)(load_end - load_base);

    if (flat_size == 0) {
        log_err("BIN", "Computed flat image size is 0");
        goto out_phdrs;
    }

    if (flat_size > BIN_MAX_IMAGE_SIZE) {
        log_err("BIN", "Flat image size %d bytes exceeds limit %d",
                (int)flat_size, (int)BIN_MAX_IMAGE_SIZE);
        goto out_phdrs;
    }

    if (ehdr.e_entry < load_base || ehdr.e_entry >= load_end) {
        log_err("BIN", "e_entry 0x%lx is outside loaded range [0x%lx, 0x%lx)",
                ehdr.e_entry, load_base, load_end);
        goto out_phdrs;
    }

    log_info("BIN", "Load range: 0x%lx - 0x%lx  (%d bytes, %d segment(s))",
             load_base, load_end, (int)flat_size, n_load);

    flat_img = (uint8_t *)kmalloc(flat_size);
    if (!flat_img) {
        log_err("BIN", "kmalloc(%d) for flat image failed", (int)flat_size);
        goto out_phdrs;
    }
    memset(flat_img, 0, flat_size);

    if (elf_read_segments(fd, &ehdr, phdrs, flat_img, flat_size, load_base) < 0)
        goto out_img;

    *out_flat_img  = flat_img;
    *out_flat_size = flat_size;
    *out_load_base = load_base;
    *out_entry     = ehdr.e_entry;
    ret = 0;
    goto out_phdrs;

out_img:
    kfree(flat_img);
out_phdrs:
    kfree(phdrs);
out_close:
    VFS_Close(fd, true);
    return ret;
}

int bin_load_elf(const char *path, uint32_t priority, uint32_t parent)
{
    if (!path) {
        log_err("BIN", "bin_load_elf: NULL path");
        return -1;
    }

    log_info("BIN", "Loading ELF: %s", path);

    uint8_t  *flat_img  = NULL;
    size_t    flat_size = 0;
    uint64_t  load_base = 0;
    uint64_t  entry     = 0;

    if (elf_open_and_load(path, &flat_img, &flat_size, &load_base, &entry) < 0)
        return -1;

    int ret = proc_create_user_image(flat_img, flat_size,
                                     load_base, entry,
                                     priority, parent);

    if (ret < 0)
        log_err("BIN", "Failed to create process for '%s'", path);
    else
        log_ok("BIN", "Launched '%s' -> PID %d  (entry=0x%lx, base=0x%lx)",
               path, ret, entry, load_base);

    kfree(flat_img);
    return ret;
}

int bin_load_elf_argv(const char *path, uint32_t priority, uint32_t parent,
                      int argc, const char **argv,
                      int envc, const char **envp)
{
    if (!path) {
        log_err("BIN", "bin_load_elf_argv: NULL path");
        return -1;
    }

    log_info("BIN", "Loading ELF: %s  (argc=%d, envc=%d)", path, argc, envc);

    uint8_t  *flat_img  = NULL;
    size_t    flat_size = 0;
    uint64_t  load_base = 0;
    uint64_t  entry     = 0;

    if (elf_open_and_load(path, &flat_img, &flat_size, &load_base, &entry) < 0)
        return -1;

    int ret = proc_create_user_image_argv(flat_img, flat_size,
                                          load_base, entry,
                                          priority, parent,
                                          argc, argv,
                                          envc, envp);

    if (ret < 0)
        log_err("BIN", "Failed to create process for '%s'", path);
    else
        log_ok("BIN", "Launched '%s' -> PID %d  (entry=0x%lx, base=0x%lx)",
               path, ret, entry, load_base);

    kfree(flat_img);
    return ret;
}