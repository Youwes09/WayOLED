#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define BACKLIGHT_SYSFS_DIR "/sys/class/backlight"
#define PATH_MAX_LOCAL 256

static int valid_device_name(const char *name) {
    if (name[0] == '\0' || name[0] == '.')
        return 0;
    for (const char *p = name; *p; p++) {
        if (*p == '/' || *p == '\\')
            return 0;
    }
    return 1;
}

static int valid_digits(const char *s) {
    if (s[0] == '\0')
        return 0;
    for (const char *p = s; *p; p++) {
        if (!isdigit((unsigned char)*p))
            return 0;
    }
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <device_name> <value>\n", argv[0]);
        return 1;
    }

    const char *name = argv[1];
    const char *value_str = argv[2];

    if (!valid_device_name(name)) {
        fprintf(stderr, "wayoled-brightness-helper: invalid device name\n");
        return 1;
    }

    if (!valid_digits(value_str)) {
        fprintf(stderr, "wayoled-brightness-helper: invalid value\n");
        return 1;
    }

    long value = atol(value_str);

    char max_path[PATH_MAX_LOCAL];
    snprintf(max_path, sizeof(max_path), "%s/%s/max_brightness", BACKLIGHT_SYSFS_DIR, name);

    FILE *mf = fopen(max_path, "r");
    if (!mf) {
        fprintf(stderr, "wayoled-brightness-helper: unknown device\n");
        return 1;
    }

    long max_val = -1;
    if (fscanf(mf, "%ld", &max_val) != 1 || max_val < 0) {
        fclose(mf);
        fprintf(stderr, "wayoled-brightness-helper: cannot read max_brightness\n");
        return 1;
    }
    fclose(mf);

    if (value < 0)
        value = 0;
    if (value > max_val)
        value = max_val;

    char brightness_path[PATH_MAX_LOCAL];
    snprintf(brightness_path, sizeof(brightness_path), "%s/%s/brightness", BACKLIGHT_SYSFS_DIR, name);

    FILE *bf = fopen(brightness_path, "w");
    if (!bf) {
        fprintf(stderr, "wayoled-brightness-helper: cannot open brightness file\n");
        return 1;
    }

    fprintf(bf, "%ld", value);
    fclose(bf);

    return 0;
}
