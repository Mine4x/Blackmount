#ifndef USER_H
#define USER_H

#define MAX_GROUPS 124
#define MAX_USERS 124

#define GID_ROOT 0
#define UID_ROOT 0

#define USER_MOD "User"

#include <stdbool.h>

typedef int uid_t;
typedef int gid_t;

// The lower the perm number, the higer the actuall perms
typedef enum
{
    ROOT = 0,
    SUDO = 1,
    USER = 2,
} perm_flags_t;

typedef struct group
{
    const char* groupname;
    gid_t gid;
    perm_flags_t perms;
    bool exists;
} group_t;


typedef struct user
{
    uid_t uid;
    gid_t group;
    const char* username;
    bool exists;
} user_t;

int user_init(void);
gid_t user_create_group(const char* name, perm_flags_t perms);
uid_t user_create_user(const char* name, int gid);

bool user_check_perms(uid_t userA, uid_t userB);

// Lookup helpers used by the ext2 permission layer
bool         user_exists(uid_t uid);
gid_t        user_get_gid(uid_t uid);
bool         user_is_root(uid_t uid);
perm_flags_t user_get_perm_level(uid_t uid);

#endif