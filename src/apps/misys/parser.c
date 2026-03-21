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

parse_respond parse_and_register_group(const char* path)
{
    int fd = open(path);
    if (fd < 0)
        return NO_FILE;

    char buffer[246];

    int bytes_read = read(fd, buffer, 245);
    if (bytes_read < 0)
    {
        close(fd);
        return MEMMORY;
    }

    buffer[bytes_read] = '\0';
    close(fd);

    char *name = NULL;

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

        if (strcmp(key, "name") == 0)
            name = value;

        line = strtok(NULL, "\n");
    }

    if (!name)
        return INVALID;

    int r = manager_register_group(name);
    if (r < 0)
        return MANAGER_ERROR;

    return OK;
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
        return MEMMORY;
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

    int r = manager_register_service(
        grp,
        name,
        description ? description : "",
        exec,
        after_group ? after_buf : NULL
    );

    if (r < 0)
        return MANAGER_ERROR;

    return OK;
}

static int parse_passwd_line(char *line, passwd_entry_t *out)
{
    char *fields[PASSWD_FIELDS];
    int i = 0;

    fields[i++] = line;
    while (*line && i < PASSWD_FIELDS) {
        if (*line == ':') {
            *line = '\0';
            fields[i++] = line + 1;
        }
        line++;
    }

    if (i < PASSWD_FIELDS)
        return -1;

    strncpy(out->username, fields[0], sizeof(out->username) - 1);
    strncpy(out->password, fields[1], sizeof(out->password) - 1);
    out->uid = atoi(fields[2]);
    out->gid = atoi(fields[3]);
    strncpy(out->gecos, fields[4], sizeof(out->gecos) - 1);
    strncpy(out->home,  fields[5], sizeof(out->home)  - 1);
    strncpy(out->shell, fields[6], sizeof(out->shell) - 1);

    char *nl = strchr(out->shell, '\n');
    if (nl) *nl = '\0';

    return 0;
}

static int find_passwd_entry(int uid, passwd_entry_t *out)
{
    int fd = open(PASSWD_FILE);
    if (fd < 0) {
        log_fail("user: failed to open /etc/passwd");
        return -1;
    }

    char *raw = malloc(FILE_BUF);
    if (!raw) {
        close(fd);
        return -1;
    }

    int nread = read(fd, raw, FILE_BUF - 1);
    close(fd);

    if (nread <= 0) {
        free(raw);
        return -1;
    }

    raw[nread] = '\0';

    passwd_entry_t *entry = malloc(sizeof(passwd_entry_t));
    if (!entry) {
        free(raw);
        return -1;
    }

    char *line = raw;
    char *end;
    int found = 0;

    while (line && *line) {
        end = strchr(line, '\n');
        if (end)
            *end = '\0';

        if (line[0] != '#' && line[0] != '\0') {
            char *tmp = malloc(LINE_BUF);
            if (!tmp)
                break;

            strncpy(tmp, line, LINE_BUF - 1);
            tmp[LINE_BUF - 1] = '\0';

            memset(entry, 0, sizeof(passwd_entry_t));

            if (parse_passwd_line(tmp, entry) == 0 && entry->uid == uid) {
                memcpy(out, entry, sizeof(passwd_entry_t));
                found = 1;
                free(tmp);
                break;
            }

            free(tmp);
        }

        line = end ? end + 1 : NULL;
    }

    free(raw);
    free(entry);
    return found ? 0 : -1;
}

int user_get_home(int uid, char *buf, size_t len)
{
    if (!buf || len == 0)
        return -1;

    passwd_entry_t *entry = malloc(sizeof(passwd_entry_t));
    if (!entry)
        return -1;

    memset(entry, 0, sizeof(passwd_entry_t));

    if (find_passwd_entry(uid, entry) < 0) {
        log_fail("user_get_home: no passwd entry found");
        free(entry);
        return -1;
    }

    strncpy(buf, entry->home, len - 1);
    buf[len - 1] = '\0';
    free(entry);
    return 0;
}

int user_get_shell(int uid, char *buf, size_t len)
{
    if (!buf || len == 0)
        return -1;

    passwd_entry_t *entry = malloc(sizeof(passwd_entry_t));
    if (!entry)
        return -1;

    memset(entry, 0, sizeof(passwd_entry_t));

    if (find_passwd_entry(uid, entry) < 0) {
        log_fail("user_get_shell: no passwd entry found");
        free(entry);
        return -1;
    }

    strncpy(buf, entry->shell, len - 1);
    buf[len - 1] = '\0';
    free(entry);
    return 0;
}