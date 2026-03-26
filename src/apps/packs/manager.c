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

int manager_install_package(package_t* pkg)
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