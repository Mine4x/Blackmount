#ifndef MANAGER_H
#define MANAGER_H

#define MAIN_DIR "/etc/packs"
#define MAX_PACKS 10000

#include "types.h"

int manager_init();
int manager_install_package(package_t* pkg);
package_t* find_pkg(const char* name);

#endif