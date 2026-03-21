#ifndef USER_H
#define USER_H

#define MAX_GROUPS       124
#define MAX_USERS        124

#define GID_ROOT         0
#define UID_ROOT         0

#define USER_MOD         "User"

#define SHADOW_HASH_LEN  128
#define USER_HOME_LEN    128
#define USER_SHELL_LEN   64

#include <stdbool.h>

typedef int uid_t;
typedef int gid_t;

typedef enum
{
    ROOT = 0,
    SUDO = 1,
    USER = 2,
} perm_flags_t;

typedef struct group
{
    const char*  groupname;
    gid_t        gid;
    perm_flags_t perms;
    bool         exists;
} group_t;

typedef struct user
{
    uid_t uid;
    gid_t group;
    const char* username;
    char        password_hash[SHADOW_HASH_LEN];
    char        home[USER_HOME_LEN];
    char        shell[USER_SHELL_LEN];
    bool        exists;
} user_t;

int   user_init(void);
gid_t user_create_group(const char* name, perm_flags_t perms);
uid_t user_create_user(const char* name, gid_t gid);
uid_t user_create_user_full(const char* name, gid_t gid,
                            const char* home, const char* shell);

bool         user_check_perms(uid_t userA, uid_t userB);
bool         user_exists(uid_t uid);
gid_t        user_get_gid(uid_t uid);
bool         user_is_root(uid_t uid);
perm_flags_t user_get_perm_level(uid_t uid);
uid_t        user_find_by_name(const char* username);
gid_t        group_find_by_name(const char* name);

int   user_set_password(uid_t uid, const char* password);
uid_t user_authenticate(const char* username, const char* password);

int user_save_to_disk(void);
int user_load_from_disk(void);

#endif