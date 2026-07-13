#define _GNU_SOURCE
#include "refresh_cycle.h"

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#define SEC_PER_COLOR 3
#define BAR_HEIGHT_PX 6

typedef struct { uint8_t r, g, b; } rgb_t;

static const rgb_t sequence[] = {
    {255, 255, 255}, // white
    {128, 128, 128}, // 50% gray
    {0,   0,   0},   // black
    {255, 0,   0},   // red
    {0,   255, 0},   // green
    {0,   0,   255}, // blue
};
#define SEQ_LEN (sizeof(sequence) / sizeof(sequence[0]))

typedef struct {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;

    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;

    int width, height;
    int configured;

    struct wl_shm_pool *pool;
    struct wl_buffer *buffer;
    uint32_t *pixels;
    int buf_fd;
    size_t buf_size;
} state_t;

static void registry_global(void *data, struct wl_registry *registry,
                             uint32_t name, const char *interface,
                             uint32_t version) {
    state_t *st = data;
    (void)version;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        st->compositor = wl_registry_bind(registry, name,
                                           &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        st->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        st->layer_shell = wl_registry_bind(registry, name,
                                            &zwlr_layer_shell_v1_interface, 1);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                    uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static int create_shm_buffer(state_t *st) {
    st->buf_size = (size_t)st->width * st->height * 4;

    st->buf_fd = memfd_create("wayoled-refresh", MFD_CLOEXEC);
    if (st->buf_fd < 0) {
        fprintf(stderr, "wayoled: memfd_create failed: %s\n", strerror(errno));
        return -1;
    }

    if (ftruncate(st->buf_fd, (off_t)st->buf_size) < 0) {
        fprintf(stderr, "wayoled: ftruncate failed: %s\n", strerror(errno));
        close(st->buf_fd);
        return -1;
    }

    st->pixels = mmap(NULL, st->buf_size, PROT_READ | PROT_WRITE,
                       MAP_SHARED, st->buf_fd, 0);
    if (st->pixels == MAP_FAILED) {
        fprintf(stderr, "wayoled: mmap failed: %s\n", strerror(errno));
        close(st->buf_fd);
        return -1;
    }

    st->pool = wl_shm_create_pool(st->shm, st->buf_fd, (int32_t)st->buf_size);
    st->buffer = wl_shm_pool_create_buffer(st->pool, 0,
                                            st->width, st->height,
                                            st->width * 4,
                                            WL_SHM_FORMAT_XRGB8888);
    return 0;
}

static void destroy_shm_buffer(state_t *st) {
    if (st->buffer) wl_buffer_destroy(st->buffer);
    if (st->pool) wl_shm_pool_destroy(st->pool);
    if (st->pixels && st->pixels != MAP_FAILED)
        munmap(st->pixels, st->buf_size);
    if (st->buf_fd >= 0) close(st->buf_fd);
}

static void paint_frame(state_t *st, rgb_t color, double progress) {
    uint32_t pixel = ((uint32_t)color.r << 16) |
                      ((uint32_t)color.g << 8) |
                       (uint32_t)color.b;

    uint32_t bar_pixel = ((uint32_t)(255 - color.r) << 16) |
                          ((uint32_t)(255 - color.g) << 8) |
                           (uint32_t)(255 - color.b);

    int bar_fill_px = (int)(st->width * progress);

    for (int y = 0; y < st->height; y++) {
        int in_bar_row = (y >= st->height - BAR_HEIGHT_PX);
        for (int x = 0; x < st->width; x++) {
            uint32_t val = pixel;
            if (in_bar_row && x < bar_fill_px)
                val = bar_pixel;
            st->pixels[y * st->width + x] = val;
        }
    }

    wl_surface_attach(st->surface, st->buffer, 0, 0);
    wl_surface_damage_buffer(st->surface, 0, 0, st->width, st->height);
    wl_surface_commit(st->surface);
}

static void layer_surface_configure(void *data,
                                     struct zwlr_layer_surface_v1 *ls,
                                     uint32_t serial,
                                     uint32_t width, uint32_t height) {
    state_t *st = data;

    zwlr_layer_surface_v1_ack_configure(ls, serial);

    st->width = (int)width;
    st->height = (int)height;
    st->configured = 1;
}

static void layer_surface_closed(void *data,
                                  struct zwlr_layer_surface_v1 *ls) {
    (void)ls;
    state_t *st = data;
    st->configured = -1;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

int refresh_cycle_run(void) {
    state_t st = {0};
    st.buf_fd = -1;

    st.display = wl_display_connect(NULL);
    if (!st.display) {
        fprintf(stderr, "wayoled: failed to connect to Wayland display\n");
        return -1;
    }

    st.registry = wl_display_get_registry(st.display);
    wl_registry_add_listener(st.registry, &registry_listener, &st);
    wl_display_roundtrip(st.display);

    if (!st.compositor || !st.shm || !st.layer_shell) {
        fprintf(stderr, "wayoled: compositor missing required globals "
                         "(compositor=%p shm=%p layer_shell=%p)\n",
                (void *)st.compositor, (void *)st.shm, (void *)st.layer_shell);
        return -1;
    }

    st.surface = wl_compositor_create_surface(st.compositor);
    st.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        st.layer_shell, st.surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wayoled-refresh");

    zwlr_layer_surface_v1_add_listener(st.layer_surface,
                                        &layer_surface_listener, &st);

    zwlr_layer_surface_v1_set_anchor(st.layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(st.layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(st.layer_surface, 0);

    wl_surface_commit(st.surface);
    wl_display_roundtrip(st.display);

    if (st.configured != 1 || st.width <= 0 || st.height <= 0) {
        fprintf(stderr, "wayoled: layer surface failed to configure\n");
        return -1;
    }

    if (create_shm_buffer(&st) != 0)
        return -1;

    fprintf(stderr, "wayoled: starting pixel-refresh cycle (%dx%d)\n",
            st.width, st.height);

    for (size_t i = 0; i < SEQ_LEN; i++) {
        struct timespec start, now;
        clock_gettime(CLOCK_MONOTONIC, &start);

        for (;;) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = (now.tv_sec - start.tv_sec) +
                              (now.tv_nsec - start.tv_nsec) / 1e9;

            if (elapsed >= SEC_PER_COLOR)
                break;

            double color_progress = elapsed / SEC_PER_COLOR;
            double overall_progress = (i + color_progress) / SEQ_LEN;

            paint_frame(&st, sequence[i], overall_progress);
            wl_display_flush(st.display);

            usleep(16667); // ~60Hz
            wl_display_dispatch_pending(st.display);

            if (st.configured < 0) {
                fprintf(stderr, "wayoled: surface closed, aborting refresh\n");
                destroy_shm_buffer(&st);
                return -1;
            }
        }
    }

    fprintf(stderr, "wayoled: pixel-refresh cycle complete\n");

    destroy_shm_buffer(&st);
    zwlr_layer_surface_v1_destroy(st.layer_surface);
    wl_surface_destroy(st.surface);
    wl_display_disconnect(st.display);

    return 0;
}
