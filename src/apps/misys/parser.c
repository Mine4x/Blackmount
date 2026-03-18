#include "parser.h"

static char* trim(char* str)
{
    while (*str == ' ' || *str == '\t' || *str == '\n')
        str++;

    char* end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n'))
        *end-- = '\0';

    return str;
}

static char* strip_quotes(char* str)
{
    if (*str == '\'' || *str == '"')
    {
        str++;
        char* end = str + strlen(str) - 1;
        if (*end == '\'' || *end == '"')
            *end = '\0';
    }
    return str;
}

parse_respond parse_and_register_service(const char* path)
{
    int fd = open(path);
    if (fd < 0)
        return NO_FILE;

    char buffer[246];

    int bytes_read = read(fd, buffer, 245);
    if (bytes_read < 0)
    {
        close(fd);
        return MALLOC;
    }

    buffer[bytes_read] = '\0';
    close(fd);

    char* group_name = NULL;
    char* name = NULL;
    char* description = NULL;
    char* exec = NULL;
    char* after_group = NULL;

    char* line = strtok(buffer, "\n");
    while (line)
    {
        line = trim(line);

        if (*line == '\0' || *line == '#')
        {
            line = strtok(NULL, "\n");
            continue;
        }

        char* comment = strchr(line, '#');
        if (comment) *comment = '\0';

        char* eq = strchr(line, '=');
        if (!eq)
        {
            line = strtok(NULL, "\n");
            continue;
        }

        *eq = '\0';
        char* key = trim(line);
        char* value = trim(eq + 1);
        value = strip_quotes(value);

        if (strcmp(key, "group") == 0)
            group_name = value;

        else if (strcmp(key, "name") == 0)
            name = value;

        else if (strcmp(key, "description") == 0)
            description = value;

        else if (strcmp(key, "exec") == 0)
            exec = value;

        else if (strcmp(key, "after") == 0)
        {
            if (strncmp(value, "group[", 6) == 0)
            {
                char* start = value + 6;
                char* end = strchr(start, ']');
                if (end)
                {
                    *end = '\0';
                    after_group = start;
                }
            }
        }

        line = strtok(NULL, "\n");
    }

    if (!group_name || !name || !exec)
        return INVALID;

    group* grp = find_group(group_name);
    if (!grp)
        return INVALID;

    char after_buf[64] = {0};
    if (after_group)
        strcpy(after_buf, after_group);

    manager_register_service(
        grp,
        name,
        description ? description : "",
        exec,
        after_group ? after_buf : NULL
    );

    return OK;
}