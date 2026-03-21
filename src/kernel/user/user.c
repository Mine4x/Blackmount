#include "user.h"
#include "sha256.h"
#include <heap.h>
#include <debug.h>
#include <string.h>
#include <drivers/fs/ext/ext2.h>

group_t* groups;
user_t*  users;

extern ext2_fs_t* rootfs;
extern bool       mounted;

static uint64_t salt_state = 0xdeadbeefcafe1234ULL;

static void salt_seed(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    salt_state = ((uint64_t)hi << 32) | lo;
    if (salt_state == 0)
        salt_state = 0xdeadbeefcafe1234ULL;
}

static uint64_t gen_salt(void)
{
    salt_state ^= salt_state << 13;
    salt_state ^= salt_state >> 7;
    salt_state ^= salt_state << 17;
    return salt_state;
}

static uint8_t hex_byte(const char* s)
{
    uint8_t v = 0;
    for (int i = 0; i < 2; i++) {
        v <<= 4;
        char c = s[i];
        if      (c >= '0' && c <= '9') v |= (uint8_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint8_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint8_t)(c - 'A' + 10);
    }
    return v;
}

static int u_strncmp(const char* a, const char* b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if ((unsigned char)a[i] != (unsigned char)b[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0')
            return 0;
    }
    return 0;
}

static size_t u_strlcpy(char* dst, const char* src, size_t size)
{
    if (!size) return strlen(src);
    size_t i = 0;
    for (; i + 1 < size && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
    return i;
}

static size_t u_itoa(char* buf, size_t bufsz, int v)
{
    if (!bufsz) return 0;
    if (v < 0) {
        if (bufsz < 2) { buf[0] = '\0'; return 0; }
        buf[0] = '-';
        return 1 + u_itoa(buf + 1, bufsz - 1, -v);
    }
    char tmp[24];
    int  len = 0;
    if (v == 0) { tmp[len++] = '0'; }
    else { unsigned uv = (unsigned)v; while (uv) { tmp[len++] = '0' + (uv % 10); uv /= 10; } }
    size_t out = 0;
    for (int i = len - 1; i >= 0 && out + 1 < bufsz; i--)
        buf[out++] = tmp[i];
    buf[out] = '\0';
    return out;
}

static size_t u_fmt(char* dst, size_t dstsz, const char* fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);

    size_t pos = 0;

    while (*fmt && pos + 1 < dstsz) {
        if (*fmt != '%') {
            dst[pos++] = *fmt++;
            continue;
        }
        fmt++;
        if (*fmt == 's') {
            const char* s = __builtin_va_arg(ap, const char*);
            if (!s) s = "(null)";
            while (*s && pos + 1 < dstsz)
                dst[pos++] = *s++;
        } else if (*fmt == 'd' || *fmt == 'u') {
            int v = __builtin_va_arg(ap, int);
            char tmp[24];
            size_t n = u_itoa(tmp, sizeof(tmp), v);
            for (size_t i = 0; i < n && pos + 1 < dstsz; i++)
                dst[pos++] = tmp[i];
        }
        fmt++;
    }
    dst[pos] = '\0';

    __builtin_va_end(ap);
    return pos;
}

static int simple_atoi(const char* s)
{
    int v = 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return v;
}

static int split_fields(char* line, char** fields, int max_fields)
{
    int count = 0;
    fields[count++] = line;
    while (*line && count < max_fields) {
        if (*line == ':') {
            *line = '\0';
            fields[count++] = line + 1;
        }
        line++;
    }
    return count;
}

static char* read_full_file(const char* path)
{
    if (!rootfs)
        return NULL;

    ext2_file_t* f = ext2_open(rootfs, path);
    if (!f)
        return NULL;

    uint64_t size = ext2_size(f);
    if (size == 0 || size > 65536) {
        ext2_close(f);
        return NULL;
    }

    char* buf = kmalloc((size_t)size + 1);
    if (!buf) {
        ext2_close(f);
        return NULL;
    }

    ext2_read(f, buf, (uint32_t)size);
    ext2_close(f);
    buf[size] = '\0';
    return buf;
}

static int write_full_file(const char* path, const char* buf, size_t len)
{
    if (!rootfs)
        return -1;

    ext2_delete(rootfs, path);

    if (ext2_create(rootfs, path, 0640) != EXT2_SUCCESS)
        return -1;

    ext2_file_t* f = ext2_open(rootfs, path);
    if (!f)
        return -1;

    int r = ext2_write(f, buf, (uint32_t)len);
    ext2_close(f);
    return (r >= 0) ? 0 : -1;
}

int user_init(void)
{
    salt_seed();

    groups = kmalloc(sizeof(group_t) * MAX_GROUPS);
    if (!groups) {
        log_err(USER_MOD, "Unable to allocate group buffer");
        return -1;
    }
    for (int i = 0; i < MAX_GROUPS; i++)
        groups[i].exists = false;

    users = kmalloc(sizeof(user_t) * MAX_USERS);
    if (!users) {
        log_err(USER_MOD, "Unable to allocate user buffer");
        kfree(groups);
        return -1;
    }
    for (int i = 0; i < MAX_USERS; i++)
        users[i].exists = false;

    gid_t rootG = user_create_group("root", ROOT);
    if (rootG < 0) {
        log_err(USER_MOD, "Unable to create root group");
        kfree(users);
        kfree(groups);
        return -1;
    }

    uid_t rootU = user_create_user_full("root", rootG, "/root", "/bin/sh");
    if (rootU < 0) {
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
    for (; i < MAX_GROUPS; i++) {
        if (!groups[i].exists)
            break;
    }
    if (i >= MAX_GROUPS) {
        log_err(USER_MOD, "Unable to find free group slot");
        return -1;
    }

    groups[i].exists    = true;
    groups[i].gid       = i;
    groups[i].perms     = perms;
    groups[i].groupname = strdup(name);

    return i;
}

uid_t user_create_user_full(const char* name, gid_t gid,
                            const char* home, const char* shell)
{
    if (gid >= MAX_GROUPS)
        return -1;

    int i = 0;
    for (; i < MAX_USERS; i++) {
        if (!users[i].exists)
            break;
    }
    if (i >= MAX_USERS) {
        log_err(USER_MOD, "Unable to find free user slot");
        return -1;
    }

    users[i].exists           = true;
    users[i].uid              = i;
    users[i].group            = gid;
    users[i].username         = strdup(name);
    users[i].password_hash[0] = '\0';

    u_strlcpy(users[i].home,  home  ? home  : "/",       USER_HOME_LEN);
    u_strlcpy(users[i].shell, shell ? shell : "/bin/sh", USER_SHELL_LEN);

    return i;
}

uid_t user_create_user(const char* name, gid_t gid)
{
    char home[USER_HOME_LEN];
    u_fmt(home, USER_HOME_LEN, "/home/%s", name);
    return user_create_user_full(name, gid, home, "/bin/sh");
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

    return groups[reqG].perms <= groups[recG].perms;
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

uid_t user_find_by_name(const char* username)
{
    if (!username)
        return -1;
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].exists && strcmp(users[i].username, username) == 0)
            return (uid_t)i;
    }
    return -1;
}

gid_t group_find_by_name(const char* name)
{
    if (!name)
        return -1;
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups[i].exists && strcmp(groups[i].groupname, name) == 0)
            return (gid_t)i;
    }
    return -1;
}

int user_set_password(uid_t uid, const char* password)
{
    if (!user_exists(uid) || !password)
        return -1;

    uint64_t s = gen_salt();
    uint8_t  salt[8];
    for (int i = 0; i < 8; i++)
        salt[i] = (uint8_t)(s >> (i * 8));

    size_t   plen      = strlen(password);
    uint8_t* input     = kmalloc(8 + plen);
    if (!input)
        return -1;

    memcpy(input, salt, 8);
    memcpy(input + 8, password, plen);

    uint8_t hash[32];
    sha256(input, 8 + plen, hash);
    kfree(input);

    static const char hexc[] = "0123456789abcdef";

    char salt_hex[17];
    for (int i = 0; i < 8; i++) {
        salt_hex[i*2]     = hexc[salt[i] >> 4];
        salt_hex[i*2 + 1] = hexc[salt[i] & 0xf];
    }
    salt_hex[16] = '\0';

    char hash_hex[65];
    for (int i = 0; i < 32; i++) {
        hash_hex[i*2]     = hexc[hash[i] >> 4];
        hash_hex[i*2 + 1] = hexc[hash[i] & 0xf];
    }
    hash_hex[64] = '\0';

    u_fmt(users[uid].password_hash, SHADOW_HASH_LEN,
          "$sha256$%s$%s", salt_hex, hash_hex);

    return 0;
}

uid_t user_authenticate(const char* username, const char* password)
{
    if (!username || !password)
        return -1;

    uid_t uid = user_find_by_name(username);
    if (uid < 0)
        return -1;

    const char* stored = users[uid].password_hash;

    if (stored[0] == '\0' || stored[0] == '!')
        return -1;

    if (u_strncmp(stored, "$sha256$", 8) != 0)
        return -1;

    const char* salt_hex = stored + 8;
    const char* hash_hex = salt_hex + 17;

    uint8_t salt[8];
    for (int i = 0; i < 8; i++)
        salt[i] = hex_byte(salt_hex + i * 2);

    size_t   plen  = strlen(password);
    uint8_t* input = kmalloc(8 + plen);
    if (!input)
        return -1;

    memcpy(input, salt, 8);
    memcpy(input + 8, password, plen);

    uint8_t hash[32];
    sha256(input, 8 + plen, hash);
    kfree(input);

    static const char hexc[] = "0123456789abcdef";
    char computed[65];
    for (int i = 0; i < 32; i++) {
        computed[i*2]     = hexc[hash[i] >> 4];
        computed[i*2 + 1] = hexc[hash[i] & 0xf];
    }
    computed[64] = '\0';

    if (u_strncmp(computed, hash_hex, 64) != 0)
        return -1;

    return uid;
}

int user_save_to_disk(void)
{
    if (!mounted || !rootfs)
        return -1;

    ext2_mkdir(rootfs, "/etc");

    char* pbuf = kmalloc(32768);
    char* sbuf = kmalloc(32768);
    char* gbuf = kmalloc(16384);

    if (!pbuf || !sbuf || !gbuf) {
        kfree(pbuf);
        kfree(sbuf);
        kfree(gbuf);
        return -1;
    }

    size_t poff = 0, soff = 0, goff = 0;

    for (int i = 0; i < MAX_USERS; i++) {
        if (!users[i].exists)
            continue;

        poff += u_fmt(pbuf + poff, 32768 - poff,
                      "%s:x:%d:%d::%s:%s\n",
                      users[i].username,
                      users[i].uid,
                      users[i].group,
                      users[i].home,
                      users[i].shell);

        const char* hash = (users[i].password_hash[0] != '\0')
                         ? users[i].password_hash : "!";

        soff += u_fmt(sbuf + soff, 32768 - soff,
                      "%s:%s:0:0:99999:7:::\n",
                      users[i].username, hash);
    }

    for (int i = 0; i < MAX_GROUPS; i++) {
        if (!groups[i].exists)
            continue;

        goff += u_fmt(gbuf + goff, 16384 - goff,
                      "%s:x:%d:%d\n",
                      groups[i].groupname,
                      groups[i].gid,
                      (int)groups[i].perms);
    }

    int r = 0;
    if (write_full_file("/etc/passwd", pbuf, poff) < 0) r = -1;
    if (write_full_file("/etc/shadow", sbuf, soff) < 0) r = -1;
    if (write_full_file("/etc/group",  gbuf, goff) < 0) r = -1;

    kfree(pbuf);
    kfree(sbuf);
    kfree(gbuf);

    if (r < 0)
        log_err(USER_MOD, "user_save_to_disk: one or more files failed to write");
    else
        log_ok(USER_MOD, "Saved passwd/shadow/group to disk");

    return r;
}

int user_load_from_disk(void)
{
    if (!mounted || !rootfs)
        return -1;

    char* gbuf = read_full_file("/etc/group");
    if (gbuf) {
        for (int i = 0; i < MAX_GROUPS; i++) {
            if (groups[i].exists && groups[i].groupname)
                kfree((void*)groups[i].groupname);
            groups[i].exists = false;
        }

        char* line = gbuf;
        while (*line) {
            char* end = line;
            while (*end && *end != '\n') end++;
            bool last = (*end == '\0');
            *end = '\0';

            if (*line != '\0') {
                char* fields[8];
                int   nf = split_fields(line, fields, 8);
                if (nf >= 4) {
                    int gid  = simple_atoi(fields[2]);
                    int perm = simple_atoi(fields[3]);
                    if (gid >= 0 && gid < MAX_GROUPS) {
                        groups[gid].groupname = strdup(fields[0]);
                        groups[gid].gid       = (gid_t)gid;
                        groups[gid].perms     = (perm_flags_t)perm;
                        groups[gid].exists    = true;
                    }
                }
            }

            if (last) break;
            line = end + 1;
        }
        kfree(gbuf);
    }

    char* pbuf = read_full_file("/etc/passwd");
    if (pbuf) {
        for (int i = 0; i < MAX_USERS; i++) {
            if (users[i].exists && users[i].username)
                kfree((void*)users[i].username);
            users[i].exists           = false;
            users[i].password_hash[0] = '\0';
        }

        char* line = pbuf;
        while (*line) {
            char* end = line;
            while (*end && *end != '\n') end++;
            bool last = (*end == '\0');
            *end = '\0';

            if (*line != '\0') {
                char* fields[8];
                int   nf = split_fields(line, fields, 8);
                if (nf >= 7) {
                    int uid = simple_atoi(fields[2]);
                    int gid = simple_atoi(fields[3]);
                    if (uid >= 0 && uid < MAX_USERS) {
                        users[uid].username = strdup(fields[0]);
                        users[uid].uid      = (uid_t)uid;
                        users[uid].group    = (gid_t)gid;
                        u_strlcpy(users[uid].home,  fields[5], USER_HOME_LEN);
                        u_strlcpy(users[uid].shell, fields[6], USER_SHELL_LEN);
                        users[uid].exists = true;
                    }
                }
            }

            if (last) break;
            line = end + 1;
        }
        kfree(pbuf);
    }

    char* sbuf = read_full_file("/etc/shadow");
    if (sbuf) {
        char* line = sbuf;
        while (*line) {
            char* end = line;
            while (*end && *end != '\n') end++;
            bool last = (*end == '\0');
            *end = '\0';

            if (*line != '\0') {
                char* fields[10];
                int   nf = split_fields(line, fields, 10);
                if (nf >= 2) {
                    uid_t uid = user_find_by_name(fields[0]);
                    if (uid >= 0)
                        u_strlcpy(users[uid].password_hash, fields[1], SHADOW_HASH_LEN);
                }
            }

            if (last) break;
            line = end + 1;
        }
        kfree(sbuf);
    }

    log_ok(USER_MOD, "Loaded user database from disk");
    return 0;
}