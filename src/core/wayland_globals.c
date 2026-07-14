#include "wayland_globals.h"

#include <string.h>

static void registry_global(void *data, struct wl_registry *registry,
                             uint32_t name, const char *interface,
                             uint32_t version) {
    wayland_globals_t *g = data;
    (void)version;

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
    } else if (strcmp(interface, wl_output_interface.name) == 0 && !g->output) {
        g->output = wl_registry_bind(registry, name, &wl_output_interface, 2);
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

    return 0;
}
