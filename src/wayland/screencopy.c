#define _GNU_SOURCE
#include "screencopy.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

int screencopy_init(screencopy_t *sc, struct wl_shm *shm,
                     struct zwlr_screencopy_manager_v1 *manager,
                     struct wl_output *output) {
    memset(sc, 0, sizeof(*sc));
    sc->fd = -1;
    sc->shm = shm;
    sc->manager = manager;
    sc->output = output;

    if (!sc->shm || !sc->manager || !sc->output) {
        fprintf(stderr, "wayoled: missing shm/screencopy-manager/output\n");
        return -1;
    }

    return 0;
}

static void buffer_event(void *data, struct zwlr_screencopy_frame_v1 *frame,
                          uint32_t format, uint32_t width, uint32_t height,
                          uint32_t stride) {
    screencopy_t *sc = data;

    sc->width = (int)width;
    sc->height = (int)height;
    sc->stride = (int)stride;
    sc->size = (size_t)stride * height;

    if (sc->pixels && sc->pixels != MAP_FAILED)
        munmap(sc->pixels, sc->size);
    if (sc->fd >= 0)
        close(sc->fd);

    sc->fd = memfd_create("wayoled-screencopy", MFD_CLOEXEC);
    if (sc->fd < 0 || ftruncate(sc->fd, (off_t)sc->size) < 0) {
        sc->failed = 1;
        return;
    }

    sc->pixels = mmap(NULL, sc->size, PROT_READ | PROT_WRITE, MAP_SHARED, sc->fd, 0);
    if (sc->pixels == MAP_FAILED) {
        sc->failed = 1;
        return;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(sc->shm, sc->fd, (int32_t)sc->size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(
        pool, 0, sc->width, sc->height, sc->stride, format);
    wl_shm_pool_destroy(pool);

    zwlr_screencopy_frame_v1_copy(frame, buffer);
}

static void flags_event(void *data, struct zwlr_screencopy_frame_v1 *frame,
                         uint32_t flags) {
    (void)data; (void)frame; (void)flags;
}

static void ready_event(void *data, struct zwlr_screencopy_frame_v1 *frame,
                         uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
    (void)frame; (void)tv_sec_hi; (void)tv_sec_lo; (void)tv_nsec;
    screencopy_t *sc = data;
    sc->done = 1;
}

static void failed_event(void *data, struct zwlr_screencopy_frame_v1 *frame) {
    (void)frame;
    screencopy_t *sc = data;
    sc->failed = 1;
}

static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
    .buffer = buffer_event,
    .flags = flags_event,
    .ready = ready_event,
    .failed = failed_event,
};

int screencopy_capture(screencopy_t *sc, struct wl_display *display) {
    sc->done = 0;
    sc->failed = 0;

    struct zwlr_screencopy_frame_v1 *frame =
        zwlr_screencopy_manager_v1_capture_output(sc->manager, 0, sc->output);
    zwlr_screencopy_frame_v1_add_listener(frame, &frame_listener, sc);

    while (!sc->done && !sc->failed) {
        if (wl_display_dispatch(display) < 0) {
            zwlr_screencopy_frame_v1_destroy(frame);
            return -1;
        }
    }

    zwlr_screencopy_frame_v1_destroy(frame);

    if (sc->failed || sc->fd < 0 || sc->size == 0)
        return -1;

    return 0;
}

void screencopy_destroy(screencopy_t *sc) {
    if (sc->pixels && sc->pixels != MAP_FAILED)
        munmap(sc->pixels, sc->size);
    if (sc->fd >= 0)
        close(sc->fd);
}

uint64_t *screencopy_grid_hashes(screencopy_t *sc, int block_size, int *out_count) {
    if (!sc->pixels || sc->width <= 0 || sc->height <= 0)
        return NULL;

    int cols = (sc->width + block_size - 1) / block_size;
    int rows = (sc->height + block_size - 1) / block_size;
    int count = cols * rows;

    uint64_t *hashes = calloc((size_t)count, sizeof(uint64_t));
    if (!hashes)
        return NULL;

    const uint8_t *bytes = sc->pixels;

    for (int by = 0; by < rows; by++) {
        for (int bx = 0; bx < cols; bx++) {
            uint64_t h = 1469598103934665603ULL;

            int x0 = bx * block_size;
            int y0 = by * block_size;
            int x1 = x0 + block_size < sc->width ? x0 + block_size : sc->width;
            int y1 = y0 + block_size < sc->height ? y0 + block_size : sc->height;

            for (int y = y0; y < y1; y++) {
                const uint8_t *row = bytes + (size_t)y * sc->stride;
                for (int x = x0; x < x1; x += 2) {
                    h ^= row[x * 4];
                    h *= 1099511628211ULL;
                }
            }

            hashes[by * cols + bx] = h;
        }
    }

    *out_count = count;
    return hashes;
}

double screencopy_grid_diff_ratio(const uint64_t *a, const uint64_t *b, int count) {
    if (count <= 0)
        return 1.0;

    int diff = 0;
    for (int i = 0; i < count; i++) {
        if (a[i] != b[i])
            diff++;
    }

    return (double)diff / (double)count;
}
