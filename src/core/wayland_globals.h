#ifndef WAYOLED_WAYLAND_GLOBALS_H
#define WAYOLED_WAYLAND_GLOBALS_H

#include <wayland-client.h>
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "wlr-gamma-control-unstable-v1-client-protocol.h"
#include "ext-idle-notify-v1-client-protocol.h"

typedef struct {
    struct wl_seat *seat;
    struct wl_shm *shm;
    struct wl_output *output;
    struct ext_idle_notifier_v1 *idle_notifier;
    struct zwlr_screencopy_manager_v1 *screencopy_manager;
    struct zwlr_gamma_control_manager_v1 *gamma_manager;
} wayland_globals_t;

int wayland_globals_bind(struct wl_display *display, wayland_globals_t *g);

#endif
