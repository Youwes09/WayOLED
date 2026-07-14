#ifndef WAYOLED_SCREENCOPY_H
#define WAYOLED_SCREENCOPY_H

#include <wayland-client.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    struct wl_shm *shm;
    struct zwlr_screencopy_manager_v1 *manager;
    struct wl_output *output;

    void *pixels;
    int fd;
    size_t size;
    int width, height, stride;

    int done;
    int failed;
} screencopy_t;

int screencopy_init(screencopy_t *sc, struct wl_shm *shm,
                     struct zwlr_screencopy_manager_v1 *manager,
                     struct wl_output *output);

int screencopy_capture(screencopy_t *sc, struct wl_display *display);
void screencopy_destroy(screencopy_t *sc);

uint64_t *screencopy_grid_hashes(screencopy_t *sc, int block_size, int *out_count);

// Compares two grid-hash arrays of equal length, returns the fraction
// (0.0-1.0) of blocks that differ.
double screencopy_grid_diff_ratio(const uint64_t *a, const uint64_t *b, int count);

#endif
