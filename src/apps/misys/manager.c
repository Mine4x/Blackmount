#include "manager.h"

group** groups;
int group_count;

service** services;
int service_count;

int manager_init(void)
{
    groups = zalloc(sizeof(group*) * MAX_GROUPS);
    if (!groups) return -1;

    services = zalloc(sizeof(service*) * MAX_SERVICES);
    if (!services) { free(groups); return -1; }

    service_count = 0;
    group_count = 0;
    return 0;
}

int manager_exec_all()
{
    for (int i = 0; i < service_count; i++)
    {
        char* msg = zalloc(sizeof(char) * 256);

        if (!services[i] || !services[i]->name || !services[i]->exec) {
            snprintf(msg, 256, "Unable to start unknown service(index %d)", i);
            log_fail(msg);
            free(msg);
            continue;
        }

        snprintf(msg, 256, "Unable to start service %s", services[i]->name);

        if (binrun(services[i]->exec) < 0) {
            log_fail(msg);
            free(msg);
            continue;
        }

        snprintf(msg, 256, "Started service '%s'", services[i]->name);
        log_ok(msg);
        free(msg);
    }
    return 0;
}

int manager_register_group(const char* name)
{
    if (group_count == MAX_GROUPS || group_exists(name))
        return -1;

    group* g = malloc(sizeof(group));
    if (!g) return -1;

    strcpy(g->name, name);

    groups[group_count] = g;
    group_count++;

    return 0;
}

int manager_register_service(group* grp, const char* name, const char* description, const char* exec, char* after_names)
{
    if (service_count == MAX_SERVICES || service_exists(name))
        return -1;

    service* s = malloc(sizeof(service));
    if (!s) return -1;

    s->group = grp;
    s->name = strdup(name);
    s->description = strdup(description);
    s->exec = strdup(exec);

    s->after_count = 0;
    s->after = NULL;

    if (after_names && strlen(after_names) > 0)
    {
        size_t count = 1;
        for (char* p = after_names; *p; p++)
            if (*p == ',') count++;

        s->after = malloc(sizeof(group*) * count);

        char* token = strtok(after_names, ",");
        while (token)
        {
            while (*token == ' ') token++;
            char* end = token + strlen(token) - 1;
            while (end > token && (*end == ' ' || *end == '\n')) *end-- = '\0';

            for (int i = 0; i < group_count; i++)
            {
                if (strcmp(groups[i]->name, token) == 0)
                {
                    s->after[s->after_count++] = groups[i];
                    break;
                }
            }

            token = strtok(NULL, ",");
        }
    }

    services[service_count++] = s;

    return 0;
}

void manager_free()
{
    for (int i = 0; i < group_count; i++)
    {
        free(groups[i]);
    }

    free(groups);

    group_count = 0;

    for (int i = 0; i < service_count; i++)
    {
        free(services[i]);
    }

    free(services);

    service_count = 0;
}

group* find_group(const char* name)
{
    for (int i = 0; i < group_count; i++)
    {
        if (strcmp(groups[i]->name, name) == 0)
            return groups[i];
    }
    return NULL;
}

bool group_exists(const char* name)
{
    for (int i = 0; i < group_count; i++)
    {
        if (strcmp(groups[i]->name, name) == 0)
            return true;
    }
    return false;
}

bool service_exists(const char* name)
{
    for (int i = 0; i < service_count; i++)
    {
        if (strcmp(services[i]->name, name) == 0)
            return true;
    }
    return false;
}