#ifndef WAYOLED_SCREENCOPY_H
#define WAYOLED_SCREENCOPY_H

#include <wayland-client.h>
#include <stdint.h>
#include <stddef.h>

#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"

typedef struct {
    struct wl_shm *shm;
    struct ext_output_image_capture_source_manager_v1 *source_manager;
    struct ext_image_copy_capture_manager_v1 *capture_manager;
    struct wl_output *output;

    struct ext_image_capture_source_v1 *source;
    struct ext_image_copy_capture_session_v1 *session;

    struct wl_shm_pool *pool;
    struct wl_buffer *buffer;
    void *pixels;
    int fd;
    size_t size;

    int width;
    int height;
    int stride;
    uint32_t format;

    int have_format;
    int constraints_done;
    int stopped;
    int need_rebuild;

    int frame_ready;
    int frame_failed;
} screencopy_t;

int screencopy_init(screencopy_t *sc, struct wl_shm *shm,
                     struct ext_output_image_capture_source_manager_v1 *source_manager,
                     struct ext_image_copy_capture_manager_v1 *capture_manager,
                     struct wl_output *output, struct wl_display *display);

int screencopy_capture(screencopy_t *sc, struct wl_display *display);
void screencopy_destroy(screencopy_t *sc);

uint64_t *screencopy_grid_hashes(screencopy_t *sc, int block_size, int *out_count);

double screencopy_grid_diff_ratio(const uint64_t *a, const uint64_t *b, int count);

#endif
