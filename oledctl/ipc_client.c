#include "ipc_client.h"
#include "../src/ipc/ipc_server.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

int ipc_client_send(const char *cmd, char *resp_out, size_t resp_max) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "oledctl: socket() failed: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, IPC_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "oledctl: cannot connect to %s: %s\n", IPC_SOCKET_PATH, strerror(errno));
        close(fd);
        return -1;
    }

    char line[IPC_CMD_MAX];
    int len = snprintf(line, sizeof(line), "%s\n", cmd);
    if (write(fd, line, (size_t)len) != len) {
        fprintf(stderr, "oledctl: write failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    size_t total = 0;
    for (;;) {
        ssize_t n = read(fd, resp_out + total, resp_max - 1 - total);
        if (n <= 0)
            break;
        total += (size_t)n;
        if (total >= resp_max - 1)
            break;
    }
    resp_out[total] = '\0';

    close(fd);
    return 0;
}
