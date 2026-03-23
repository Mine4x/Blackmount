#ifndef PARSER_H
#define PARSER_H

#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "manager.h"
#include "log.h"

#define PASSWD_FILE "/etc/passwd"
#define PASSWD_FIELDS 7
#define LINE_BUF 512
#define FILE_BUF 4096

typedef struct {
    char username[64];
    char password[64];
    int  uid;
    int  gid;
    char gecos[128];
    char home[256];
    char shell[256];
} passwd_entry_t;

typedef enum
{
    OK = 0,
    MANAGER_ERROR = -1,
    NO_FILE = -2,
    MEMMORY = -3,
    INVALID = -4,
} parse_respond;

parse_respond parse_and_register_service(const char* path);
parse_respond parse_and_register_group(const char* path);

int user_get_home(int uid, char *buf, size_t len);
int user_get_shell(int uid, char *buf, size_t len);

#endif