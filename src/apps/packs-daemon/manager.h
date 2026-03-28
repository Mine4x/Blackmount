#ifndef MANAGER_H
#define MANAGER_H

#define MAIN_DIR       "/etc/packs"
#define INSTALLED_DIR  "/etc/packs/installed"
#define PACKAGES_DB    "/etc/packs/packages.db"
#define MAX_PACKS      10000


#define DT_DIR 4

#include "types.h"


int manager_init();


int        manager_install_package(package_t* pkg, const char* src_pakpath);
int        manager_remove_package(const char* name);
package_t* find_pkg(const char* name);


int manager_add_package(package_t* pkg);


int manager_list_packages(char* buf, int buf_size);


int manager_package_info(const char* name, char* buf, int buf_size);


int manager_save_packages();
int manager_load_packages();

#endif 