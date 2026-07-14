#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void parse_profile_file(FILE *f, wayoled_profile_t *out) {
    char line[128];

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;

        char key[64], value[64];
        if (sscanf(line, "%63[^=]=%63s", key, value) != 2)
            continue;

        if (strcmp(key, "dim_factor") == 0)
            out->dim_factor = atof(value);
        else if (strcmp(key, "static_threshold_polls") == 0)
            out->static_threshold_polls = atoi(value);
        else if (strcmp(key, "min_safe_brightness") == 0)
            out->min_safe_brightness = atol(value);
        else if (strcmp(key, "risk_monitor_enabled") == 0)
            out->risk_monitor_enabled = atoi(value);
    }
}

void config_default_profile(wayoled_profile_t *out) {
    memset(out, 0, sizeof(*out));
    strncpy(out->name, "default", sizeof(out->name) - 1);
    out->dim_factor = 0.7;
    out->static_threshold_polls = 20;
    out->min_safe_brightness = 2;
    out->risk_monitor_enabled = 1;
}

int config_load_profile(const char *name, wayoled_profile_t *out) {
    config_default_profile(out);
    strncpy(out->name, name, sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = '\0';

    char path[256];
    const char *home = getenv("HOME");

    if (home) {
        snprintf(path, sizeof(path), "%s%s/%s.conf", home, CONFIG_USER_DIR_SUFFIX, name);
        FILE *f = fopen(path, "r");
        if (f) {
            parse_profile_file(f, out);
            fclose(f);
            return 0;
        }
    }

    snprintf(path, sizeof(path), "%s/%s.conf", CONFIG_DIR, name);
    FILE *f = fopen(path, "r");
    if (f) {
        parse_profile_file(f, out);
        fclose(f);
        return 0;
    }

    if (strcmp(name, "default") == 0)
        return 0;

    return -1;
}
