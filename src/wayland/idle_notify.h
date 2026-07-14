#ifndef WAYOLED_IDLE_NOTIFY_H
#define WAYOLED_IDLE_NOTIFY_H

#include <wayland-client.h>

typedef struct {
    struct wl_seat *seat;
    struct ext_idle_notifier_v1 *notifier;
    struct ext_idle_notification_v1 *notification;
    int is_idle;
    uint32_t timeout_ms;
} idle_watch_t;

int idle_watch_init(idle_watch_t *iw, struct wl_seat *seat,
                     struct ext_idle_notifier_v1 *notifier, uint32_t timeout_ms);
void idle_watch_destroy(idle_watch_t *iw);

#endif
