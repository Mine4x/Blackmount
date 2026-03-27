#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define AF_UNIX         1

#define SOCK_STREAM     1
#define SOCK_DGRAM      2

#define UNIX_PATH_MAX       108
#define UNIX_BUF_SIZE       8192   
#define UNIX_BACKLOG_MAX    8      
#define MAX_UNIX_SOCKETS    64

struct sockaddr_un {
    uint16_t sun_family;            
    char     sun_path[UNIX_PATH_MAX];
};

typedef struct {
    uint8_t  data[UNIX_BUF_SIZE];
    uint32_t head;   
    uint32_t tail;   
    uint32_t count;  
} unix_ring_t;

typedef struct {
    int  client_sock_id;  
    int  client_pid;      
    bool accepted;        
    int  peer_sock_id;    
} unix_pending_t;

typedef enum {
    US_FREE      = 0,
    US_CREATED,        
    US_BOUND,          
    US_LISTENING,      
    US_CONNECTED,      
    US_CLOSED,         
} unix_sock_state_t;

typedef struct {
    bool               in_use;
    unix_sock_state_t  state;
    int                type;                   
    char               path[UNIX_PATH_MAX];    

    int                peer_id;   

    unix_ring_t        rx;

    unix_pending_t     backlog[UNIX_BACKLOG_MAX];
    int                backlog_head;
    int                backlog_tail;
    int                backlog_count;

    int                blocked_reader_pid;

    int                owner_pid;   
} unix_socket_t;



void unix_sock_init(void);


int            unix_sock_create (int type);          
void           unix_sock_destroy(int id);


int unix_sock_bind   (int id, const char *path);     
int unix_sock_listen (int id, int backlog);           
int unix_sock_accept (int id);                        


int unix_sock_connect(int id, const char *path);      


int unix_sock_write  (int id, const void *buf, size_t count);
int unix_sock_read   (int id,       void *buf, size_t count);


int unix_sock_sendto  (int id, const void *buf, size_t count, const char *dest_path);
int unix_sock_recvfrom(int id,       void *buf, size_t count, char *src_path_out);


unix_socket_t *unix_sock_get(int id);