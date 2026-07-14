#define _GNU_SOURCE
#include "ipc_server.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

int ipc_server_init(ipc_server_t *srv) {
    srv->client_fd = -1;
    unlink(IPC_SOCKET_PATH);

    srv->listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (srv->listen_fd < 0) {
        fprintf(stderr, "wayoled: socket() failed: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, IPC_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "wayoled: bind() failed: %s\n", strerror(errno));
        close(srv->listen_fd);
        return -1;
    }

    if (chmod(IPC_SOCKET_PATH, S_IRUSR | S_IWUSR) < 0) {
        fprintf(stderr, "wayoled: chmod() on socket failed: %s\n", strerror(errno));
        close(srv->listen_fd);
        unlink(IPC_SOCKET_PATH);
        return -1;
    }

    if (listen(srv->listen_fd, 4) < 0) {
        fprintf(stderr, "wayoled: listen() failed: %s\n", strerror(errno));
        close(srv->listen_fd);
        return -1;
    }

    return 0;
}

static int peer_uid_ok(int client_fd) {
    struct ucred cred;
    socklen_t len = sizeof(cred);

    if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0)
        return 0;

    return cred.uid == getuid();
}

int ipc_server_poll(ipc_server_t *srv, char *cmd_out, size_t cmd_max) {
    int client_fd = accept(srv->listen_fd, NULL, NULL);
    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        return -1;
    }

    if (!peer_uid_ok(client_fd)) {
        fprintf(stderr, "wayoled: rejected IPC connection from foreign UID\n");
        close(client_fd);
        return 0;
    }

    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ssize_t n = read(client_fd, cmd_out, cmd_max - 1);
    if (n <= 0) {
        close(client_fd);
        return 0;
    }

    cmd_out[n] = '\0';

    char *newline = strchr(cmd_out, '\n');
    if (newline)
        *newline = '\0';

    srv->client_fd = client_fd;
    return 1;
}

void ipc_server_respond(ipc_server_t *srv, const char *response) {
    if (srv->client_fd < 0)
        return;

    write(srv->client_fd, response, strlen(response));
    close(srv->client_fd);
    srv->client_fd = -1;
}

void ipc_server_destroy(ipc_server_t *srv) {
    if (srv->client_fd >= 0)
        close(srv->client_fd);
    if (srv->listen_fd >= 0)
        close(srv->listen_fd);
    unlink(IPC_SOCKET_PATH);
}
