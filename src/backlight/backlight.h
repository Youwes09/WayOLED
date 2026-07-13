#ifndef WAYOLED_BACKLIGHT_H
#define WAYOLED_BACKLIGHT_H

#include <stddef.h>

#define BACKLIGHT_SYSFS_DIR "/sys/class/backlight"
#define BACKLIGHT_PATH_MAX 256

typedef struct {
    char device_path[BACKLIGHT_PATH_MAX];   // Ex: /sys/class/backlight/amdgpu_bl0
    char brightness_path[BACKLIGHT_PATH_MAX];
    char max_brightness_path[BACKLIGHT_PATH_MAX];

    long max_brightness;
    long current_brightness;
    long target_brightness;

    int fd;
} backlight_dev_t;


int backlight_detect(backlight_dev_t *dev);

int backlight_read(backlight_dev_t *dev);

int backlight_write(backlight_dev_t *dev, long value);

int backlight_tick(backlight_dev_t *dev, double step_fraction);

void backlight_close(backlight_dev_t *dev);

#endif
