#include "parser.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "types.h"

package_t* parse_package(const char* pack_path)
{
    int path_fd = open(pack_path);
    if (path_fd < 0) {return NULL;} else {close(path_fd);}

    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/package.conf", pack_path);

    int conf_fd = open(config_path);
    if (conf_fd < 0) return NULL;

    char buffer[2048];
    int bytes_read = read(conf_fd, buffer, sizeof(buffer) - 1);
    close(conf_fd);

    if (bytes_read <= 0) return NULL;
    buffer[bytes_read] = '\0';

    package_t* pkg = calloc(1, sizeof(package_t));
    if (!pkg) return NULL;

    pkg->location = strdup(pack_path);

    bool found_name = false;
    bool found_type = false;
    bool found_strv = false;
    bool found_intv = false;

    char* line = strtok(buffer, "\n");
    while (line)
    {
        if (strncmp(line, "name=", 5) == 0)
        {
            pkg->name = strdup(line + 5);
            found_name = true;
        }
        else if (strncmp(line, "type=", 5) == 0)
        {
            pkg->type = atoi(line + 5);
            found_type = true;
        }
        else if (strncmp(line, "strv=", 5) == 0)
        {
            pkg->str_ver = strdup(line + 5);
            found_strv = true;
        }
        else if (strncmp(line, "intv=", 5) == 0)
        {
            pkg->int_ver = atoi(line + 5);
            found_intv = true;
        }

        line = strtok(NULL, "\n");
    }

    if (!found_intv || !found_name || !found_strv || !found_type)
    {
        return NULL;
    }

    return pkg;
}