#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "daemon.h"
#include "types.h"
#include "manager.h"
#include "parser.h"

static void __on_req(request_t* req)
{
    if (req->t == INSTALL)
    {
        package_t *pkg = parse_package(req->pakpath);
        if (pkg == NULL) {errorf("Unable to parse package with path %s\n", req->pakpath); return;}

        if (manager_add_package(pkg) < 0) {errorf("Unable to register package: %s", pkg->name);free(pkg);return;}

        return;
    }

    errorf("Not valid package type from %s: %d\n", req->pakpath, req->t);
}

void daemon_start()
{
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) return;

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;

    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path));
    addr.sun_path[sizeof(addr.sun_path)-1] = '\0'; // safety

    unlink(SOCKET_PATH);

    if (bind(server_fd, &addr, sizeof(addr)) < 0) return;
    if (listen(server_fd, 5) < 0) return;

    while (1)
    {
        int client_fd = accept(server_fd, 0, 0);
        if (client_fd < 0) continue;

        request_t req;
        size_t received = 0;

        while (received < sizeof(req))
        {
            ssize_t n = recv(client_fd, ((char*)&req) + received, sizeof(req) - received, 0);
            if (n <= 0) break;
            received += n;
        }

        if (received == sizeof(req))
        {
            __on_req(&req);
        }
        else
        {
            errorf("Incomplete request received\n");
        }

        close(client_fd);
    }
}