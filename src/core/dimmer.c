#define _GNU_SOURCE
#include "dimmer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
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

    if (!manager) {
        fprintf(stderr, "wayoled: gamma-control unavailable, dimming disabled\n");
        return -1;
    }

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

void dimmer_set_factor(dimmer_t *dm, double factor) {
    if (!dm->available || !dm->control)
        return;

    size_t table_size = (size_t)dm->ramp_size * 3 * sizeof(uint16_t);
    uint16_t *table = malloc(table_size);
    if (!table)
        return;

    for (uint32_t i = 0; i < dm->ramp_size; i++) {
        double base = (double)i * 65535.0 / (double)(dm->ramp_size - 1);
        uint16_t v = (uint16_t)(base * factor);
        table[i] = v;
        table[dm->ramp_size + i] = v;
        table[2 * dm->ramp_size + i] = v;
    }

    int fd = memfd_create("wayoled-gamma", MFD_CLOEXEC);
    if (fd < 0) {
        free(table);
        return;
    }

    ftruncate(fd, (off_t)table_size);
    write(fd, table, table_size);
    lseek(fd, 0, SEEK_SET);

    zwlr_gamma_control_v1_set_gamma(dm->control, fd);

    close(fd);
    free(table);
}

void dimmer_transition(dimmer_t *dm, struct wl_display *display, double from, double to, int steps, int step_us) {
    if (!dm->available)
        return;

    for (int i = 1; i <= steps; i++) {
        double factor = from + (to - from) * ((double)i / steps);
        dimmer_set_factor(dm, factor);
        wl_display_flush(display);
        usleep(step_us);
    }
}

void dimmer_destroy(dimmer_t *dm) {
    if (dm->control)
        zwlr_gamma_control_v1_destroy(dm->control);
}
