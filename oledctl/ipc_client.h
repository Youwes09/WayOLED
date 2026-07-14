#ifndef WAYOLED_IPC_CLIENT_H
#define WAYOLED_IPC_CLIENT_H

#include <stddef.h>

int ipc_client_send(const char *cmd, char *resp_out, size_t resp_max);

#endif
