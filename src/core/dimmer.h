#ifndef WAYOLED_DIMMER_H
#define WAYOLED_DIMMER_H

#include <wayland-client.h>
#include <stdint.h>
#include "wlr-gamma-control-unstable-v1-client-protocol.h"

typedef struct {
    struct zwlr_gamma_control_manager_v1 *manager;
    struct zwlr_gamma_control_v1 *control;
    uint32_t ramp_size;
    int available;
    int failed;
} dimmer_t;

int dimmer_init(dimmer_t *dm, struct zwlr_gamma_control_manager_v1 *manager, struct wl_output *output);
int dimmer_confirm(dimmer_t *dm, struct wl_display *display);
void dimmer_set_factor(dimmer_t *dm, double factor);
void dimmer_transition(dimmer_t *dm, struct wl_display *display, double from, double to, int steps, int step_us);
void dimmer_destroy(dimmer_t *dm);

#endif
