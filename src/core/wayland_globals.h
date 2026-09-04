#ifndef WAYOLED_WAYLAND_GLOBALS_H
#define WAYOLED_WAYLAND_GLOBALS_H

#include <wayland-client.h>
#include "wlr-gamma-control-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "ext-idle-notify-v1-client-protocol.h"

#define WAYOLED_MAX_MONITORS 8
#define WAYOLED_MONITOR_NAME_MAX 32

typedef struct {
    struct wl_seat *seat;
    struct wl_shm *shm;
    struct wl_compositor *compositor;
    struct ext_idle_notifier_v1 *idle_notifier;
    struct zwlr_screencopy_manager_v1 *screencopy_manager;
    struct zwlr_gamma_control_manager_v1 *gamma_manager;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wl_registry *registry;

    void (*output_added)(void *user, struct wl_output *output, uint32_t registry_name, uint32_t version);
    void (*output_removed)(void *user, uint32_t registry_name);
    void *user;
} wayland_globals_t;

int wayland_globals_bind(struct wl_display *display, wayland_globals_t *g);

#endif
