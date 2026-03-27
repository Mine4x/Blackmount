#include "unix_socket.h"
#include <string.h>
#include <debug.h>
#include <proc/proc.h>


static unix_socket_t sock_table[MAX_UNIX_SOCKETS];

void unix_sock_init(void)
{
    memset(sock_table, 0, sizeof(sock_table));
    for (int i = 0; i < MAX_UNIX_SOCKETS; i++) {
        sock_table[i].peer_id            = -1;
        sock_table[i].blocked_reader_pid = -1;
    }
    log_ok("UNIX", "socket subsystem initialised (%d slots)", MAX_UNIX_SOCKETS);
}



unix_socket_t *unix_sock_get(int id)
{
    if (id < 0 || id >= MAX_UNIX_SOCKETS)
        return NULL;
    return sock_table[id].in_use ? &sock_table[id] : NULL;
}

static unix_socket_t *find_listener(const char *path)
{
    for (int i = 0; i < MAX_UNIX_SOCKETS; i++) {
        if (sock_table[i].in_use &&
            sock_table[i].state == US_LISTENING &&
            strcmp(sock_table[i].path, path) == 0)
            return &sock_table[i];
    }
    return NULL;
}

static unix_socket_t *find_bound(const char *path)
{
    for (int i = 0; i < MAX_UNIX_SOCKETS; i++) {
        if (sock_table[i].in_use &&
            (sock_table[i].state == US_BOUND ||
             sock_table[i].state == US_LISTENING) &&
            strcmp(sock_table[i].path, path) == 0)
            return &sock_table[i];
    }
    return NULL;
}


static void wake_reader(unix_socket_t *s)
{
    if (s->blocked_reader_pid >= 0) {
        proc_unblock(s->blocked_reader_pid);
        s->blocked_reader_pid = -1;
    }
}



static bool rb_write(unix_ring_t *rb, const uint8_t *src, uint32_t len)
{
    if (rb->count + len > UNIX_BUF_SIZE)
        return false;
    for (uint32_t i = 0; i < len; i++) {
        rb->data[rb->tail] = src[i];
        rb->tail = (rb->tail + 1) % UNIX_BUF_SIZE;
    }
    rb->count += len;
    return true;
}

static uint32_t rb_read(unix_ring_t *rb, uint8_t *dst, uint32_t want)
{
    uint32_t n = (want < rb->count) ? want : rb->count;
    for (uint32_t i = 0; i < n; i++) {
        dst[i] = rb->data[rb->head];
        rb->head = (rb->head + 1) % UNIX_BUF_SIZE;
    }
    rb->count -= n;
    return n;
}



int unix_sock_create(int type)
{
    if (type != SOCK_STREAM && type != SOCK_DGRAM) {
        log_err("UNIX", "unix_sock_create: unsupported type %d", type);
        return -1;
    }

    for (int i = 0; i < MAX_UNIX_SOCKETS; i++) {
        if (!sock_table[i].in_use) {
            memset(&sock_table[i], 0, sizeof(unix_socket_t));
            sock_table[i].in_use             = true;
            sock_table[i].state              = US_CREATED;
            sock_table[i].type               = type;
            sock_table[i].peer_id            = -1;
            sock_table[i].blocked_reader_pid = -1;
            sock_table[i].owner_pid          = proc_get_current_pid();
            log_info("UNIX", "created socket %d (type=%s)",
                     i, type == SOCK_STREAM ? "STREAM" : "DGRAM");
            return i;
        }
    }

    log_err("UNIX", "unix_sock_create: socket table full");
    return -1;
}

void unix_sock_destroy(int id)
{
    unix_socket_t *s = unix_sock_get(id);
    if (!s)
        return;

    log_info("UNIX", "destroying socket %d (state=%d)", id, s->state);

    
    wake_reader(s);

    
    if (s->peer_id >= 0) {
        unix_socket_t *peer = unix_sock_get(s->peer_id);
        if (peer) {
            peer->state   = US_CLOSED;
            peer->peer_id = -1;
            wake_reader(peer);
        }
        s->peer_id = -1;
    }

    



    while (s->backlog_count > 0) {
        unix_pending_t *e = &s->backlog[s->backlog_head];
        s->backlog_head  = (s->backlog_head + 1) % UNIX_BACKLOG_MAX;
        s->backlog_count--;
        if (e->client_pid >= 0)
            proc_unblock(e->client_pid);
    }

    s->state  = US_CLOSED;
    s->in_use = false;
}



int unix_sock_bind(int id, const char *path)
{
    unix_socket_t *s = unix_sock_get(id);
    if (!s) {
        log_err("UNIX", "bind: invalid socket id %d", id);
        return -1;
    }
    if (s->state != US_CREATED) {
        log_err("UNIX", "bind: socket %d not in CREATED state", id);
        return -1;
    }
    if (!path || path[0] == '\0') {
        log_err("UNIX", "bind: empty path");
        return -1;
    }

    
    if (find_bound(path)) {
        log_err("UNIX", "bind: path '%s' already in use", path);
        return -1;
    }

    strncpy(s->path, path, UNIX_PATH_MAX - 1);
    s->path[UNIX_PATH_MAX - 1] = '\0';
    s->state = US_BOUND;

    log_ok("UNIX", "socket %d bound to '%s'", id, path);
    return 0;
}

int unix_sock_listen(int id, int backlog)
{
    unix_socket_t *s = unix_sock_get(id);
    if (!s) {
        log_err("UNIX", "listen: invalid socket id %d", id);
        return -1;
    }
    if (s->type != SOCK_STREAM) {
        log_err("UNIX", "listen: DGRAM sockets do not use listen()");
        return -1;
    }
    if (s->state != US_BOUND) {
        log_err("UNIX", "listen: socket %d must be bound first", id);
        return -1;
    }
    (void)backlog; 
    s->state = US_LISTENING;
    log_ok("UNIX", "socket %d listening on '%s'", id, s->path);
    return 0;
}








int unix_sock_accept(int id)
{
    unix_socket_t *server = unix_sock_get(id);
    if (!server || server->state != US_LISTENING) {
        log_err("UNIX", "accept: socket %d not listening", id);
        return -1;
    }

    
    while (server->backlog_count == 0) {
        server->blocked_reader_pid = proc_get_current_pid();
        proc_block(proc_get_current_pid());
        proc_yield();

        
        server = unix_sock_get(id);
        if (!server || server->state != US_LISTENING) {
            log_err("UNIX", "accept: listener %d gone while blocked", id);
            return -1;
        }
    }

    
    unix_pending_t entry = server->backlog[server->backlog_head];
    server->backlog_head  = (server->backlog_head + 1) % UNIX_BACKLOG_MAX;
    server->backlog_count--;

    int client_id  = entry.client_sock_id;
    int client_pid = entry.client_pid;

    
    int new_id = -1;
    for (int i = 0; i < MAX_UNIX_SOCKETS; i++) {
        if (!sock_table[i].in_use) {
            memset(&sock_table[i], 0, sizeof(unix_socket_t));
            sock_table[i].in_use             = true;
            sock_table[i].state              = US_CONNECTED;
            sock_table[i].type               = server->type;
            sock_table[i].peer_id            = client_id;
            sock_table[i].blocked_reader_pid = -1;
            sock_table[i].owner_pid          = proc_get_current_pid();
            strncpy(sock_table[i].path, server->path, UNIX_PATH_MAX - 1);
            new_id = i;
            break;
        }
    }

    if (new_id < 0) {
        log_err("UNIX", "accept: no free socket slot for new connection");
        
        if (client_pid >= 0)
            proc_unblock(client_pid);
        return -1;
    }

    
    unix_socket_t *client = unix_sock_get(client_id);
    if (client) {
        client->state   = US_CONNECTED;
        client->peer_id = new_id;
    }

    
    if (client_pid >= 0)
        proc_unblock(client_pid);

    log_ok("UNIX", "accept: listener=%d  client_sock=%d  server_conn=%d",
           id, client_id, new_id);

    return new_id;
}



int unix_sock_connect(int id, const char *path)
{
    unix_socket_t *client = unix_sock_get(id);
    if (!client) {
        log_err("UNIX", "connect: invalid socket id %d", id);
        return -1;
    }
    if (client->state != US_CREATED && client->state != US_BOUND) {
        log_err("UNIX", "connect: socket %d already connected or closed", id);
        return -1;
    }
    if (client->type != SOCK_STREAM) {
        log_err("UNIX", "connect: DGRAM sockets use sendto(), not connect()");
        return -1;
    }

    unix_socket_t *server = find_listener(path);
    if (!server) {
        log_err("UNIX", "connect: no listener at '%s'", path);
        return -1;
    }

    if (server->backlog_count >= UNIX_BACKLOG_MAX) {
        log_err("UNIX", "connect: listener at '%s' backlog full", path);
        return -1;
    }

    
    unix_pending_t *e = &server->backlog[server->backlog_tail];
    e->client_sock_id = id;
    e->client_pid     = proc_get_current_pid();
    e->accepted       = false;
    e->peer_sock_id   = -1;
    server->backlog_tail  = (server->backlog_tail + 1) % UNIX_BACKLOG_MAX;
    server->backlog_count++;

    
    wake_reader(server);

    
    proc_block(proc_get_current_pid());
    proc_yield();

    




    client = unix_sock_get(id);
    if (!client || client->state != US_CONNECTED) {
        log_err("UNIX", "connect: pairing failed for socket %d (path '%s')", id, path);
        return -1;
    }

    log_ok("UNIX", "connect: socket %d connected to '%s' (peer=%d)",
           id, path, client->peer_id);
    return 0;
}



int unix_sock_write(int id, const void *buf, size_t count)
{
    unix_socket_t *s = unix_sock_get(id);
    if (!s) return -1;
    if (s->state != US_CONNECTED) {
        log_err("UNIX", "write: socket %d not connected (state=%d)", id, s->state);
        return -1;
    }

    const uint8_t *src     = (const uint8_t *)buf;
    uint32_t       written = 0;

    while (written < (uint32_t)count) {
        unix_socket_t *peer = unix_sock_get(s->peer_id);
        if (!peer || peer->state == US_CLOSED) {
            
            log_err("UNIX", "write: peer of socket %d is gone", id);
            break;
        }

        uint32_t space = UNIX_BUF_SIZE - peer->rx.count;
        if (space == 0) {
            
            proc_yield();
            continue;
        }

        uint32_t chunk = (uint32_t)(count - written);
        if (chunk > space)
            chunk = space;

        rb_write(&peer->rx, src + written, chunk);
        written += chunk;

        
        wake_reader(peer);
    }

    return (int)written;
}

int unix_sock_read(int id, void *buf, size_t count)
{
    unix_socket_t *s = unix_sock_get(id);
    if (!s) return -1;
    if (s->state != US_CONNECTED && s->state != US_CLOSED) {
        log_err("UNIX", "read: socket %d not connected", id);
        return -1;
    }

    
    while (s->rx.count == 0) {
        
        if (s->peer_id < 0 ||
            !sock_table[s->peer_id].in_use ||
            sock_table[s->peer_id].state == US_CLOSED) {
            return 0; 
        }
        s->blocked_reader_pid = proc_get_current_pid();
        proc_block(proc_get_current_pid());
        proc_yield();

        s = unix_sock_get(id);
        if (!s)
            return -1;
    }

    uint32_t n = rb_read(&s->rx, (uint8_t *)buf, (uint32_t)count);
    return (int)n;
}



int unix_sock_sendto(int id, const void *buf, size_t count, const char *dest_path)
{
    unix_socket_t *s = unix_sock_get(id);
    if (!s) {
        log_err("UNIX", "sendto: invalid socket %d", id);
        return -1;
    }
    if (s->type != SOCK_DGRAM) {
        log_err("UNIX", "sendto: socket %d is not DGRAM", id);
        return -1;
    }

    
    unix_socket_t *dest = find_bound(dest_path);
    if (!dest) {
        log_err("UNIX", "sendto: no socket bound at '%s'", dest_path);
        return -1;
    }

    if (count > UNIX_BUF_SIZE - dest->rx.count) {
        log_err("UNIX", "sendto: destination buffer full");
        return -1;
    }

    rb_write(&dest->rx, (const uint8_t *)buf, (uint32_t)count);
    wake_reader(dest);

    return (int)count;
}

int unix_sock_recvfrom(int id, void *buf, size_t count, char *src_path_out)
{
    unix_socket_t *s = unix_sock_get(id);
    if (!s) {
        log_err("UNIX", "recvfrom: invalid socket %d", id);
        return -1;
    }
    if (s->type != SOCK_DGRAM) {
        log_err("UNIX", "recvfrom: socket %d is not DGRAM", id);
        return -1;
    }

    
    while (s->rx.count == 0) {
        s->blocked_reader_pid = proc_get_current_pid();
        proc_block(proc_get_current_pid());
        proc_yield();

        s = unix_sock_get(id);
        if (!s)
            return -1;
    }

    uint32_t n = rb_read(&s->rx, (uint8_t *)buf, (uint32_t)count);

    




    if (src_path_out)
        src_path_out[0] = '\0';

    return (int)n;
}