#ifndef WAYOLED_MONITOR_H
#define WAYOLED_MONITOR_H

#include "state.h"

wayoled_monitor_t *monitor_find(wayoled_state_t *st, const char *name);
int monitor_resolve(wayoled_state_t *st, const char *cli_name,
                     char cfg_monitors[][WAYOLED_MONITOR_NAME_MAX], int cfg_count,
                     int require_explicit, wayoled_monitor_t *out[],
                     char *err, size_t err_max);
void monitor_name_list(wayoled_state_t *st, char *buf, size_t max);

void monitor_add(wayoled_state_t *st, struct wl_output *output,
                  uint32_t registry_name, uint32_t version);
void monitor_mark_removed(wayoled_state_t *st, uint32_t registry_name);
void monitor_reconcile(wayoled_state_t *st);

#endif
