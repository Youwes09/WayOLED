#define _GNU_SOURCE
#include "mask_overlay.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

static const int bayer4[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5},
};

static void paint_mask(mask_overlay_t *m) {
    int width = m->overlay.width;
    int height = m->overlay.height;
    int threshold = (int)(m->density * 16.0 + 0.5);
    int px = m->phase & 3;
    int py = (m->phase >> 2) & 3;
    uint32_t *pixels = m->pixels;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int off = bayer4[(y + py) & 3][(x + px) & 3] < threshold;
            pixels[y * width + x] = off ? 0xFF000000u : 0x00000000u;
        }
    }

    wl_surface_attach(m->overlay.surface, m->buffer, 0, 0);
    wl_surface_damage_buffer(m->overlay.surface, 0, 0, width, height);
    wl_surface_commit(m->overlay.surface);
}

static void release_buffer(mask_overlay_t *m) {
    if (m->buffer) {
        wl_buffer_destroy(m->buffer);
        m->buffer = NULL;
    }
    if (m->pool) {
        wl_shm_pool_destroy(m->pool);
        m->pool = NULL;
    }
    if (m->pixels && m->pixels != MAP_FAILED) {
        munmap(m->pixels, m->size);
        m->pixels = NULL;
    }
    if (m->fd >= 0) {
        close(m->fd);
        m->fd = -1;
    }
}

int mask_engage(mask_overlay_t *m, struct wl_display *display, struct wl_compositor *compositor,
                 struct zwlr_layer_shell_v1 *layer_shell, struct wl_shm *shm,
                 struct wl_output *output, double density, const wayoled_rect_t *rect) {
    if (m->active)
        return 0;

    memset(m, 0, sizeof(*m));
    m->fd = -1;

    if (overlay_create(display, compositor, layer_shell, output, "wayoled-mask", rect, &m->overlay) != 0)
        return -1;

    m->size = (size_t)m->overlay.width * m->overlay.height * 4;
    m->fd = memfd_create("wayoled-mask", MFD_CLOEXEC);
    if (m->fd < 0 || ftruncate(m->fd, (off_t)m->size) < 0) {
        release_buffer(m);
        overlay_destroy(&m->overlay);
        return -1;
    }

    m->pixels = mmap(NULL, m->size, PROT_READ | PROT_WRITE, MAP_SHARED, m->fd, 0);
    if (m->pixels == MAP_FAILED) {
        release_buffer(m);
        overlay_destroy(&m->overlay);
        return -1;
    }

    m->pool = wl_shm_create_pool(shm, m->fd, (int32_t)m->size);
    m->buffer = wl_shm_pool_create_buffer(m->pool, 0, m->overlay.width, m->overlay.height,
                                          m->overlay.width * 4, WL_SHM_FORMAT_ARGB8888);
    if (!m->buffer) {
        release_buffer(m);
        overlay_destroy(&m->overlay);
        return -1;
    }

    if (density < 0.0) density = 0.0;
    if (density > 1.0) density = 1.0;

    m->density = density;
    m->phase = 0;
    paint_mask(m);
    m->active = 1;
    return 0;
}

void mask_shift(mask_overlay_t *m) {
    if (!m->active)
        return;

    m->phase = (m->phase + 1) & 15;
    paint_mask(m);
}

void mask_disengage(mask_overlay_t *m) {
    if (!m->active)
        return;

    release_buffer(m);
    overlay_destroy(&m->overlay);
    memset(m, 0, sizeof(*m));
    m->fd = -1;
}
