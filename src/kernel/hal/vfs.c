#include "vfs.h"
#include <user/user.h>
#include <arch/x86_64/e9.h>
#include <fb/textrenderer.h>
#include <block/block.h>
#include <stdbool.h>
#include <drivers/fs/ext/ext2.h>
#include <drivers/disk/ata.h>
#include <block/block_image.h>
#include <config/config.h>
#include <heap.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <debug.h>
#include <panic/panic.h>
#include <proc/proc.h>
#include <console/console.h>
#include <device/device.h>
#include <device/stdin/device_stdin.h>
#include <device/stdout/device_stdout.h>
#include <block/block_mbr.h>
#include <block/block_ramdisk.h>
#include <mkfs/ext2_format.h>
#include <errno/errno.h>
#include <net/unix_socket.h>

block_device_t* rootdrive;
ext2_fs_t*      rootfs;

bool mounted = false;

static vfs_mount_t mount_table[MAX_MOUNTS];

const char* special_paths[] = {"/dev/stdin", "/dev/stdout", "/dev/stderr", "/dev/stddbg"};
const int sp_len = sizeof(special_paths) / sizeof(special_paths[0]);

VFS_File_t* open_files;

int get_value_by_key(const char* input, int key, char* out, size_t out_size)
{
    if (!input || !out || out_size == 0)
        return (int)serror(EINVAL);

    int current_key = 0;
    size_t out_index = 0;

    while (*input)
    {
        if (current_key == key)
        {
            if (*input == '/')
                break;

            if (out_index + 1 < out_size)
                out[out_index++] = *input;
        }

        if (*input == '/')
            current_key++;

        input++;
    }

    if (current_key != key)
        return (int)serror(ENOENT);

    out[out_index] = '\0';
    return 0;
}

static int check_path(const char* path)
{
    for (int i = 0; i < sp_len; i++) {
        if (strcmp(path, special_paths[i]) == 0)
            return i;
    }
    return -1;
}

static uid_t vfs_caller_uid(void)
{
    int pid = proc_get_current_pid();
    if (pid < 0)
        return UID_ROOT;
    uid_t uid = proc_get_owner(pid);
    if (!user_exists(uid))
        return UID_ROOT;
    return uid;
}

static ext2_fs_t* resolve_fs(const char* path, const char** out_path)
{
    ext2_fs_t*  best_fs  = rootfs;
    const char* best_rel = path;
    size_t      best_len = 0;

    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (!mount_table[i].active)
            continue;

        size_t mplen = strlen(mount_table[i].mountpoint);
        if (mplen <= best_len)
            continue;

        if (strncmp(path, mount_table[i].mountpoint, mplen) == 0) {
            if (path[mplen] == '/' || path[mplen] == '\0') {
                best_fs  = mount_table[i].fs;
                best_rel = (path[mplen] == '/') ? path + mplen : "/";
                best_len = mplen;
            }
        }
    }

    *out_path = best_rel;
    return best_fs;
}

static bool vfs_access_ok(ext2_fs_t* fs, const char* path, int mask, bool privileged)
{
    if (privileged)
        return true;
    uid_t uid = vfs_caller_uid();
    if (user_is_root(uid))
        return true;
    return ext2_access(fs, path, uid, mask) == EXT2_SUCCESS;
}

int VFS_Mount(const char* source, const char* target)
{
    block_device_t* dev = block_get(source);
    if (!dev)
        return (int)serror(ENODEV);

    ext2_fs_t* fs = ext2_mount(dev);
    if (!fs)
        return (int)serror(EIO);

    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (!mount_table[i].active) {
            strncpy(mount_table[i].mountpoint, target, sizeof(mount_table[i].mountpoint) - 1);
            mount_table[i].mountpoint[sizeof(mount_table[i].mountpoint) - 1] = '\0';
            mount_table[i].fs     = fs;
            mount_table[i].dev    = dev;
            mount_table[i].active = true;
            log_ok("VFS", "Mounted %s at %s", source, target);
            return 0;
        }
    }

    ext2_unmount(fs);
    log_err("VFS", "VFS_Mount: mount table full");
    return (int)serror(ENFILE);
}

int VFS_Unmount_Path(const char* target)
{
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (mount_table[i].active && strcmp(mount_table[i].mountpoint, target) == 0) {
            ext2_unmount(mount_table[i].fs);
            mount_table[i].active = false;
            log_ok("VFS", "Unmounted %s", target);
            return 0;
        }
    }
    log_err("VFS", "VFS_Unmount_Path: %s not found", target);
    return (int)serror(ENOENT);
}

int VFS_Create(const char* path, bool isDir)
{
    const char* rel;
    ext2_fs_t*  fs = resolve_fs(path, &rel);

    if (!vfs_access_ok(fs, rel, ACCESS_WRITE, false)) {
        log_err("VFS", "VFS_Create: permission denied for %s", path);
        return (int)serror(EPERM);
    }

    if (isDir) {
        int result = ext2_mkdir(fs, rel);
        if (result == EXT2_SUCCESS)
            return 0;
        return (int)serror(ENOTDIR);
    }

    int result = ext2_create(fs, rel, 0644);
    if (result == EXT2_SUCCESS)
        return 0;
    return (int)serror(EIO);
}

static int setflags(file_flags_t flags, int fd)
{
    if (!open_files[fd].exists)
        return (int)serror(EBADF);

    open_files[fd].flags = flags;
    return 0;
}

int VFS_Open(const char* path, bool privileged)
{
    int response = check_path(path);
    if (response != -1 && !privileged)
        return response;

    const char* rel;
    ext2_fs_t*  fs = resolve_fs(path, &rel);

    if (!vfs_access_ok(fs, rel, ACCESS_READ, privileged)) {
        log_err("VFS", "VFS_Open: permission denied for %s", path);
        return (int)serror(EPERM);
    }

    uid_t caller_uid = privileged ? UID_ROOT : vfs_caller_uid();

    ext2_inode_t inode;
    if (ext2_stat(fs, rel, &inode) == EXT2_SUCCESS &&
        (inode.i_mode & 0xF000) == EXT2_S_IFDIR)
    {
        ext2_dir_iter_t* iter = ext2_opendir(fs, rel);
        if (!iter)
            return (int)serror(ENOTDIR);

        for (int i = 0; i < MAX_OPEN_FILES; i++) {
            if (!open_files[i].exists) {
                open_files[i].path      = path;
                open_files[i].file      = NULL;
                open_files[i].dir_iter  = iter;
                open_files[i].fs        = fs;
                open_files[i].is_dir    = true;
                open_files[i].exists    = true;
                open_files[i].is_dev    = false;
                open_files[i].is_socket = false;
                open_files[i].owner     = caller_uid;
                open_files[i].pid       = privileged ? -1 : proc_get_current_pid();
                open_files[i].write_all = false;
                if (privileged)
                    open_files[i].flags = KERNEL;
                return i;
            }
        }

        ext2_closedir(iter);
        return (int)serror(EMFILE);
    }

    ext2_file_t* ext2_file = ext2_open(fs, rel);
    if (!ext2_file)
        return (int)serror(ENOENT);

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i].exists) {
            open_files[i].path      = path;
            open_files[i].file      = ext2_file;
            open_files[i].dir_iter  = NULL;
            open_files[i].fs        = fs;
            open_files[i].is_dir    = false;
            open_files[i].exists    = true;
            open_files[i].owner     = caller_uid;
            open_files[i].pid       = proc_get_current_pid();
            open_files[i].write_all = false;
            open_files[i].is_socket = false;

            if (privileged) {
                open_files[i].pid   = -1;
                open_files[i].flags = KERNEL;
            }

            device_t* dev = device_get(path);
            if (dev != NULL) {
                open_files[i].is_dev = true;
                open_files[i].dev    = dev;
                log_info("VFS", "is dev");
            }

            return i;
        }
    }

    ext2_close(ext2_file);
    return (int)serror(EMFILE);
}

int VFS_ioctl(int fd, uint64_t req, void* arg)
{
    int pid = proc_get_current_pid();

    if (!open_files[fd].exists || !open_files[fd].is_dev || open_files[fd].pid != pid)
        return (int)serror(EBADF);

    return open_files[fd].dev->dispatch(pid, req, arg);
}

int VFS_Write(int fd, size_t count, void* buf, bool privileged)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return (int)serror(EBADF);

    if (!open_files[fd].exists)
        return (int)serror(EBADF);

    
    if (open_files[fd].is_socket) {
        if (open_files[fd].pid != proc_get_current_pid())
            return (int)serror(EACCES);
        return unix_sock_write(open_files[fd].unix_sock_id, buf, count);
    }

    if (!privileged && open_files[fd].pid != proc_get_current_pid() && !open_files[fd].write_all)
        return (int)serror(EACCES);

    const char* rel;
    resolve_fs(open_files[fd].path, &rel);

    if (!open_files[fd].is_dev
        && !vfs_access_ok(open_files[fd].fs, rel, ACCESS_WRITE, privileged))
        return (int)serror(EACCES);

    if (!open_files[fd].file)
        return (int)serror(EBADF);

    if (open_files[fd].is_dev && open_files[fd].dev->write != NULL)
        return open_files[fd].dev->write(count, buf);

    int result = ext2_write(open_files[fd].file, buf, (uint32_t)count);
    if (result < 0)
        return (int)serror(EIO);

    return result;
}

static void setwriteall(bool new, int fd)
{
    open_files[fd].write_all = true;
}

int VFS_Read(int fd, size_t count, void* buf)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return (int)serror(EBADF);

    if (!open_files[fd].exists)
        return (int)serror(EBADF);

    
    if (open_files[fd].is_socket) {
        if (open_files[fd].pid != proc_get_current_pid())
            return (int)serror(EACCES);
        return unix_sock_read(open_files[fd].unix_sock_id, buf, count);
    }

    if (open_files[fd].pid != -1 && open_files[fd].pid != proc_get_current_pid())
        return (int)serror(EACCES);

    if (!open_files[fd].file)
        return (int)serror(EBADF);

    if (open_files[fd].is_dev && open_files[fd].dev->read != NULL)
        return open_files[fd].dev->read(count, buf);

    int result = ext2_read(open_files[fd].file, buf, (uint32_t)count);
    if (result < 0)
        return (int)serror(EIO);

    return result;
}

int VFS_Close(int fd, bool privileged)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return (int)serror(EBADF);

    if (!open_files[fd].exists)
        return (int)serror(EBADF);

    
    if (open_files[fd].is_socket) {
        if (!privileged && open_files[fd].pid != proc_get_current_pid())
            return (int)serror(EACCES);
        unix_sock_destroy(open_files[fd].unix_sock_id);
        open_files[fd].exists    = false;
        open_files[fd].is_socket = false;
        return 0;
    }

    if (!privileged && (open_files[fd].flags & KERNEL))
        return (int)serror(EACCES);

    if (!privileged && open_files[fd].pid != proc_get_current_pid())
        return (int)serror(EACCES);

    if (open_files[fd].is_dir && open_files[fd].dir_iter) {
        ext2_closedir(open_files[fd].dir_iter);
        open_files[fd].dir_iter = NULL;
    } else if (open_files[fd].file) {
        ext2_close(open_files[fd].file);
        open_files[fd].file = NULL;
    }

    open_files[fd].exists = false;
    open_files[fd].is_dir = false;
    return 0;
}

int VFS_GetDents64(int fd, struct linux_dirent64* buf, size_t count)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return (int)serror(EBADF);

    if (!open_files[fd].exists || !open_files[fd].is_dir || !open_files[fd].dir_iter)
        return (int)serror(ENOTDIR);

    if (open_files[fd].pid != proc_get_current_pid())
        return (int)serror(EACCES);

    size_t written = 0;
    uint8_t* ptr = (uint8_t*)buf;

    while (written < count) {
        vfs_dirent_t entry;

        int result = ext2_readdir(open_files[fd].dir_iter, entry.name, &entry.inode, &entry.type);
        if (result < 0)
            break;

        size_t namelen = strlen(entry.name);
        size_t reclen  = sizeof(struct linux_dirent64) + namelen + 1;
        reclen = (reclen + 7) & ~7;

        if (written + reclen > count)
            break;

        struct linux_dirent64* d = (struct linux_dirent64*)ptr;
        d->d_ino    = entry.inode;
        d->d_off    = (int64_t)(written + reclen);
        d->d_reclen = (unsigned short)reclen;
        d->d_type   = entry.type;
        memcpy(d->d_name, entry.name, namelen + 1);

        ptr     += reclen;
        written += reclen;
    }

    return (int)written;
}

int VFS_Set_Pos(int fd, uint32_t pos, bool privileged)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return (int)serror(EBADF);

    if (!open_files[fd].exists || !open_files[fd].file)
        return (int)serror(EBADF);

    if (!privileged && (open_files[fd].flags & KERNEL) && !(open_files[fd].flags & USER_WRITE))
        return (int)serror(EACCES);

    if (!privileged && open_files[fd].pid != proc_get_current_pid())
        return (int)serror(EACCES);

    int result = ext2_seek(open_files[fd].file, pos, EXT2_SEEK_SET);
    if (result == EXT2_SUCCESS)
        return 0;

    return (int)serror(ESPIPE);
}









static int vfs_alloc_socket_fd(int sock_id)
{
    
    for (int i = 4; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i].exists) {
            memset(&open_files[i], 0, sizeof(VFS_File_t));
            open_files[i].exists       = true;
            open_files[i].is_socket    = true;
            open_files[i].unix_sock_id = sock_id;
            open_files[i].pid          = proc_get_current_pid();
            open_files[i].path         = NULL;  
            return i;
        }
    }
    return -1;
}



int VFS_Socket(int domain, int type, int protocol)
{
    (void)protocol;

    if (domain != AF_UNIX) {
        log_err("VFS", "VFS_Socket: only AF_UNIX is supported (got %d)", domain);
        return (int)serror(EAFNOSUPPORT);
    }

    if (type != SOCK_STREAM && type != SOCK_DGRAM) {
        log_err("VFS", "VFS_Socket: unsupported type %d", type);
        return (int)serror(ESOCKTNOSUPPORT);
    }

    int sock_id = unix_sock_create(type);
    if (sock_id < 0) {
        log_err("VFS", "VFS_Socket: unix socket table full");
        return (int)serror(ENFILE);
    }

    int fd = vfs_alloc_socket_fd(sock_id);
    if (fd < 0) {
        unix_sock_destroy(sock_id);
        log_err("VFS", "VFS_Socket: open_files table full");
        return (int)serror(EMFILE);
    }

    log_ok("VFS", "socket(): fd=%d sock_id=%d type=%s",
           fd, sock_id, type == SOCK_STREAM ? "STREAM" : "DGRAM");
    return fd;
}



int VFS_Bind(int fd, const struct sockaddr_un *addr, uint32_t addrlen)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return (int)serror(EBADF);
    if (!open_files[fd].exists || !open_files[fd].is_socket)
        return (int)serror(ENOTSOCK);
    if (open_files[fd].pid != proc_get_current_pid())
        return (int)serror(EACCES);
    if (!addr || addrlen < sizeof(uint16_t))
        return (int)serror(EINVAL);
    if (addr->sun_family != AF_UNIX)
        return (int)serror(EAFNOSUPPORT);

    const char *path = addr->sun_path;

    




    VFS_Create(path, false);

    int rc = unix_sock_bind(open_files[fd].unix_sock_id, path);
    if (rc < 0) {
        log_err("VFS", "VFS_Bind: bind failed for fd=%d path='%s'", fd, path);
        return (int)serror(EADDRINUSE);
    }

    
    open_files[fd].path = addr->sun_path;

    log_ok("VFS", "bind(): fd=%d -> '%s'", fd, path);
    return 0;
}



int VFS_Listen(int fd, int backlog)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return (int)serror(EBADF);
    if (!open_files[fd].exists || !open_files[fd].is_socket)
        return (int)serror(ENOTSOCK);
    if (open_files[fd].pid != proc_get_current_pid())
        return (int)serror(EACCES);

    int rc = unix_sock_listen(open_files[fd].unix_sock_id, backlog);
    if (rc < 0) {
        log_err("VFS", "VFS_Listen: listen failed for fd=%d", fd);
        return (int)serror(EOPNOTSUPP);
    }

    log_ok("VFS", "listen(): fd=%d sock_id=%d", fd, open_files[fd].unix_sock_id);
    return 0;
}



int VFS_Accept(int fd, struct sockaddr_un *peer_addr, uint32_t *addrlen)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return (int)serror(EBADF);
    if (!open_files[fd].exists || !open_files[fd].is_socket)
        return (int)serror(ENOTSOCK);
    if (open_files[fd].pid != proc_get_current_pid())
        return (int)serror(EACCES);

    
    int new_sock_id = unix_sock_accept(open_files[fd].unix_sock_id);
    if (new_sock_id < 0) {
        log_err("VFS", "VFS_Accept: accept failed on fd=%d", fd);
        return (int)serror(EINVAL);
    }

    
    if (peer_addr && addrlen && *addrlen >= sizeof(struct sockaddr_un)) {
        unix_socket_t *conn_sock = unix_sock_get(new_sock_id);
        if (conn_sock && conn_sock->peer_id >= 0) {
            unix_socket_t *client_sock = unix_sock_get(conn_sock->peer_id);
            if (client_sock) {
                peer_addr->sun_family = AF_UNIX;
                strncpy(peer_addr->sun_path, client_sock->path, UNIX_PATH_MAX - 1);
                peer_addr->sun_path[UNIX_PATH_MAX - 1] = '\0';
            }
        }
        *addrlen = sizeof(struct sockaddr_un);
    }

    
    int new_fd = vfs_alloc_socket_fd(new_sock_id);
    if (new_fd < 0) {
        unix_sock_destroy(new_sock_id);
        log_err("VFS", "VFS_Accept: open_files full, dropping connection");
        return (int)serror(EMFILE);
    }

    log_ok("VFS", "accept(): listener fd=%d -> new fd=%d (sock_id=%d)",
           fd, new_fd, new_sock_id);
    return new_fd;
}



int VFS_Connect(int fd, const struct sockaddr_un *addr, uint32_t addrlen)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return (int)serror(EBADF);
    if (!open_files[fd].exists || !open_files[fd].is_socket)
        return (int)serror(ENOTSOCK);
    if (open_files[fd].pid != proc_get_current_pid())
        return (int)serror(EACCES);
    if (!addr || addrlen < sizeof(uint16_t))
        return (int)serror(EINVAL);
    if (addr->sun_family != AF_UNIX)
        return (int)serror(EAFNOSUPPORT);

    
    int rc = unix_sock_connect(open_files[fd].unix_sock_id, addr->sun_path);
    if (rc < 0) {
        log_err("VFS", "VFS_Connect: connect failed for fd=%d path='%s'",
                fd, addr->sun_path);
        return (int)serror(ECONNREFUSED);
    }

    log_ok("VFS", "connect(): fd=%d -> '%s'", fd, addr->sun_path);
    return 0;
}



int VFS_SendTo(int fd, const void *buf, size_t count,
               const struct sockaddr_un *dest)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return (int)serror(EBADF);
    if (!open_files[fd].exists || !open_files[fd].is_socket)
        return (int)serror(ENOTSOCK);
    if (open_files[fd].pid != proc_get_current_pid())
        return (int)serror(EACCES);

    unix_socket_t *s = unix_sock_get(open_files[fd].unix_sock_id);
    if (!s)
        return (int)serror(EBADF);

    if (s->type == SOCK_STREAM) {
        



        return unix_sock_write(open_files[fd].unix_sock_id, buf, count);
    }

    
    if (!dest) {
        log_err("VFS", "VFS_SendTo: DGRAM socket requires a destination");
        return (int)serror(EDESTADDRREQ);
    }
    if (dest->sun_family != AF_UNIX)
        return (int)serror(EAFNOSUPPORT);

    int rc = unix_sock_sendto(open_files[fd].unix_sock_id,
                              buf, count, dest->sun_path);
    if (rc < 0)
        return (int)serror(ENOENT); 
    return rc;
}



int VFS_RecvFrom(int fd, void *buf, size_t count,
                 struct sockaddr_un *src_out)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return (int)serror(EBADF);
    if (!open_files[fd].exists || !open_files[fd].is_socket)
        return (int)serror(ENOTSOCK);
    if (open_files[fd].pid != proc_get_current_pid())
        return (int)serror(EACCES);

    unix_socket_t *s = unix_sock_get(open_files[fd].unix_sock_id);
    if (!s)
        return (int)serror(EBADF);

    if (s->type == SOCK_STREAM) {
        
        return unix_sock_read(open_files[fd].unix_sock_id, buf, count);
    }

    
    char src_path[UNIX_PATH_MAX] = {0};
    int rc = unix_sock_recvfrom(open_files[fd].unix_sock_id,
                                buf, count, src_path);
    if (rc < 0)
        return (int)serror(EIO);

    if (src_out) {
        src_out->sun_family = AF_UNIX;
        strncpy(src_out->sun_path, src_path, UNIX_PATH_MAX - 1);
        src_out->sun_path[UNIX_PATH_MAX - 1] = '\0';
    }

    return rc;
}





static void create_special_files(void)
{
    VFS_Create("/dev", true);

    block_device_t *dev_ramdisk = ramdisk_create_blockdev("ram0", 4 * 1024 * 1024);
    block_register(dev_ramdisk);
    ext2_format(dev_ramdisk);
    VFS_Mount("ram0", "/dev");
    log_ok("VFS", "Created and mounted device ramdisk(/dev, ram0)");

    block_device_t *tmp_ramdisk = ramdisk_create_blockdev("ram1", 4 * 1024 * 1024);
    block_register(tmp_ramdisk);
    ext2_format(tmp_ramdisk);
    VFS_Mount("ram1", "/tmp");
    log_ok("VFS", "Created and mounted tmp ramdisk(/tmp, ram1)");

    stdin_device_init("/dev/stdin");
    stdout_device_init("/dev/stdout");
    if (VFS_Create("/dev/stderr", false) < 0)
        log_err("VFS", "Failed to create special file 4\n This could just be because it already exists");
    if (VFS_Create("/dev/stddbg", false) < 0)
        log_err("VFS", "Failed to create special file 5\n This could just be because it already exists");

    if (VFS_Open("/dev/stdin",  true) < 0) log_err("VFS", "Failed to open special file 1");
    if (VFS_Open("/dev/stdout", true) < 0) log_err("VFS", "Failed to open special file 2");
    if (VFS_Open("/dev/stderr", true) < 0) log_err("VFS", "Failed to open special file 3");
    if (VFS_Open("/dev/stddbg", true) < 0) log_err("VFS", "Failed to open special file 4");

    setwriteall(true, 1);
    setwriteall(true, 2);
    setwriteall(true, 3);
}

void VFS_Init(void)
{
    open_files = kmalloc(MAX_OPEN_FILES * sizeof(VFS_File_t));

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        open_files[i].exists    = false;
        open_files[i].file      = NULL;
        open_files[i].dir_iter  = NULL;
        open_files[i].is_dir    = false;
        open_files[i].fs        = NULL;
        open_files[i].is_socket = false;
    }

    for (int i = 0; i < MAX_MOUNTS; i++)
        mount_table[i].active = false;

    
    unix_sock_init();

    const char* rd = config_get("bootdisk", "iso");
    if (strcmp(rd, "iso") == 0) {
        log_info("VFS", "Using hdd.img as root");

        block_device_t* imgdev = image_create_blockdev("hdaimg", "hdd.img");
        if (!imgdev)
            panic("VFS", "Failed to create block device from hdd.img");
        block_register(imgdev);
        if (!gpt_register_partitions(imgdev))
            panic("VFS", "Failed to parse GPT on hdd.img");
        block_device_t* part = block_get("hdaimgp1");
        if (!part)
            panic("VFS", "Failed to find partition hdaimgp1");
        rootfs = ext2_mount(part);
        if (!rootfs)
            panic("VFS", "Failed to mount ext2 on hdaimgp1");
        rootdrive = part;
        mounted   = true;

        create_special_files();
        return;
    }

    char type[32];
    get_value_by_key(rd, 0, type, sizeof(type));
    if (strcmp(type, "ata") == 0) {
        log_info("VFS", "Using ATA with EXT2");
        log_warn("VFS", "ATA wont work on most systems!");

        ata_init();
        log_ok("VFS", "Started ATA driver");

        block_device_t* bd = ata_create_primary_blockdev("root");
        if (bd == NULL)
            panic("VFS", "Failed to mount root drive");

        log_info("VFS", "Sector size: %d", bd->sector_size);

        ext2_fs_t* efs = ext2_mount(bd);
        if (efs == NULL)
            panic("VFS", "Mounted root drive but failed to mount EXT2 fs");

        rootdrive = bd;
        rootfs    = efs;
        mounted   = true;

        create_special_files();
    }
}

void VFS_Unmount(void)
{
    if (mounted)
        ext2_unmount(rootfs);

    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (mount_table[i].active) {
            ext2_unmount(mount_table[i].fs);
            mount_table[i].active = false;
        }
    }
}

int VFS_Write_old(fd_t file, uint8_t* data, size_t size)
{
    switch (file)
    {
    case VFS_FD_STDIN:
        return 0;
    case VFS_FD_STDOUT:
        for (size_t i = 0; i < size; i++)
            console_putc(data[i]);
        return size;
    case VFS_FD_STDERR:
        for (size_t i = 0; i < size; i++) {
            log_info("CON", "byte 0x%02x", (unsigned)data[i]);
            console_putc(data[i]);
        }
        return size;
    case VFS_FD_DEBUG:
        for (size_t i = 0; i < size; i++)
            e9_putc(data[i]);
        return size;
    default:
        return (int)serror(EBADF);
    }
}

uint64_t sys_execve(uint64_t path, uint64_t argv, uint64_t envp)
{
    const char  *prog = (const char *)path;
    const char **av   = (const char **)argv;
    const char **ev   = (const char **)envp;

    uid_t caller_uid = proc_get_owner(proc_get_current_pid());
    if (!user_exists(caller_uid))
        return serror(EPERM);

    if (!user_is_root(caller_uid)) {
        const char* rel;
        ext2_fs_t*  fs = resolve_fs(prog, &rel);
        if (ext2_access(fs, rel, caller_uid, ACCESS_EXEC) != EXT2_SUCCESS)
            return serror(EACCES);
    }

    int argc = 0;
    int envc = 0;
    if (av) while (av[argc]) argc++;
    if (ev) while (ev[envc]) envc++;

    x86_64_DisableInterrupts();
    int pid = bin_load_elf_argv(prog, 1, proc_get_current_pid(),
                                argc, av, envc, ev);
    if (pid > 0)
        proc_set_owner(pid, caller_uid);
    x86_64_EnableInterrupts();

    return (uint64_t)pid;
}