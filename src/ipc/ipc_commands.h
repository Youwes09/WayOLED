#ifndef WAYOLED_IPC_COMMANDS_H
#define WAYOLED_IPC_COMMANDS_H

#include "../core/state.h"

void ipc_dispatch(wayoled_state_t *st, const char *cmd);

#endif
