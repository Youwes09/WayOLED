#ifndef WAYOLED_INOTIFY_WATCH_H
#define WAYOLED_INOTIFY_WATCH_H

#include "backlight.h"

typedef struct {
    int inotify_fd;
    int watch_fd;
} backlight_watcher_t;

int watcher_init(backlight_watcher_t *watcher, backlight_dev_t *dev);

int watcher_poll(backlight_watcher_t *watcher, backlight_dev_t *dev);

void watcher_close(backlight_watcher_t *watcher);

#endif
