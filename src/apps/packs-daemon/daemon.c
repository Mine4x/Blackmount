#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "daemon.h"
#include "types.h"
#include "manager.h"
#include "parser.h"

static void send_response(int fd,
                           int status,
                           const char* message,
                           const char* data,
                           int data_len)
{
    response_t resp;
    resp.status   = status;
    resp.data_len = (data && data_len > 0) ? data_len : 0;

    strncpy(resp.message,
            message ? message : "",
            sizeof(resp.message) - 1);
    resp.message[sizeof(resp.message) - 1] = '\0';

    
    size_t sent = 0;
    while (sent < sizeof(resp))
    {
        ssize_t n = send(fd,
                         ((const char*)&resp) + sent,
                         sizeof(resp) - sent,
                         0);
        if (n <= 0) return;
        sent += (size_t)n;
    }

    
    if (resp.data_len > 0)
    {
        sent = 0;
        while ((int)sent < resp.data_len)
        {
            ssize_t n = send(fd,
                             data + sent,
                             (size_t)(resp.data_len - (int)sent),
                             0);
            if (n <= 0) return;
            sent += (size_t)n;
        }
    }
}
static void handle_install(int client_fd, request_t* req)
{
    if (req->data[0] == '\0')
    {
        send_response(client_fd, -1, "INSTALL: no package path given", NULL, 0);
        return;
    }

    package_t* pkg = parse_package(req->data);
    if (!pkg)
    {
        char msg[RESPONSE_MSG_SIZE];
        snprintf(msg, sizeof(msg),
                 "INSTALL: failed to parse package at '%s'", req->data);
        send_response(client_fd, -1, msg, NULL, 0);
        return;
    }

    if (manager_install_package(pkg, req->data) < 0)
    {
        char msg[RESPONSE_MSG_SIZE];
        snprintf(msg, sizeof(msg),
                 "INSTALL: failed to install '%s'",
                 pkg->name ? pkg->name : req->data);
        send_response(client_fd, -1, msg, NULL, 0);
        
        if (pkg->name)         free(pkg->name);
        if (pkg->location)     free(pkg->location);
        if (pkg->str_ver)      free(pkg->str_ver);
        if (pkg->related_files)free(pkg->related_files);
        if (pkg->install_spec) free(pkg->install_spec);
        free(pkg);
        return;
    }

    
    char msg[RESPONSE_MSG_SIZE];
    snprintf(msg, sizeof(msg),
             "INSTALL: '%s' %s installed successfully",
             pkg->name, pkg->str_ver ? pkg->str_ver : "");
    send_response(client_fd, 0, msg, NULL, 0);
}




static void handle_remove(int client_fd, request_t* req)
{
    if (req->data[0] == '\0')
    {
        send_response(client_fd, -1, "REMOVE: no package name given", NULL, 0);
        return;
    }

    if (manager_remove_package(req->data) < 0)
    {
        char msg[RESPONSE_MSG_SIZE];
        snprintf(msg, sizeof(msg),
                 "REMOVE: package '%s' not found", req->data);
        send_response(client_fd, -1, msg, NULL, 0);
        return;
    }

    char msg[RESPONSE_MSG_SIZE];
    snprintf(msg, sizeof(msg),
             "REMOVE: '%s' removed successfully", req->data);
    send_response(client_fd, 0, msg, NULL, 0);
}





static void handle_list(int client_fd)
{
    static char buf[65536];
    int len = manager_list_packages(buf, (int)sizeof(buf));

    if (len <= 0)
    {
        send_response(client_fd, 0, "No packages installed.", NULL, 0);
    }
    else
    {
        send_response(client_fd, 0, "OK", buf, len);
    }
}





static void handle_info(int client_fd, request_t* req)
{
    if (req->data[0] == '\0')
    {
        send_response(client_fd, -1, "INFO: no package name given", NULL, 0);
        return;
    }

    char buf[2048];
    int len = manager_package_info(req->data, buf, (int)sizeof(buf));

    if (len < 0)
    {
        char msg[RESPONSE_MSG_SIZE];
        snprintf(msg, sizeof(msg),
                 "INFO: package '%s' not found", req->data);
        send_response(client_fd, -1, msg, NULL, 0);
        return;
    }

    send_response(client_fd, 0, "OK", buf, len);
}





static void dispatch(int client_fd, request_t* req)
{
    switch (req->t)
    {
        case INSTALL: handle_install(client_fd, req); break;
        case REMOVE:  handle_remove(client_fd,  req); break;
        case LIST:    handle_list(client_fd);          break;
        case INFO:    handle_info(client_fd,    req); break;

        default:
        {
            char msg[RESPONSE_MSG_SIZE];
            snprintf(msg, sizeof(msg),
                     "Unknown request type: %d", (int)req->t);
            send_response(client_fd, -1, msg, NULL, 0);
            errorf("packs: unknown request type %d\n", (int)req->t);
            break;
        }
    }
}





void daemon_start()
{
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        errorf("packs: socket() failed\n");
        return;
    }

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

    
    unlink(SOCKET_PATH);

    if (bind(server_fd, &addr, sizeof(addr)) < 0)
    {
        errorf("packs: bind() failed on '%s'\n", SOCKET_PATH);
        close((uint64_t)server_fd);
        return;
    }

    if (listen(server_fd, 8) < 0)
    {
        errorf("packs: listen() failed\n");
        close((uint64_t)server_fd);
        return;
    }

    printf("packs: daemon listening on %s\n", SOCKET_PATH);

    while (1)
    {
        int client_fd = accept(server_fd, 0, 0);
        if (client_fd < 0) continue;

        
        request_t req;
        size_t received = 0;

        while (received < sizeof(req))
        {
            ssize_t n = recv(client_fd,
                             ((char*)&req) + received,
                             sizeof(req) - received,
                             0);
            if (n <= 0) break;
            received += (size_t)n;
        }

        if (received == sizeof(req))
        {
            
            req.data[sizeof(req.data) - 1] = '\0';
            dispatch(client_fd, &req);
        }
        else
        {
            errorf("packs: incomplete request (%zu/%zu bytes)\n",
                   received, sizeof(req));
            send_response(client_fd, -1, "Incomplete request", NULL, 0);
        }

        close((uint64_t)client_fd);
    }
}