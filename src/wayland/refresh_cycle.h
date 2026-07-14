#ifndef WAYOLED_REFRESH_CYCLE_H
#define WAYOLED_REFRESH_CYCLE_H

// Runs the pixel-refresh sweep. Returns 0 on normal completion, -1 on
// failure, and 1 if cancelled early via SIGTERM (see refresh stop).
int refresh_cycle_run(void);

#endif
