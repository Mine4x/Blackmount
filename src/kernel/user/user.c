#include "user.h"
#include <heap.h>
#include <debug.h>
#include <string.h>

group_t* groups;
user_t* users;

int user_init(void)
{
    groups = kmalloc(sizeof(group_t) * MAX_GROUPS);
    if (!groups)
    {
        log_err(USER_MOD, "Unable to alloctate group buffer");
        return -1;
    }
    for (int i = 0; i < MAX_GROUPS; i++)
    {
        groups[i].exists = false;
    }

    users = kmalloc(sizeof(user_t) * MAX_USERS);
    if (!users)
    {
        log_err(USER_MOD, "Unable to allocate user buffer");
        kfree(groups);
        return -1;
    }
    for (int i = 0; i < MAX_USERS; i++)
    {
        users[i].exists = false;
    }

    perm_flags_t rootGflags = ROOT;
    gid_t rootG = user_create_group("root", rootGflags);
    if (rootG < 0)
    {
        log_err(USER_MOD, "Unable to create root group");
        kfree(users);
        kfree(groups);
        return -1;
    }
    gid_t rootU = user_create_user("root", rootG);
    if (rootU < 0)
    {
        log_err(USER_MOD, "Unable to create root user");
        kfree(users);
        kfree(groups);
        return -1;
    }


    return 0;
}

gid_t user_create_group(const char* name, perm_flags_t perms)
{
    int i = 0;
    for (; i < MAX_GROUPS; i++)
    {
        if (!groups[i].exists)
            break;
    }
    if (i >= MAX_GROUPS)
    {
        log_err(USER_MOD, "Unable to find free group slot");
        return -1;
    }

    groups[i].exists = true;
    groups[i].gid = i;
    groups[i].perms = perms;
    groups[i].groupname = strdup(name);

    return i;
}

uid_t user_create_user(const char* name, int gid)
{
    if (gid > MAX_GROUPS)
        return -1;
    
    int i = 0;
    for (; i < MAX_USERS; i++)
    {
        if (!users[i].exists)
            break;
    }
    if (i >= MAX_USERS)
    {
        log_err(USER_MOD, "Unable to find free user slot");
        return -1;
    }

    users[i].exists = true;
    users[i].group = gid;
    users[i].uid = i;
    users[i].username = strdup(name);

    return i;
}

bool user_check_perms(uid_t reqU, uid_t recU)
{
    if (reqU == recU)
        return true;

    if (reqU >= MAX_USERS || recU >= MAX_USERS)
        return false;

    if (!users[reqU].exists || !users[recU].exists)
        return false;

    gid_t reqG = users[reqU].group;
    gid_t recG = users[recU].group;

    if (reqG >= MAX_GROUPS || recG >= MAX_GROUPS)
        return false;

    perm_flags_t reqP = groups[reqG].perms;
    perm_flags_t recP = groups[recG].perms;

    return reqP <= recP;
}

bool user_exists(uid_t uid)
{
    if (uid < 0 || uid >= MAX_USERS)
        return false;
    return users[uid].exists;
}

gid_t user_get_gid(uid_t uid)
{
    if (!user_exists(uid))
        return -1;
    return users[uid].group;
}

bool user_is_root(uid_t uid)
{
    if (!user_exists(uid))
        return false;
    gid_t gid = users[uid].group;
    if (gid < 0 || gid >= MAX_GROUPS || !groups[gid].exists)
        return false;
    return groups[gid].perms == ROOT;
}

perm_flags_t user_get_perm_level(uid_t uid)
{
    if (!user_exists(uid))
        return USER;
    gid_t gid = users[uid].group;
    if (gid < 0 || gid >= MAX_GROUPS || !groups[gid].exists)
        return USER;
    return groups[gid].perms;
}