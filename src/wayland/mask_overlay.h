#ifndef WAYOLED_MASK_OVERLAY_H
#define WAYOLED_MASK_OVERLAY_H

#include <wayland-client.h>
#include <stddef.h>
#include "layer_shell_overlay.h"

typedef struct {
    overlay_t overlay;

    struct wl_shm_pool *pool;
    struct wl_buffer *buffer;
    void *pixels;
    int fd;
    size_t size;

    int active;
    int phase;
    double density;
} mask_overlay_t;

int mask_engage(mask_overlay_t *m, struct wl_display *display, struct wl_compositor *compositor,
                 struct zwlr_layer_shell_v1 *layer_shell, struct wl_shm *shm,
                 struct wl_output *output, double density, const wayoled_rect_t *rect);
void mask_shift(mask_overlay_t *m);
void mask_disengage(mask_overlay_t *m);

#endif
