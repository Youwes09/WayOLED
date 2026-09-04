#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

static void parse_monitor_list(wayoled_profile_t *out, const char *value) {
    out->monitor_count = 0;
    if (strcmp(value, "all") == 0)
        return;

    char buf[256];
    strncpy(buf, value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tok = strtok(buf, ",");
    while (tok && out->monitor_count < CONFIG_MONITOR_LIST_MAX) {
        strncpy(out->monitors[out->monitor_count], tok, WAYOLED_MONITOR_NAME_MAX - 1);
        out->monitors[out->monitor_count][WAYOLED_MONITOR_NAME_MAX - 1] = '\0';
        out->monitor_count++;
        tok = strtok(NULL, ",");
    }
}

static void parse_gamma(wayoled_profile_t *out, const char *value) {
    double r, g, b;
    int n = sscanf(value, "%lf:%lf:%lf", &r, &g, &b);
    if (n == 1)
        g = b = r;
    else if (n != 3)
        return;
    if (r < 0.1 || r > 10.0 || g < 0.1 || g > 10.0 || b < 0.1 || b > 10.0)
        return;
    out->gamma_r = r;
    out->gamma_g = g;
    out->gamma_b = b;
}

static void parse_mask_area(wayoled_profile_t *out, const char *value) {
    if (strcmp(value, "full") == 0) {
        out->mask_area.w = 0;
        return;
    }

    int x, y, w, h;
    if (sscanf(value, "%d:%d:%d:%d", &x, &y, &w, &h) == 4 && w > 0 && h > 0) {
        out->mask_area.x = x;
        out->mask_area.y = y;
        out->mask_area.w = w;
        out->mask_area.h = h;
    }
}

static void parse_dim_mode(wayoled_profile_t *out, const char *value) {
    if (strcmp(value, "mask") == 0)
        out->dim_mode = DIM_MODE_MASK;
    else if (strcmp(value, "gamma") == 0)
        out->dim_mode = DIM_MODE_GAMMA;
}

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
        else if (strcmp(key, "colortemp_enabled") == 0)
            out->colortemp_enabled = atoi(value);
        else if (strcmp(key, "day_temp") == 0)
            out->day_temp = atoi(value);
        else if (strcmp(key, "night_temp") == 0)
            out->night_temp = atoi(value);
        else if (strcmp(key, "gamma") == 0)
            parse_gamma(out, value);
        else if (strcmp(key, "dim_mode") == 0)
            parse_dim_mode(out, value);
        else if (strcmp(key, "mask_density") == 0) {
            double d = atof(value);
            if (d < 0.0) d = 0.0;
            if (d > 1.0) d = 1.0;
            out->mask_density = d;
        } else if (strcmp(key, "mask_area") == 0) {
            parse_mask_area(out, value);
        } else if (strcmp(key, "mask_shift_interval_s") == 0) {
            out->mask_shift_interval_s = atoi(value);
        } else if (strcmp(key, "monitor") == 0)
            parse_monitor_list(out, value);
    }
}

void config_default_profile(wayoled_profile_t *out) {
    memset(out, 0, sizeof(*out));
    strncpy(out->name, "default", sizeof(out->name) - 1);
    out->dim_factor = 0.7;
    out->static_threshold_polls = 20;
    out->min_safe_brightness = 2;
    out->risk_monitor_enabled = 1;
    out->colortemp_enabled = 1;
    out->day_temp = 6500;
    out->night_temp = 3400;
    out->gamma_r = 1.0;
    out->gamma_g = 1.0;
    out->gamma_b = 1.0;
    out->dim_mode = DIM_MODE_GAMMA;
    out->mask_density = 0.5;
    out->mask_shift_interval_s = 3600;
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

static int list_has(char names[][CONFIG_PROFILE_NAME_MAX], int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (strcmp(names[i], name) == 0)
            return 1;
    }
    return 0;
}

static int scan_dir(const char *dir, char out_names[][CONFIG_PROFILE_NAME_MAX], int count) {
    DIR *d = opendir(dir);
    if (!d)
        return count;

    struct dirent *ent;
    while (count < CONFIG_LIST_MAX && (ent = readdir(d)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len <= 5 || strcmp(ent->d_name + len - 5, ".conf") != 0)
            continue;

        char name[CONFIG_PROFILE_NAME_MAX];
        size_t namelen = len - 5;
        if (namelen >= sizeof(name))
            namelen = sizeof(name) - 1;
        memcpy(name, ent->d_name, namelen);
        name[namelen] = '\0';

        if (!list_has(out_names, count, name)) {
            snprintf(out_names[count], CONFIG_PROFILE_NAME_MAX, "%s", name);
            count++;
        }
    }

    closedir(d);
    return count;
}

int config_list_profiles(char out_names[][CONFIG_PROFILE_NAME_MAX]) {
    int count = 0;
    char path[256];
    const char *home = getenv("HOME");

    if (home) {
        snprintf(path, sizeof(path), "%s%s", home, CONFIG_USER_DIR_SUFFIX);
        count = scan_dir(path, out_names, count);
    }

    count = scan_dir(CONFIG_DIR, out_names, count);

    if (!list_has(out_names, count, "default") && count < CONFIG_LIST_MAX) {
        strncpy(out_names[count], "default", CONFIG_PROFILE_NAME_MAX - 1);
        out_names[count][CONFIG_PROFILE_NAME_MAX - 1] = '\0';
        count++;
    }

    return count;
}
