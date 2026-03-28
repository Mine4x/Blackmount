#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define PACKS_SOCKET_PATH   "/tmp/packs.sock"
#define PACKS_DATA_SIZE     124
#define RESPONSE_MSG_SIZE   240

typedef enum
{
    PACKS_INSTALL = 0,
    PACKS_REMOVE  = 1,
    PACKS_LIST    = 2,
    PACKS_INFO    = 3,
} packs_req_type_t;

typedef struct
{
    packs_req_type_t t;
    char             data[PACKS_DATA_SIZE];
} packs_request_t;

typedef struct
{
    int  status;
    int  data_len;
    char message[RESPONSE_MSG_SIZE];
} packs_response_t;


#define COL_RESET   "\x1b[0m"
#define COL_BOLD    "\x1b[1m"
#define COL_RED     "\x1b[31m"
#define COL_GREEN   "\x1b[32m"
#define COL_YELLOW  "\x1b[33m"
#define COL_CYAN    "\x1b[36m"
#define COL_GREY    "\x1b[90m"

static void print_ok(const char* msg)
{
    printf(COL_GREEN COL_BOLD "  ok  " COL_RESET "  %s\n", msg);
}

static void print_err(const char* msg)
{
    printf(COL_RED COL_BOLD " err  " COL_RESET "  %s\n", msg);
}

static void print_info(const char* msg)
{
    printf(COL_CYAN COL_BOLD "info  " COL_RESET "  %s\n", msg);
}









static int packs_connect()
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        print_err("socket() failed — cannot create socket");
        return -1;
    }

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, PACKS_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

    if (connect(fd, &addr, sizeof(addr)) < 0)
    {
        print_err("connect() failed — is the packs daemon running?");
        close((uint64_t)fd);
        return -1;
    }

    return fd;
}





static int send_request(int fd, const packs_request_t* req)
{
    size_t sent = 0;
    while (sent < sizeof(*req))
    {
        ssize_t n = send(fd,
                         ((const char*)req) + sent,
                         sizeof(*req) - sent,
                         0);
        if (n <= 0)
        {
            print_err("send() failed — connection lost");
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}





static int recv_response(int fd, packs_response_t* resp)
{
    size_t got = 0;
    while (got < sizeof(*resp))
    {
        ssize_t n = recv(fd,
                         ((char*)resp) + got,
                         sizeof(*resp) - got,
                         0);
        if (n <= 0)
        {
            print_err("recv() failed — connection lost while reading response");
            return -1;
        }
        got += (size_t)n;
    }
    resp->message[RESPONSE_MSG_SIZE - 1] = '\0';
    return 0;
}






static char* recv_payload(int fd, int data_len)
{
    if (data_len <= 0) return NULL;

    char* buf = malloc((size_t)data_len + 1);
    if (!buf)
    {
        print_err("out of memory receiving payload");
        return NULL;
    }

    size_t got = 0;
    while ((int)got < data_len)
    {
        ssize_t n = recv(fd,
                         buf + got,
                         (size_t)(data_len - (int)got),
                         0);
        if (n <= 0)
        {
            print_err("recv() failed while reading payload");
            free(buf);
            return NULL;
        }
        got += (size_t)n;
    }

    buf[data_len] = '\0';
    return buf;
}






static int transact(const packs_request_t* req)
{
    int fd = packs_connect();
    if (fd < 0) return -1;

    if (send_request(fd, req) < 0)
    {
        close((uint64_t)fd);
        return -1;
    }

    packs_response_t resp;
    if (recv_response(fd, &resp) < 0)
    {
        close((uint64_t)fd);
        return -1;
    }

    
    if (resp.status == 0)
        print_ok(resp.message[0] ? resp.message : "Success");
    else
        print_err(resp.message[0] ? resp.message : "Unknown error");

    
    if (resp.data_len > 0)
    {
        char* payload = recv_payload(fd, resp.data_len);
        if (payload)
        {
            printf("\n%s\n", payload);
            free(payload);
        }
    }

    close((uint64_t)fd);
    return resp.status;
}





static int cmd_install(const char* pakpath)
{
    if (!pakpath || pakpath[0] == '\0')
    {
        print_err("install requires a package path");
        return -1;
    }

    printf(COL_BOLD "packs install" COL_RESET
           COL_GREY " → " COL_RESET "%s\n\n", pakpath);

    packs_request_t req;
    req.t = PACKS_INSTALL;
    strncpy(req.data, pakpath, PACKS_DATA_SIZE - 1);
    req.data[PACKS_DATA_SIZE - 1] = '\0';

    return transact(&req);
}

static int cmd_remove(const char* name)
{
    if (!name || name[0] == '\0')
    {
        print_err("remove requires a package name");
        return -1;
    }

    printf(COL_BOLD "packs remove" COL_RESET
           COL_GREY " → " COL_RESET "%s\n\n", name);

    packs_request_t req;
    req.t = PACKS_REMOVE;
    strncpy(req.data, name, PACKS_DATA_SIZE - 1);
    req.data[PACKS_DATA_SIZE - 1] = '\0';

    return transact(&req);
}

static int cmd_list()
{
    printf(COL_BOLD "packs list\n\n" COL_RESET);

    packs_request_t req;
    req.t = PACKS_LIST;
    req.data[0] = '\0';

    return transact(&req);
}

static int cmd_info(const char* name)
{
    if (!name || name[0] == '\0')
    {
        print_err("info requires a package name");
        return -1;
    }

    printf(COL_BOLD "packs info" COL_RESET
           COL_GREY " → " COL_RESET "%s\n\n", name);

    packs_request_t req;
    req.t = PACKS_INFO;
    strncpy(req.data, name, PACKS_DATA_SIZE - 1);
    req.data[PACKS_DATA_SIZE - 1] = '\0';

    return transact(&req);
}

static void cmd_help(const char* argv0)
{
    printf(
        COL_BOLD "packs" COL_RESET " — Blackmount package manager\n"
        "\n"
        COL_BOLD "Usage:" COL_RESET "\n"
        "  %s install <path>   install a package from an unpacked directory\n"
        "  %s remove  <name>   remove an installed package by name\n"
        "  %s list             list all installed packages\n"
        "  %s info    <name>   show detailed info for a package\n"
        "  %s help             show this help text\n"
        "\n"
        COL_BOLD "Package directory layout:" COL_RESET "\n"
        "  myapp/\n"
        "  ├── package.conf   metadata (required)\n"
        "  └── myapp          any files to be installed system-wide\n"
        "\n"
        COL_BOLD "package.conf fields:" COL_RESET "\n"
        "  name=<str>          package identifier\n"
        "  type=<0|1>          0 = executable,  1 = service\n"
        "  strv=<str>          version string, e.g. 1.2.0\n"
        "  intv=<int>          numeric version,  e.g. 120\n"
        "  files=<spec>        (optional) files to install system-wide\n"
        "                      format: srcname:/dest/path;srcname2:/dest2\n"
        "\n"
        COL_BOLD "Daemon socket:" COL_RESET "  %s\n"
        "\n",
        argv0, argv0, argv0, argv0, argv0,
        PACKS_SOCKET_PATH
    );
}





int main(int argc, char** argv)
{
    if (argc < 2)
    {
        cmd_help(argv[0]);
        return 0;
    }

    const char* cmd = argv[1];

    if (strcmp(cmd, "install") == 0)
    {
        if (argc < 3)
        {
            print_err("usage: packs install <path>");
            return -1;
        }
        return cmd_install(argv[2]);
    }

    if (strcmp(cmd, "remove") == 0)
    {
        if (argc < 3)
        {
            print_err("usage: packs remove <name>");
            return -1;
        }
        return cmd_remove(argv[2]);
    }

    if (strcmp(cmd, "list") == 0)
        return cmd_list();

    if (strcmp(cmd, "info") == 0)
    {
        if (argc < 3)
        {
            print_err("usage: packs info <name>");
            return -1;
        }
        return cmd_info(argv[2]);
    }

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0)
    {
        cmd_help(argv[0]);
        return 0;
    }

    
    char errmsg[128];
    snprintf(errmsg, sizeof(errmsg), "unknown command '%s' — try 'packs help'", cmd);
    print_err(errmsg);
    return -1;
}