#include "ipc_client.h"
#include "../src/ipc/ipc_server.h"

#include <stdio.h>
#include <string.h>

static void print_usage(const char *prog) {
    fprintf(stderr, "usage: %s <command> [args]\n", prog);
    fprintf(stderr, "run '%s help' for the full command list\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    char cmd[IPC_CMD_MAX] = {0};
    size_t off = 0;

    for (int i = 1; i < argc; i++) {
        int n = snprintf(cmd + off, sizeof(cmd) - off, "%s%s", i > 1 ? " " : "", argv[i]);
        if (n < 0 || (size_t)n >= sizeof(cmd) - off)
            break;
        off += (size_t)n;
    }

    char resp[IPC_RESP_MAX] = {0};
    if (ipc_client_send(cmd, resp, sizeof(resp)) != 0)
        return 1;

    fputs(resp, stdout);
    return (strncmp(resp, "err", 3) == 0) ? 1 : 0;
}
