#ifndef WAYOLED_LAYER_SHELL_OVERLAY_H
#define WAYOLED_LAYER_SHELL_OVERLAY_H

#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

typedef struct {
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;

    int width;
    int height;

    // 0 = awaiting initial configure, 1 = configured, -1 = compositor closed it
    int configured;
} overlay_t;

int overlay_create(struct wl_display *display, struct wl_compositor *compositor,
                    struct zwlr_layer_shell_v1 *layer_shell, const char *namespace,
                    overlay_t *ov);

void overlay_destroy(overlay_t *ov);

#endif
