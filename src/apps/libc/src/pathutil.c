#include "pathutil.h"

#include <stdio.h>
#include <string.h>

const char *getenv_local(const char *key, char **envp)
{
    for (int i = 0; envp[i]; i++) {
        const char *entry = envp[i];
        int j = 0;
        while (key[j] && entry[j] && key[j] == entry[j])
            j++;
        if (key[j] == '\0' && entry[j] == '=')
            return entry + j + 1;
    }
    return NULL;
}

void normalize_path(char *path)
{
    char tmp[PATH_SIZE];
    strncpy(tmp, path, PATH_SIZE - 1);
    tmp[PATH_SIZE - 1] = '\0';

    char *segs[32];
    int   lens[32];
    int   top = 0;

    char *p = tmp;

    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        char *start = p;
        while (*p && *p != '/') p++;

        int len = p - start;

        if (len == 1 && start[0] == '.') {
            continue;
        } else if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (top > 0) top--;
        } else {
            segs[top] = start;
            lens[top] = len;
            top++;
        }
    }

    char result[PATH_SIZE];
    int  pos = 0;

    if (top == 0) {
        result[pos++] = '/';
    } else {
        for (int i = 0; i < top; i++) {
            result[pos++] = '/';
            for (int j = 0; j < lens[i]; j++)
                result[pos++] = segs[i][j];
        }
    }

    result[pos] = '\0';
    strncpy(path, result, PATH_SIZE - 1);
    path[PATH_SIZE - 1] = '\0';
}
void build_path(char *out, const char *pwd, const char *input,
                const char *home)
{
    if (input[0] == '~' && (input[1] == '/' || input[1] == '\0')) {
        /* Tilde expansion: substitute HOME (fall back to "/"). */
        const char *h = home ? home : "/";
        if (input[1] == '\0') {
            strncpy(out, h, PATH_SIZE - 1);
        } else {
            /* input is "~/…" — skip the '~' so we get "<home>/…". */
            snprintf(out, PATH_SIZE, "%s%s", h, input + 1);
        }
    } else if (input[0] == '/') {
        strncpy(out, input, PATH_SIZE - 1);
    } else {
        snprintf(out, PATH_SIZE, "%s/%s", pwd, input);
    }

    out[PATH_SIZE - 1] = '\0';
    normalize_path(out);
}