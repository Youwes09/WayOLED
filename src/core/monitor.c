#include "monitor.h"

#include <string.h>
#include <stdio.h>

wayoled_monitor_t *monitor_find(wayoled_state_t *st, const char *name) {
    for (int i = 0; i < st->monitor_count; i++) {
        if (strcmp(st->monitors[i].name, name) == 0)
            return &st->monitors[i];
    }
    return NULL;
}

void monitor_name_list(wayoled_state_t *st, char *buf, size_t max) {
    size_t off = 0;
    for (int i = 0; i < st->monitor_count && off < max; i++) {
        int n = snprintf(buf + off, max - off, "%s%s", i ? "," : "", st->monitors[i].name);
        if (n < 0 || (size_t)n >= max - off)
            break;
        off += (size_t)n;
    }
}

int monitor_resolve(wayoled_state_t *st, const char *cli_name,
                     char cfg_monitors[][WAYOLED_MONITOR_NAME_MAX], int cfg_count,
                     int require_explicit, wayoled_monitor_t *out[],
                     char *err, size_t err_max) {
    if (cli_name && cli_name[0]) {
        wayoled_monitor_t *m = monitor_find(st, cli_name);
        if (!m) {
            snprintf(err, err_max, "err unknown monitor '%s'\n", cli_name);
            return -1;
        }
        out[0] = m;
        return 1;
    }

    if (st->monitor_count == 1) {
        out[0] = &st->monitors[0];
        return 1;
    }

    if (cfg_count > 0) {
        int n = 0;
        for (int i = 0; i < cfg_count; i++) {
            wayoled_monitor_t *m = monitor_find(st, cfg_monitors[i]);
            if (m)
                out[n++] = m;
        }
        return n;
    }

    if (require_explicit) {
        char list[256];
        monitor_name_list(st, list, sizeof(list));
        snprintf(err, err_max, "err multiple monitors, specify --monitor (available: %s)\n", list);
        return -1;
    }

    for (int i = 0; i < st->monitor_count; i++)
        out[i] = &st->monitors[i];
    return st->monitor_count;
}
