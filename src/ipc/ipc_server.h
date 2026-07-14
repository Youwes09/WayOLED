#ifndef WAYOLED_IPC_SERVER_H
#define WAYOLED_IPC_SERVER_H

#include <stddef.h>

#define IPC_SOCKET_PATH "/tmp/wayoled.sock"
#define IPC_CMD_MAX 256
#define IPC_RESP_MAX 1024

typedef struct {
    int listen_fd;
    int client_fd;
} ipc_server_t;

int ipc_server_init(ipc_server_t *srv);
int ipc_server_poll(ipc_server_t *srv, char *cmd_out, size_t cmd_max);
void ipc_server_respond(ipc_server_t *srv, const char *response);
void ipc_server_destroy(ipc_server_t *srv);

#endif
