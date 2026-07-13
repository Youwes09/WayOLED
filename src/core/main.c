#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "../backlight/backlight.h"
#include "../backlight/inotify_watch.h"
#include "../wayland/refresh_cycle.h"

#define TICK_US 16667 // ~60Hz
#define STEP_FRACTION 0.15 // 15% of remaining distance per tick

static int run_backlight_daemon(void) {
    backlight_dev_t dev = {0};
    backlight_watcher_t watcher = {0};

    if (backlight_detect(&dev) != 0) {
        fprintf(stderr, "wayoled: failed to initialize backlight device\n");
        return 1;
    }

    if (watcher_init(&watcher, &dev) != 0) {
        fprintf(stderr, "wayoled: failed to initialize inotify watcher\n");
        backlight_close(&dev);
        return 1;
    }

    fprintf(stderr, "wayoled: watching %s (max=%ld, current=%ld)\n",
            dev.brightness_path, dev.max_brightness, dev.current_brightness);

    for (;;) {
        watcher_poll(&watcher, &dev);
        backlight_tick(&dev, STEP_FRACTION);
        usleep(TICK_US);
    }

    watcher_close(&watcher);
    backlight_close(&dev);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--refresh") == 0) {
        return refresh_cycle_run() == 0 ? 0 : 1;
    }

    return run_backlight_daemon();
}
