#include "monitor.h"
#include "profile.h"
#include "../colortemp/colortemp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>

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

static void output_name_event(void *data, struct wl_output *wl_output, const char *name) {
    (void)wl_output;
    wayoled_monitor_t *mon = data;
    snprintf(mon->name, sizeof(mon->name), "%s", name);
}

static void output_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y,
                             int32_t w, int32_t h, int32_t subpixel, const char *make,
                             const char *model, int32_t transform) {
    (void)data; (void)wl_output; (void)x; (void)y; (void)w; (void)h;
    (void)subpixel; (void)make; (void)model; (void)transform;
}

static void output_mode(void *data, struct wl_output *wl_output, uint32_t flags,
                         int32_t w, int32_t h, int32_t refresh) {
    (void)data; (void)wl_output; (void)flags; (void)w; (void)h; (void)refresh;
}

static void output_done(void *data, struct wl_output *wl_output) {
    (void)data; (void)wl_output;
}

static void output_scale(void *data, struct wl_output *wl_output, int32_t factor) {
    (void)data; (void)wl_output; (void)factor;
}

static void output_description(void *data, struct wl_output *wl_output, const char *description) {
    (void)data; (void)wl_output; (void)description;
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name_event,
    .description = output_description,
};

void monitor_add(wayoled_state_t *st, struct wl_output *output,
                  uint32_t registry_name, uint32_t version) {
    (void)version;

    if (st->monitor_count >= WAYOLED_MAX_MONITORS) {
        wl_output_destroy(output);
        return;
    }

    wayoled_monitor_t *mon = &st->monitors[st->monitor_count++];
    memset(mon, 0, sizeof(*mon));
    mon->output = output;
    mon->registry_name = registry_name;
    mon->pending = 1;
    snprintf(mon->name, sizeof(mon->name), "output-%u", registry_name);
    wl_output_add_listener(output, &output_listener, mon);
}

void monitor_mark_removed(wayoled_state_t *st, uint32_t registry_name) {
    for (int i = 0; i < st->monitor_count; i++) {
        if (st->monitors[i].output && st->monitors[i].registry_name == registry_name)
            st->monitors[i].removing = 1;
    }
}

static void monitor_setup(wayoled_state_t *st, wayoled_monitor_t *mon) {
    mon->pending = 0;
    fprintf(stderr, "wayoled: output %s connected\n", mon->name);

    if (st->shm && st->screencopy_manager &&
        screencopy_init(&mon->screencopy, st->shm, st->screencopy_manager, mon->output) == 0) {
        mon->screencopy_available = 1;
        st->cap_static_content = 1;
    }

    if (st->gamma_manager && dimmer_init(&mon->dimmer, st->gamma_manager, mon->output) == 0) {
        dimmer_confirm(&mon->dimmer, st->display);
        if (mon->dimmer.available)
            st->cap_gamma = 1;
    }

    profile_apply(mon, "default");
    colortemp_tick(mon);
}

static void monitor_teardown(wayoled_monitor_t *mon) {
    fprintf(stderr, "wayoled: output %s disconnected\n", mon->name);

    if (mon->refresh_in_progress) {
        kill(mon->refresh_pid, SIGTERM);
        waitpid(mon->refresh_pid, NULL, 0);
    }
    dimmer_destroy(&mon->dimmer);
    if (mon->screencopy_available)
        screencopy_destroy(&mon->screencopy);
    free(mon->last_hashes);
    if (mon->output)
        wl_output_destroy(mon->output);
}

void monitor_reconcile(wayoled_state_t *st) {
    int has_pending = 0;
    for (int i = 0; i < st->monitor_count; i++)
        has_pending |= st->monitors[i].pending;

    if (has_pending)
        wl_display_roundtrip(st->display);

    for (int i = 0; i < st->monitor_count; i++) {
        if (st->monitors[i].pending)
            monitor_setup(st, &st->monitors[i]);
    }

    for (int i = 0; i < st->monitor_count;) {
        if (!st->monitors[i].removing) {
            i++;
            continue;
        }

        monitor_teardown(&st->monitors[i]);

        int rest = st->monitor_count - i - 1;
        if (rest > 0) {
            memmove(&st->monitors[i], &st->monitors[i + 1],
                    (size_t)rest * sizeof(wayoled_monitor_t));
            for (int k = i; k < i + rest; k++) {
                wayoled_monitor_t *m = &st->monitors[k];
                if (m->dimmer.control)
                    wl_proxy_set_user_data((struct wl_proxy *)m->dimmer.control, &m->dimmer);
                if (m->output)
                    wl_proxy_set_user_data((struct wl_proxy *)m->output, m);
            }
        }

        st->monitor_count--;
        memset(&st->monitors[st->monitor_count], 0, sizeof(wayoled_monitor_t));
    }
}
