#ifndef WAYOLED_WAYLAND_GLOBALS_H
#define WAYOLED_WAYLAND_GLOBALS_H

#include <wayland-client.h>
#include "wlr-gamma-control-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "ext-idle-notify-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"

#define WAYOLED_MAX_MONITORS 8
#define WAYOLED_MONITOR_NAME_MAX 32

typedef struct {
    struct wl_output *output;
    char name[WAYOLED_MONITOR_NAME_MAX];
} wayoled_global_output_t;

typedef struct {
    struct wl_seat *seat;
    struct wl_shm *shm;
    struct ext_idle_notifier_v1 *idle_notifier;
    struct ext_output_image_capture_source_manager_v1 *image_source_manager;
    struct ext_image_copy_capture_manager_v1 *image_copy_manager;
    struct zwlr_gamma_control_manager_v1 *gamma_manager;
    struct zwlr_layer_shell_v1 *layer_shell;
    wayoled_global_output_t outputs[WAYOLED_MAX_MONITORS];
    int output_count;
} wayland_globals_t;

int wayland_globals_bind(struct wl_display *display, wayland_globals_t *g);

#endif
