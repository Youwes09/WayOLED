#include "wayland_globals.h"

#include <string.h>
#include <stdio.h>

static void output_name(void *data, struct wl_output *wl_output, const char *name) {
    (void)wl_output;
    wayoled_global_output_t *o = data;
    strncpy(o->name, name, WAYOLED_MONITOR_NAME_MAX - 1);
    o->name[WAYOLED_MONITOR_NAME_MAX - 1] = '\0';
}

static void output_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y,
                             int32_t w, int32_t h, int32_t subpixel, const char *make,
                             const char *model, int32_t transform) {
    (void)data; (void)wl_output; (void)x; (void)y; (void)w; (void)h;
    (void)subpixel; (void)make; (void)model; (void)transform;
}

static void output_mode(void *data, struct wl_output *wl_output, uint32_t flags,
                         int32_t w, int32_t h, int32_t refresh) {
    (void)data; (void)wl_output; (void)flags; (void)w; (void)h; (void)refresh;
}

static void output_done(void *data, struct wl_output *wl_output) {
    (void)data; (void)wl_output;
}

static void output_scale(void *data, struct wl_output *wl_output, int32_t factor) {
    (void)data; (void)wl_output; (void)factor;
}

static void output_description(void *data, struct wl_output *wl_output, const char *description) {
    (void)data; (void)wl_output; (void)description;
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

static void registry_global(void *data, struct wl_registry *registry,
                             uint32_t name, const char *interface,
                             uint32_t version) {
    wayland_globals_t *g = data;

    if (strcmp(interface, wl_seat_interface.name) == 0 && !g->seat) {
        g->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    } else if (strcmp(interface, ext_idle_notifier_v1_interface.name) == 0) {
        g->idle_notifier = wl_registry_bind(registry, name, &ext_idle_notifier_v1_interface, 1);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        g->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        g->screencopy_manager = wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, 1);
    } else if (strcmp(interface, zwlr_gamma_control_manager_v1_interface.name) == 0) {
        g->gamma_manager = wl_registry_bind(registry, name, &zwlr_gamma_control_manager_v1_interface, 1);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        g->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        if (g->output_count < WAYOLED_MAX_MONITORS) {
            uint32_t bind_ver = version < 4 ? version : 4;
            wayoled_global_output_t *o = &g->outputs[g->output_count++];
            o->output = wl_registry_bind(registry, name, &wl_output_interface, bind_ver);
            snprintf(o->name, WAYOLED_MONITOR_NAME_MAX, "output-%d", g->output_count - 1);
            wl_output_add_listener(o->output, &output_listener, o);
        }
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

int wayland_globals_bind(struct wl_display *display, wayland_globals_t *g) {
    memset(g, 0, sizeof(*g));

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, g);
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);

    return g->output_count > 0 ? 0 : -1;
}
