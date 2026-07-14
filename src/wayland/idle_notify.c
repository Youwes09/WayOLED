#include "idle_notify.h"
#include "ext-idle-notify-v1-client-protocol.h"

#include <stdio.h>
#include <string.h>

static void idle_notif_idled(void *data, struct ext_idle_notification_v1 *n) {
    (void)n;
    idle_watch_t *iw = data;
    iw->is_idle = 1;
}

static void idle_notif_resumed(void *data, struct ext_idle_notification_v1 *n) {
    (void)n;
    idle_watch_t *iw = data;
    iw->is_idle = 0;
}

static const struct ext_idle_notification_v1_listener idle_notif_listener = {
    .idled = idle_notif_idled,
    .resumed = idle_notif_resumed,
};

int idle_watch_init(idle_watch_t *iw, struct wl_seat *seat,
                     struct ext_idle_notifier_v1 *notifier, uint32_t timeout_ms) {
    memset(iw, 0, sizeof(*iw));
    iw->timeout_ms = timeout_ms;
    iw->seat = seat;
    iw->notifier = notifier;

    if (!iw->seat || !iw->notifier) {
        fprintf(stderr, "wayoled: missing wl_seat or ext_idle_notifier_v1\n");
        return -1;
    }

    iw->notification = ext_idle_notifier_v1_get_idle_notification(
        iw->notifier, timeout_ms, iw->seat);
    ext_idle_notification_v1_add_listener(iw->notification,
                                           &idle_notif_listener, iw);
    return 0;
}

void idle_watch_destroy(idle_watch_t *iw) {
    if (iw->notification)
        ext_idle_notification_v1_destroy(iw->notification);
}
