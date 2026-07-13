#include "inotify_watch.h"

#include <sys/inotify.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>

#define EVENT_BUF_LEN (16 * (sizeof(struct inotify_event) + 16))

int watcher_init(backlight_watcher_t *watcher, backlight_dev_t *dev) {
    watcher->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (watcher->inotify_fd < 0) {
        fprintf(stderr, "wayoled: inotify_init1 failed: %s\n", strerror(errno));
        return -1;
    }

    watcher->watch_fd = inotify_add_watch(watcher->inotify_fd,
                                           dev->device_path,
                                           IN_MODIFY | IN_CLOSE_WRITE);

    if (watcher->watch_fd < 0) {
        fprintf(stderr, "wayoled: inotify_add_watch failed on %s: %s\n",
                dev->device_path, strerror(errno));
        close(watcher->inotify_fd);
        return -1;
    }

    return 0;
}

int watcher_poll(backlight_watcher_t *watcher, backlight_dev_t *dev) {
    char buf[EVENT_BUF_LEN];
    int changed = 0;

    ssize_t len = read(watcher->inotify_fd, buf, sizeof(buf));

    if (len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        fprintf(stderr, "wayoled: inotify read failed: %s\n", strerror(errno));
        return -1;
    }

    for (char *ptr = buf; ptr < buf + len; ) {
        struct inotify_event *ev = (struct inotify_event *)ptr;

        if (ev->len > 0 && strcmp(ev->name, "brightness") == 0) {
            changed = 1;
        }

        ptr += sizeof(struct inotify_event) + ev->len;
    }

    if (changed) {
        long new_val = -1;
        FILE *f = fopen(dev->brightness_path, "r");
        if (f) {
            if (fscanf(f, "%ld", &new_val) != 1)
                new_val = -1;
            fclose(f);
        }

        if (new_val >= 0 && new_val != dev->current_brightness) {
            dev->target_brightness = new_val;
            return 1;
        }
    }

    return 0;
}

void watcher_close(backlight_watcher_t *watcher) {
    if (watcher->watch_fd >= 0)
        inotify_rm_watch(watcher->inotify_fd, watcher->watch_fd);
    if (watcher->inotify_fd >= 0)
        close(watcher->inotify_fd);
}
