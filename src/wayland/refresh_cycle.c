#define _GNU_SOURCE
#include "refresh_cycle.h"
#include "layer_shell_overlay.h"

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
#include <signal.h>

#define SEC_PER_COLOR 3
#define BAR_HEIGHT_PX 6

static volatile sig_atomic_t g_stop_requested = 0;

static void handle_sigterm(int signum) {
    (void)signum;
    g_stop_requested = 1;
}

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

    overlay_t overlay;

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
    st->buf_size = (size_t)st->overlay.width * st->overlay.height * 4;

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
                                            st->overlay.width, st->overlay.height,
                                            st->overlay.width * 4,
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
    int width = st->overlay.width;
    int height = st->overlay.height;

    uint32_t pixel = ((uint32_t)color.r << 16) |
                      ((uint32_t)color.g << 8) |
                       (uint32_t)color.b;

    uint32_t bar_pixel = ((uint32_t)(255 - color.r) << 16) |
                          ((uint32_t)(255 - color.g) << 8) |
                           (uint32_t)(255 - color.b);

    int bar_fill_px = (int)(width * progress);

    for (int y = 0; y < height; y++) {
        int in_bar_row = (y >= height - BAR_HEIGHT_PX);
        for (int x = 0; x < width; x++) {
            uint32_t val = pixel;
            if (in_bar_row && x < bar_fill_px)
                val = bar_pixel;
            st->pixels[y * width + x] = val;
        }
    }

    wl_surface_attach(st->overlay.surface, st->buffer, 0, 0);
    wl_surface_damage_buffer(st->overlay.surface, 0, 0, width, height);
    wl_surface_commit(st->overlay.surface);
}

int refresh_cycle_run(void) {
    struct sigaction sa = {0};
    sa.sa_handler = handle_sigterm;
    sigaction(SIGTERM, &sa, NULL);

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

    if (overlay_create(st.display, st.compositor, st.layer_shell, "wayoled-refresh", &st.overlay) != 0) {
        wl_display_disconnect(st.display);
        return -1;
    }

    if (create_shm_buffer(&st) != 0) {
        overlay_destroy(&st.overlay);
        wl_display_disconnect(st.display);
        return -1;
    }

    fprintf(stderr, "wayoled: starting pixel-refresh cycle (%dx%d)\n",
            st.overlay.width, st.overlay.height);

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

            if (st.overlay.configured < 0) {
                fprintf(stderr, "wayoled: surface closed, aborting refresh\n");
                destroy_shm_buffer(&st);
                overlay_destroy(&st.overlay);
                wl_display_disconnect(st.display);
                return -1;
            }

            if (g_stop_requested) {
                fprintf(stderr, "wayoled: refresh cycle cancelled\n");
                destroy_shm_buffer(&st);
                overlay_destroy(&st.overlay);
                wl_display_disconnect(st.display);
                return 1;
            }
        }
    }

    fprintf(stderr, "wayoled: pixel-refresh cycle complete\n");

    destroy_shm_buffer(&st);
    overlay_destroy(&st.overlay);
    wl_display_disconnect(st.display);

    return 0;
}
