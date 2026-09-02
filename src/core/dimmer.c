#define _GNU_SOURCE
#include "dimmer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <sys/mman.h>

static void gamma_size_event(void *data, struct zwlr_gamma_control_v1 *gc, uint32_t size) {
    (void)gc;
    dimmer_t *dm = data;
    dm->ramp_size = size;
}

static void gamma_failed_event(void *data, struct zwlr_gamma_control_v1 *gc) {
    (void)gc;
    dimmer_t *dm = data;
    dm->failed = 1;
}

static const struct zwlr_gamma_control_v1_listener gamma_listener = {
    .gamma_size = gamma_size_event,
    .failed = gamma_failed_event,
};

int dimmer_init(dimmer_t *dm, struct zwlr_gamma_control_manager_v1 *manager, struct wl_output *output) {
    memset(dm, 0, sizeof(*dm));
    dm->manager = manager;
    dm->dim_factor = 1.0;
    dm->temp_r = 1.0;
    dm->temp_g = 1.0;
    dm->temp_b = 1.0;
    dm->gamma_r = 1.0;
    dm->gamma_g = 1.0;
    dm->gamma_b = 1.0;

    if (!manager)
        return -1;

    dm->control = zwlr_gamma_control_manager_v1_get_gamma_control(manager, output);
    zwlr_gamma_control_v1_add_listener(dm->control, &gamma_listener, dm);
    return 0;
}

int dimmer_confirm(dimmer_t *dm, struct wl_display *display) {
    wl_display_roundtrip(display);

    if (dm->failed || dm->ramp_size == 0) {
        if (dm->control)
            zwlr_gamma_control_v1_destroy(dm->control);
        dm->control = NULL;
        dm->available = 0;
        fprintf(stderr, "wayoled: gamma control unavailable, dimming disabled\n");
        return -1;
    }

    dm->available = 1;
    return 0;
}

void dimmer_render(dimmer_t *dm) {
    if (!dm->available || !dm->control)
        return;

    size_t table_size = (size_t)dm->ramp_size * 3 * sizeof(uint16_t);
    uint16_t *table = malloc(table_size);
    if (!table)
        return;

    double rf = dm->dim_factor * dm->temp_r;
    double gf = dm->dim_factor * dm->temp_g;
    double bf = dm->dim_factor * dm->temp_b;

    if (rf > 1.0) rf = 1.0;
    if (gf > 1.0) gf = 1.0;
    if (bf > 1.0) bf = 1.0;

    double inv_r = 1.0 / dm->gamma_r;
    double inv_g = 1.0 / dm->gamma_g;
    double inv_b = 1.0 / dm->gamma_b;
    double last = (double)(dm->ramp_size - 1);

    for (uint32_t i = 0; i < dm->ramp_size; i++) {
        double norm = (double)i / last;
        table[i] = (uint16_t)(pow(norm, inv_r) * rf * 65535.0);
        table[dm->ramp_size + i] = (uint16_t)(pow(norm, inv_g) * gf * 65535.0);
        table[2 * dm->ramp_size + i] = (uint16_t)(pow(norm, inv_b) * bf * 65535.0);
    }

    int fd = memfd_create("wayoled-gamma", MFD_CLOEXEC);
    if (fd < 0) {
        free(table);
        return;
    }

    if (ftruncate(fd, (off_t)table_size) < 0 ||
        write(fd, table, table_size) != (ssize_t)table_size) {
        close(fd);
        free(table);
        return;
    }
    lseek(fd, 0, SEEK_SET);

    zwlr_gamma_control_v1_set_gamma(dm->control, fd);

    close(fd);
    free(table);
}

static long dimmer_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void dimmer_fade_start(dimmer_t *dm, double to, int duration_ms) {
    if (!dm->available)
        return;

    dm->fade_from = dm->dim_factor;
    dm->fade_to = to;
    dm->fade_dur_ms = duration_ms > 0 ? duration_ms : 1;
    dm->fade_start_ms = dimmer_now_ms();
    dm->fading = 1;
}

int dimmer_fade_tick(dimmer_t *dm) {
    if (!dm->fading)
        return 0;

    double t = (double)(dimmer_now_ms() - dm->fade_start_ms) / dm->fade_dur_ms;
    if (t >= 1.0) {
        dm->dim_factor = dm->fade_to;
        dm->fading = 0;
    } else {
        dm->dim_factor = dm->fade_from + (dm->fade_to - dm->fade_from) * t;
    }

    dimmer_render(dm);
    return dm->fading;
}

void dimmer_reset(dimmer_t *dm) {
    dm->fading = 0;
    dm->dim_factor = 1.0;
    dm->temp_r = dm->temp_g = dm->temp_b = 1.0;
    dm->gamma_r = dm->gamma_g = dm->gamma_b = 1.0;
    dimmer_render(dm);
}

void dimmer_set_colortemp(dimmer_t *dm, double r, double g, double b) {
    dm->temp_r = r;
    dm->temp_g = g;
    dm->temp_b = b;
    dimmer_render(dm);
}

void dimmer_set_gamma(dimmer_t *dm, double r, double g, double b) {
    dm->gamma_r = r;
    dm->gamma_g = g;
    dm->gamma_b = b;
    dimmer_render(dm);
}

void dimmer_destroy(dimmer_t *dm) {
    if (dm->control)
        zwlr_gamma_control_v1_destroy(dm->control);
}