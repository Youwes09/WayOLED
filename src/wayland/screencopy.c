#define _GNU_SOURCE
#include "screencopy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

static int format_supported(uint32_t format) {
    return format == WL_SHM_FORMAT_XRGB8888 ||
           format == WL_SHM_FORMAT_ARGB8888 ||
           format == WL_SHM_FORMAT_XBGR8888 ||
           format == WL_SHM_FORMAT_ABGR8888;
}

static void release_buffer(screencopy_t *sc) {
    if (sc->buffer) {
        wl_buffer_destroy(sc->buffer);
        sc->buffer = NULL;
    }
    if (sc->pool) {
        wl_shm_pool_destroy(sc->pool);
        sc->pool = NULL;
    }
    if (sc->pixels && sc->pixels != MAP_FAILED) {
        munmap(sc->pixels, sc->size);
        sc->pixels = NULL;
    }
    if (sc->fd >= 0) {
        close(sc->fd);
        sc->fd = -1;
    }
    sc->size = 0;
}

static int rebuild_buffer(screencopy_t *sc) {
    release_buffer(sc);

    if (sc->width <= 0 || sc->height <= 0 || !sc->have_format)
        return -1;

    sc->stride = sc->width * 4;
    sc->size = (size_t)sc->stride * sc->height;

    sc->fd = memfd_create("wayoled-screencopy", MFD_CLOEXEC);
    if (sc->fd < 0 || ftruncate(sc->fd, (off_t)sc->size) < 0) {
        release_buffer(sc);
        return -1;
    }

    sc->pixels = mmap(NULL, sc->size, PROT_READ | PROT_WRITE, MAP_SHARED, sc->fd, 0);
    if (sc->pixels == MAP_FAILED) {
        release_buffer(sc);
        return -1;
    }

    sc->pool = wl_shm_create_pool(sc->shm, sc->fd, (int32_t)sc->size);
    sc->buffer = wl_shm_pool_create_buffer(sc->pool, 0, sc->width, sc->height,
                                            sc->stride, sc->format);
    if (!sc->buffer) {
        release_buffer(sc);
        return -1;
    }

    sc->need_rebuild = 0;
    return 0;
}

static void session_buffer_size(void *data, struct ext_image_copy_capture_session_v1 *s,
                                 uint32_t width, uint32_t height) {
    (void)s;
    screencopy_t *sc = data;
    if ((int)width != sc->width || (int)height != sc->height) {
        sc->width = (int)width;
        sc->height = (int)height;
        sc->need_rebuild = 1;
    }
}

static void session_shm_format(void *data, struct ext_image_copy_capture_session_v1 *s,
                                uint32_t format) {
    (void)s;
    screencopy_t *sc = data;
    if (!sc->have_format && format_supported(format)) {
        sc->format = format;
        sc->have_format = 1;
        sc->need_rebuild = 1;
    }
}

static void session_dmabuf_device(void *data, struct ext_image_copy_capture_session_v1 *s,
                                   struct wl_array *device) {
    (void)data; (void)s; (void)device;
}

static void session_dmabuf_format(void *data, struct ext_image_copy_capture_session_v1 *s,
                                   uint32_t format, struct wl_array *modifiers) {
    (void)data; (void)s; (void)format; (void)modifiers;
}

static void session_done(void *data, struct ext_image_copy_capture_session_v1 *s) {
    (void)s;
    screencopy_t *sc = data;
    sc->constraints_done = 1;
}

static void session_stopped(void *data, struct ext_image_copy_capture_session_v1 *s) {
    (void)s;
    screencopy_t *sc = data;
    sc->stopped = 1;
}

static const struct ext_image_copy_capture_session_v1_listener session_listener = {
    .buffer_size = session_buffer_size,
    .shm_format = session_shm_format,
    .dmabuf_device = session_dmabuf_device,
    .dmabuf_format = session_dmabuf_format,
    .done = session_done,
    .stopped = session_stopped,
};

int screencopy_init(screencopy_t *sc, struct wl_shm *shm,
                     struct ext_output_image_capture_source_manager_v1 *source_manager,
                     struct ext_image_copy_capture_manager_v1 *capture_manager,
                     struct wl_output *output, struct wl_display *display) {
    memset(sc, 0, sizeof(*sc));
    sc->fd = -1;
    sc->shm = shm;
    sc->source_manager = source_manager;
    sc->capture_manager = capture_manager;
    sc->output = output;

    if (!shm || !source_manager || !capture_manager || !output) {
        fprintf(stderr, "wayoled: missing shm/image-capture-source/image-copy-capture/output\n");
        return -1;
    }

    sc->source = ext_output_image_capture_source_manager_v1_create_source(source_manager, output);
    sc->session = ext_image_copy_capture_manager_v1_create_session(capture_manager, sc->source, 0);
    ext_image_copy_capture_session_v1_add_listener(sc->session, &session_listener, sc);

    wl_display_roundtrip(display);

    if (sc->stopped || !sc->constraints_done || !sc->have_format) {
        fprintf(stderr, "wayoled: image-copy-capture session gave no usable shm format\n");
        screencopy_destroy(sc);
        return -1;
    }

    if (rebuild_buffer(sc) != 0) {
        fprintf(stderr, "wayoled: failed to allocate capture buffer\n");
        screencopy_destroy(sc);
        return -1;
    }

    return 0;
}

static void frame_transform(void *data, struct ext_image_copy_capture_frame_v1 *f, uint32_t transform) {
    (void)data; (void)f; (void)transform;
}

static void frame_damage(void *data, struct ext_image_copy_capture_frame_v1 *f,
                          int32_t x, int32_t y, int32_t width, int32_t height) {
    (void)data; (void)f; (void)x; (void)y; (void)width; (void)height;
}

static void frame_presentation_time(void *data, struct ext_image_copy_capture_frame_v1 *f,
                                     uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
    (void)data; (void)f; (void)tv_sec_hi; (void)tv_sec_lo; (void)tv_nsec;
}

static void frame_ready(void *data, struct ext_image_copy_capture_frame_v1 *f) {
    (void)f;
    screencopy_t *sc = data;
    sc->frame_ready = 1;
}

static void frame_failed(void *data, struct ext_image_copy_capture_frame_v1 *f, uint32_t reason) {
    (void)f;
    screencopy_t *sc = data;
    sc->frame_failed = 1;
    if (reason == EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS)
        sc->need_rebuild = 1;
    else if (reason == EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_STOPPED)
        sc->stopped = 1;
}

static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
    .transform = frame_transform,
    .damage = frame_damage,
    .presentation_time = frame_presentation_time,
    .ready = frame_ready,
    .failed = frame_failed,
};

int screencopy_capture(screencopy_t *sc, struct wl_display *display) {
    if (sc->stopped)
        return -1;

    if (sc->need_rebuild && rebuild_buffer(sc) != 0)
        return -1;
    if (!sc->buffer)
        return -1;

    sc->frame_ready = 0;
    sc->frame_failed = 0;

    struct ext_image_copy_capture_frame_v1 *frame =
        ext_image_copy_capture_session_v1_create_frame(sc->session);
    ext_image_copy_capture_frame_v1_add_listener(frame, &frame_listener, sc);
    ext_image_copy_capture_frame_v1_attach_buffer(frame, sc->buffer);
    ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0, sc->width, sc->height);
    ext_image_copy_capture_frame_v1_capture(frame);

    while (!sc->frame_ready && !sc->frame_failed) {
        if (wl_display_dispatch(display) < 0) {
            ext_image_copy_capture_frame_v1_destroy(frame);
            return -1;
        }
    }

    ext_image_copy_capture_frame_v1_destroy(frame);

    if (sc->frame_failed || !sc->pixels || sc->size == 0)
        return -1;

    return 0;
}

void screencopy_destroy(screencopy_t *sc) {
    release_buffer(sc);
    if (sc->session) {
        ext_image_copy_capture_session_v1_destroy(sc->session);
        sc->session = NULL;
    }
    if (sc->source) {
        ext_image_capture_source_v1_destroy(sc->source);
        sc->source = NULL;
    }
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
