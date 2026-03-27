#include "manager.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"

/*
 * TODO:
 * Move the package into MAIN_DIR/packages
 * For now just storing the location will work though
 */

package_t** packages;

int manager_init()
{
    packages = zalloc(sizeof(package_t*) * MAX_PACKS);
    if (packages == NULL) {return -1;}

    int main_fd = open(MAIN_DIR);
    if (main_fd < 0)
    {
        int r = create(MAIN_DIR, true);
        if (r < 0) {return -1;}
    }
}

static int find_free_package()
{
    for (int i = 0; i < MAX_PACKS; i++)
    {
        if (packages[i] == NULL) {continue;}
        return i;
    }

    return -1;
}

int manager_add_package(package_t* pkg)
{
    int pack_i = find_free_package();
    if (pack_i < 0) {return -1;}

    packages[pack_i] = pkg;

    return 0;
}

package_t* find_pkg(const char* name)
{
    for (int i = 0; i < MAX_PACKS; i++)
    {
        if (packages[i] == NULL) {continue;}
        if (strcmp(packages[i]->name, name) == 0) {return packages[i];}
    }

    return NULL;
}


int manager_save_packages()
{
    int fd = open(PACKAGES_FILE);
    if (fd < 0) return -1;

    for (int i = 0; i < MAX_PACKS; i++)
    {
        package_t* p = packages[i];
        if (!p) continue;

        int name_len = strlen(p->name);
        int loc_len  = strlen(p->location);
        int ver_len  = strlen(p->str_ver);

        write(fd, &name_len, sizeof(int));
        write(fd, p->name, name_len);

        write(fd, &loc_len, sizeof(int));
        write(fd, p->location, loc_len);

        write(fd, &p->int_ver, sizeof(int));

        write(fd, &ver_len, sizeof(int));
        write(fd, p->str_ver, ver_len);

        write(fd, &p->type, sizeof(int));
    }

    close(fd);
    return 0;
}

int manager_load_packages()
{
    int fd = open(PACKAGES_FILE);
    if (fd < 0) return -1;

    while (1)
    {
        package_t* p = zalloc(sizeof(package_t));
        if (!p) break;

        int name_len;
        int r = read(fd, &name_len, sizeof(int));
        if (r != sizeof(int))
        {
            free(p);
            break; // EOF
        }

        p->name = zalloc(name_len + 1);
        read(fd, p->name, name_len);

        int loc_len;
        read(fd, &loc_len, sizeof(int));
        p->location = zalloc(loc_len + 1);
        read(fd, p->location, loc_len);

        read(fd, &p->int_ver, sizeof(int));

        int ver_len;
        read(fd, &ver_len, sizeof(int));
        p->str_ver = zalloc(ver_len + 1);
        read(fd, p->str_ver, ver_len);

        read(fd, &p->type, sizeof(int));

        manager_install_package(p);
    }

    close(fd);
    return 0;
}