#include "layer_shell_overlay.h"

#include <stdio.h>
#include <string.h>

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *ls,
                                     uint32_t serial, uint32_t width, uint32_t height) {
    overlay_t *ov = data;

    zwlr_layer_surface_v1_ack_configure(ls, serial);

    ov->width = (int)width;
    ov->height = (int)height;
    ov->configured = 1;
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
    (void)ls;
    overlay_t *ov = data;
    ov->configured = -1;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

int overlay_create(struct wl_display *display, struct wl_compositor *compositor,
                    struct zwlr_layer_shell_v1 *layer_shell, const char *namespace,
                    overlay_t *ov) {
    memset(ov, 0, sizeof(*ov));

    ov->surface = wl_compositor_create_surface(compositor);
    ov->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        layer_shell, ov->surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, namespace);

    zwlr_layer_surface_v1_add_listener(ov->layer_surface, &layer_surface_listener, ov);

    zwlr_layer_surface_v1_set_anchor(ov->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(ov->layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(ov->layer_surface, 0);

    wl_surface_commit(ov->surface);
    wl_display_roundtrip(display);

    if (ov->configured != 1 || ov->width <= 0 || ov->height <= 0) {
        fprintf(stderr, "wayoled: layer surface failed to configure\n");
        overlay_destroy(ov);
        return -1;
    }

    return 0;
}

void overlay_destroy(overlay_t *ov) {
    if (ov->layer_surface)
        zwlr_layer_surface_v1_destroy(ov->layer_surface);
    if (ov->surface)
        wl_surface_destroy(ov->surface);
    ov->layer_surface = NULL;
    ov->surface = NULL;
}
