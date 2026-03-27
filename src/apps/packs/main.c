#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define SOCKET_PATH "/tmp/packs.sock"

typedef enum request_type
{
    INSTALL = 0,
    REMOVE  = 1,
} request_type_t;

typedef struct request
{
    request_type_t  t;
    char pakpath[124];
} request_t;

int main()
{
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("socket");
        return 1;
    }

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path));
    addr.sun_path[sizeof(addr.sun_path)-1] = '\0';

    if (connect(sock, &addr, sizeof(addr)) < 0) {
        printf("connect");
        return 1;
    }

    request_t req;
    req.t = REMOVE;
    strncpy(req.pakpath, "/tmp/testpackage", sizeof(req.pakpath));
    req.pakpath[sizeof(req.pakpath)-1] = '\0';

    ssize_t sent = send(sock, &req, sizeof(req), 0);
    if (sent != sizeof(req)) {
        printf("send");
        close(sock);
        return 1;
    }

    printf("Test package sent: %s\n", req.pakpath);

    close(sock);
    return 0;
}