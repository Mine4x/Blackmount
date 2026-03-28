#include "manager.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "types.h"





static package_t** packages;









static int open_or_create_file(const char* path)
{
    int fd = (int)open(path);
    if (fd >= 0) return fd;

    
    int cfd = (int)create(path, false);
    if (cfd < 0) return -1;

    
    if (cfd > 0) return cfd;

    
    close((uint64_t)cfd);
    return (int)open(path);
}





static int ensure_dir(const char* path)
{
    int fd = (int)open(path);
    if (fd >= 0)
    {
        close((uint64_t)fd);
        return 0;
    }
    int r = (int)create(path, true);
    return (r < 0) ? -1 : 0;
}





static int copy_fd_data(int src_fd, int dst_fd)
{
    char buf[4096];
    ssize_t n;

    while ((n = (ssize_t)read((uint64_t)src_fd, buf, sizeof(buf))) > 0)
    {
        size_t written = 0;
        while ((ssize_t)written < n)
        {
            ssize_t w = (ssize_t)write((uint64_t)dst_fd,
                                       buf + written,
                                       (size_t)(n - (ssize_t)written));
            if (w <= 0) return -1;
            written += (size_t)w;
        }
    }
    return (n < 0) ? -1 : 0;
}





static int copy_file(const char* src_path, const char* dst_path)
{
    int src_fd = (int)open(src_path);
    if (src_fd < 0) return -1;

    unlink(dst_path); 

    int dst_fd = open_or_create_file(dst_path);
    if (dst_fd < 0) { close((uint64_t)src_fd); return -1; }

    int ret = copy_fd_data(src_fd, dst_fd);

    close((uint64_t)src_fd);
    close((uint64_t)dst_fd);
    return ret;
}





static int copy_directory_flat(const char* src_dir, const char* dst_dir)
{
    int dir_fd = (int)open(src_dir);
    if (dir_fd < 0) return -1;

    char dirbuf[4096];
    ssize_t nread;
    int ret = 0;

    while ((nread = (ssize_t)getdents64((uint64_t)dir_fd,
                                         (struct linux_dirent64*)dirbuf,
                                         sizeof(dirbuf))) > 0)
    {
        ssize_t off = 0;
        while (off < nread)
        {
            struct linux_dirent64* d =
                (struct linux_dirent64*)(dirbuf + off);
            off += d->d_reclen;

            if (strcmp(d->d_name, ".") == 0 ||
                strcmp(d->d_name, "..") == 0) continue;

            
            if (d->d_type == DT_DIR) continue;

            char src_file[512];
            char dst_file[512];
            snprintf(src_file, sizeof(src_file), "%s/%s", src_dir, d->d_name);
            snprintf(dst_file, sizeof(dst_file), "%s/%s", dst_dir, d->d_name);

            if (copy_file(src_file, dst_file) < 0)
            {
                errorf("packs: failed to copy '%s'\n", src_file);
                ret = -1;
            }
        }
    }

    close((uint64_t)dir_fd);
    return ret;
}




static void remove_directory(const char* path)
{
    int dir_fd = (int)open(path);
    if (dir_fd < 0) return;

    char dirbuf[4096];
    ssize_t nread;

    while ((nread = (ssize_t)getdents64((uint64_t)dir_fd,
                                         (struct linux_dirent64*)dirbuf,
                                         sizeof(dirbuf))) > 0)
    {
        ssize_t off = 0;
        while (off < nread)
        {
            struct linux_dirent64* d =
                (struct linux_dirent64*)(dirbuf + off);
            off += d->d_reclen;

            if (strcmp(d->d_name, ".") == 0 ||
                strcmp(d->d_name, "..") == 0) continue;

            char child[512];
            snprintf(child, sizeof(child), "%s/%s", path, d->d_name);

            if (d->d_type == DT_DIR)
                remove_directory(child);
            else
                unlink(child);
        }
    }

    close((uint64_t)dir_fd);
    rmdir(path);
}












static int install_system_files(package_t* pkg, const char* pkg_dir)
{
    
    if (!pkg->install_spec || pkg->install_spec[0] == '\0')
    {
        if (!pkg->related_files)
            pkg->related_files = strdup("");
        return 0;
    }

    char  related[4096];
    int   related_len = 0;
    related[0] = '\0';

    char* spec = strdup(pkg->install_spec);
    if (!spec) return -1;

    char* entry = strtok(spec, ";");
    while (entry)
    {
        char* colon = strchr(entry, ':');
        if (colon)
        {
            *colon = '\0';
            const char* src_name = entry;
            const char* dst_path = colon + 1;

            char src_path[512];
            snprintf(src_path, sizeof(src_path), "%s/%s", pkg_dir, src_name);

            if (copy_file(src_path, dst_path) < 0)
            {
                errorf("packs: failed to install '%s' -> '%s'\n",
                       src_name, dst_path);
                free(spec);
                return -1;
            }

            
            if (related_len > 0 &&
                related_len < (int)sizeof(related) - 1)
            {
                related[related_len++] = ';';
            }
            int dlen = (int)strlen(dst_path);
            if (related_len + dlen < (int)sizeof(related) - 1)
            {
                memcpy(related + related_len, dst_path, (size_t)dlen);
                related_len += dlen;
                related[related_len] = '\0';
            }
        }

        entry = strtok(NULL, ";");
    }

    free(spec);

    if (pkg->related_files) free(pkg->related_files);
    pkg->related_files = strdup(related);
    return 0;
}













int manager_save_packages()
{
    
    unlink(PACKAGES_DB);

    int fd = open_or_create_file(PACKAGES_DB);
    if (fd < 0)
    {
        errorf("packs: cannot open packages database for writing\n");
        return -1;
    }

    for (int i = 0; i < MAX_PACKS; i++)
    {
        package_t* p = packages[i];
        if (!p) continue;

        const char* name = p->name          ? p->name          : "";
        const char* rela = p->related_files  ? p->related_files  : "";
        const char* loc  = p->location       ? p->location       : "";
        const char* strv = p->str_ver        ? p->str_ver        : "";

        int name_len = (int)strlen(name);
        int rela_len = (int)strlen(rela);
        int loc_len  = (int)strlen(loc);
        int ver_len  = (int)strlen(strv);

        write((uint64_t)fd, &name_len,   sizeof(int));
        write((uint64_t)fd, name,        (size_t)name_len);

        write((uint64_t)fd, &rela_len,   sizeof(int));
        write((uint64_t)fd, rela,        (size_t)rela_len);

        write((uint64_t)fd, &loc_len,    sizeof(int));
        write((uint64_t)fd, loc,         (size_t)loc_len);

        write((uint64_t)fd, &p->int_ver, sizeof(int));

        write((uint64_t)fd, &ver_len,    sizeof(int));
        write((uint64_t)fd, strv,        (size_t)ver_len);

        write((uint64_t)fd, &p->type,    sizeof(int));
    }

    close((uint64_t)fd);
    return 0;
}

int manager_load_packages()
{
    int fd = (int)open(PACKAGES_DB);
    if (fd < 0) return 0; 

    while (1)
    {
        package_t* p = zalloc(sizeof(package_t));
        if (!p) break;

        
        int name_len = 0;
        if ((ssize_t)read((uint64_t)fd, &name_len, sizeof(int)) != (ssize_t)sizeof(int))
        {
            free(p);
            break; 
        }
        p->name = zalloc((size_t)name_len + 1);
        if (!p->name) { free(p); break; }
        read((uint64_t)fd, p->name, (size_t)name_len);

        

        int rela_len = 0;
        read((uint64_t)fd, &rela_len, sizeof(int));
        p->related_files = zalloc((size_t)rela_len + 1);
        if (p->related_files) read((uint64_t)fd, p->related_files, (size_t)rela_len);

        
        int loc_len = 0;
        read((uint64_t)fd, &loc_len, sizeof(int));
        p->location = zalloc((size_t)loc_len + 1);
        if (p->location) read((uint64_t)fd, p->location, (size_t)loc_len);

        
        read((uint64_t)fd, &p->int_ver, sizeof(int));

        
        int ver_len = 0;
        read((uint64_t)fd, &ver_len, sizeof(int));
        p->str_ver = zalloc((size_t)ver_len + 1);
        if (p->str_ver) read((uint64_t)fd, p->str_ver, (size_t)ver_len);

        
        read((uint64_t)fd, &p->type, sizeof(int));

        if (manager_add_package(p) < 0)
        {
            
            free(p->name);
            free(p->related_files);
            free(p->location);
            free(p->str_ver);
            free(p);
            break;
        }
    }

    close((uint64_t)fd);
    return 0;
}





int manager_init()
{
    packages = zalloc(sizeof(package_t*) * MAX_PACKS);
    if (!packages) return -1;

    if (ensure_dir(MAIN_DIR)      < 0) return -1;
    if (ensure_dir(INSTALLED_DIR) < 0) return -1;

    manager_load_packages();
    return 0;
}






static int find_free_slot()
{
    for (int i = 0; i < MAX_PACKS; i++)
    {
        if (packages[i] == NULL) return i;
    }
    return -1; 
}

int manager_add_package(package_t* pkg)
{
    int slot = find_free_slot();
    if (slot < 0) return -1;
    packages[slot] = pkg;
    return 0;
}








int manager_install_package(package_t* pkg, const char* src_pakpath)
{
    if (!pkg || !src_pakpath) return -1;

    if (find_pkg(pkg->name))
    {
        errorf("packs: '%s' is already installed\n", pkg->name);
        return -1;
    }

    
    char installed_path[512];
    snprintf(installed_path, sizeof(installed_path),
             "%s/%s", INSTALLED_DIR, pkg->name);

    if (ensure_dir(installed_path) < 0)
    {
        errorf("packs: cannot create install directory '%s'\n", installed_path);
        return -1;
    }

    
    if (copy_directory_flat(src_pakpath, installed_path) < 0)
    {
        errorf("packs: failed to copy package directory\n");
        remove_directory(installed_path);
        return -1;
    }

    
    if (pkg->location) free(pkg->location);
    pkg->location = strdup(installed_path);

    
    if (install_system_files(pkg, installed_path) < 0)
    {
        errorf("packs: failed to install system files for '%s'\n", pkg->name);
        remove_directory(installed_path);
        return -1;
    }

    
    if (manager_add_package(pkg) < 0)
    {
        errorf("packs: package registry is full\n");
        remove_directory(installed_path);
        return -1;
    }

    manager_save_packages();
    return 0;
}







int manager_remove_package(const char* name)
{
    for (int i = 0; i < MAX_PACKS; i++)
    {
        if (!packages[i]) continue;
        if (strcmp(packages[i]->name, name) != 0) continue;

        package_t* p = packages[i];

        
        if (p->related_files && p->related_files[0] != '\0')
        {
            char* spec = strdup(p->related_files);
            if (spec)
            {
                char* path = strtok(spec, ";");
                while (path)
                {
                    if (unlink(path) < 0)
                        errorf("packs: warning — could not remove '%s'\n", path);
                    path = strtok(NULL, ";");
                }
                free(spec);
            }
        }

        
        if (p->location && p->location[0] != '\0')
            remove_directory(p->location);

        
        if (p->name)          free(p->name);
        if (p->location)      free(p->location);
        if (p->related_files) free(p->related_files);
        if (p->install_spec)  free(p->install_spec);
        if (p->str_ver)       free(p->str_ver);
        free(p);

        packages[i] = NULL;

        manager_save_packages();
        return 0;
    }

    return -1; 
}

package_t* find_pkg(const char* name)
{
    for (int i = 0; i < MAX_PACKS; i++)
    {
        if (!packages[i]) continue;
        if (strcmp(packages[i]->name, name) == 0) return packages[i];
    }
    return NULL;
}





int manager_list_packages(char* buf, int buf_size)
{
    if (!buf || buf_size <= 0) return 0;

    
    int written = snprintf(buf, (size_t)buf_size,
                           "%-24s  %-12s  %-10s  %s\n"
                           "%-24s  %-12s  %-10s  %s\n",
                           "NAME", "VERSION", "TYPE", "LOCATION",
                           "----", "-------", "----", "--------");
    if (written < 0) return 0;

    for (int i = 0; i < MAX_PACKS; i++)
    {
        if (!packages[i]) continue;
        package_t* p = packages[i];

        const char* type_str = (p->type == SERVICE) ? "service" : "executable";
        int n = snprintf(buf + written, (size_t)(buf_size - written),
                         "%-24s  %-12s  %-10s  %s\n",
                         p->name ? p->name : "(unknown)",
                         p->str_ver ? p->str_ver : "?",
                         type_str,
                         p->location ? p->location : "?");
        if (n <= 0 || written + n >= buf_size) break;
        written += n;
    }

    return written;
}





int manager_package_info(const char* name, char* buf, int buf_size)
{
    package_t* p = find_pkg(name);
    if (!p) return -1;

    const char* type_str = (p->type == SERVICE) ? "service" : "executable";

    return snprintf(buf, (size_t)buf_size,
                    "Name:     %s\n"
                    "Type:     %s\n"
                    "Version:  %s  (build %d)\n"
                    "Location: %s\n"
                    "Files:    %s\n",
                    p->name     ? p->name     : "(unknown)",
                    type_str,
                    p->str_ver  ? p->str_ver  : "?",
                    p->int_ver,
                    p->location ? p->location : "(unknown)",
                    (p->related_files && p->related_files[0] != '\0')
                        ? p->related_files : "(none)");
}