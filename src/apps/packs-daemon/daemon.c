#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "daemon.h"
#include "types.h"

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
            printf("Received request type=%d, path=%s\n", req.t, req.pakpath);
        }
        else
        {
            printf("Incomplete request received\n");
        }

        close(client_fd);
    }
}