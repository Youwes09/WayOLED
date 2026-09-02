#include "wayland_globals.h"

#include <string.h>

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
    } else if (strcmp(interface, wl_output_interface.name) == 0 && g->output_added) {
        uint32_t bind_ver = version < 4 ? version : 4;
        struct wl_output *output = wl_registry_bind(registry, name, &wl_output_interface, bind_ver);
        g->output_added(g->user, output, name, bind_ver);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)registry;
    wayland_globals_t *g = data;
    if (g->output_removed)
        g->output_removed(g->user, name);
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

int wayland_globals_bind(struct wl_display *display, wayland_globals_t *g) {
    g->registry = wl_display_get_registry(display);
    wl_registry_add_listener(g->registry, &registry_listener, g);
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);

    return 0;
}
