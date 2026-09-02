#ifndef WAYOLED_DIMMER_H
#define WAYOLED_DIMMER_H

#include <wayland-client.h>
#include <stdint.h>
#include "wlr-gamma-control-unstable-v1-client-protocol.h"

#define DIMMER_FADE_MS 300

typedef struct {
    struct zwlr_gamma_control_manager_v1 *manager;
    struct zwlr_gamma_control_v1 *control;
    uint32_t ramp_size;
    int available;
    int failed;

    double dim_factor;
    double temp_r;
    double temp_g;
    double temp_b;
    double gamma_r;
    double gamma_g;
    double gamma_b;

    int fading;
    double fade_from;
    double fade_to;
    long fade_start_ms;
    int fade_dur_ms;
} dimmer_t;

int dimmer_init(dimmer_t *dm, struct zwlr_gamma_control_manager_v1 *manager, struct wl_output *output);
int dimmer_confirm(dimmer_t *dm, struct wl_display *display);
void dimmer_render(dimmer_t *dm);
void dimmer_fade_start(dimmer_t *dm, double to, int duration_ms);
int dimmer_fade_tick(dimmer_t *dm);
void dimmer_reset(dimmer_t *dm);
void dimmer_set_colortemp(dimmer_t *dm, double r, double g, double b);
void dimmer_set_gamma(dimmer_t *dm, double r, double g, double b);
void dimmer_destroy(dimmer_t *dm);

#endif