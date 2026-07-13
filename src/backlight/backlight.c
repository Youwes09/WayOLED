#include "backlight.h"

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

int backlight_detect(backlight_dev_t *dev) {
    DIR *d = opendir(BACKLIGHT_SYSFS_DIR);
    if (!d) {
        fprintf(stderr, "wayoled: cannot open %s: %s\n",
                BACKLIGHT_SYSFS_DIR, strerror(errno));
        return -1;
    }

    struct dirent *entry;
    int found = 0;

    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        snprintf(dev->device_path, BACKLIGHT_PATH_MAX, "%s/%s",
                  BACKLIGHT_SYSFS_DIR, entry->d_name);
        snprintf(dev->brightness_path, BACKLIGHT_PATH_MAX, "%s/brightness",
                  dev->device_path);
        snprintf(dev->max_brightness_path, BACKLIGHT_PATH_MAX,
                  "%s/max_brightness", dev->device_path);

        if (access(dev->brightness_path, R_OK | W_OK) == 0 &&
            access(dev->max_brightness_path, R_OK) == 0) {
            found = 1;
            break;
        }
    }

    closedir(d);

    if (!found) {
        fprintf(stderr, "wayoled: no usable backlight device found in %s\n",
                BACKLIGHT_SYSFS_DIR);
        return -1;
    }

    fprintf(stderr, "wayoled: using backlight device %s\n", dev->device_path);

    dev->fd = open(dev->brightness_path, O_RDWR | O_CLOEXEC);
    if (dev->fd < 0) {
        fprintf(stderr, "wayoled: failed to open %s: %s\n",
                dev->brightness_path, strerror(errno));
        return -1;
    }

    return backlight_read(dev);
}

static long read_long_from_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    long val = -1;
    if (fscanf(f, "%ld", &val) != 1)
        val = -1;

    fclose(f);
    return val;
}

int backlight_read(backlight_dev_t *dev) {
    dev->max_brightness = read_long_from_file(dev->max_brightness_path);
    long cur = read_long_from_file(dev->brightness_path);

    if (dev->max_brightness < 0 || cur < 0)
        return -1;

    dev->current_brightness = cur;
    dev->target_brightness = cur;
    return 0;
}

int backlight_write(backlight_dev_t *dev, long value) {
    if (value < 0)
        value = 0;
    if (value > dev->max_brightness)
        value = dev->max_brightness;

    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%ld", value);

    if (pwrite(dev->fd, buf, len, 0) != len) {
        fprintf(stderr, "wayoled: failed to write brightness: %s\n",
                strerror(errno));
        return -1;
    }

    dev->current_brightness = value;
    return 0;
}

int backlight_tick(backlight_dev_t *dev, double step_fraction) {
    long diff = dev->target_brightness - dev->current_brightness;

    if (diff == 0)
        return 0;

    long step = (long)ceil(fabs((double)diff) * step_fraction);
    if (step < 1)
        step = 1;

    long next = dev->current_brightness + (diff > 0 ? step : -step);

    if ((diff > 0 && next > dev->target_brightness) ||
        (diff < 0 && next < dev->target_brightness)) {
        next = dev->target_brightness;
    }

    backlight_write(dev, next);

    return (next != dev->target_brightness);
}

void backlight_close(backlight_dev_t *dev) {
    if (dev->fd >= 0)
        close(dev->fd);
}
