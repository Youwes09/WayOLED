#ifndef WAYOLED_CONFIG_H
#define WAYOLED_CONFIG_H

#include "wayland_globals.h"
#include "../wayland/layer_shell_overlay.h"

#define CONFIG_PROFILE_NAME_MAX 32
#define CONFIG_DIR "/etc/wayoled/profiles"
#define CONFIG_USER_DIR_SUFFIX "/.config/wayoled/profiles"
#define CONFIG_MONITOR_LIST_MAX WAYOLED_MAX_MONITORS

#define DIM_MODE_GAMMA 0
#define DIM_MODE_MASK 1

typedef struct {
    char name[CONFIG_PROFILE_NAME_MAX];
    double dim_factor;
    int static_threshold_polls;
    long min_safe_brightness;
    int risk_monitor_enabled;
    int colortemp_enabled;
    int day_temp;
    int night_temp;
    double gamma_r;
    double gamma_g;
    double gamma_b;
    int dim_mode;
    double mask_density;
    wayoled_rect_t mask_area;
    int mask_shift_interval_s;
    char monitors[CONFIG_MONITOR_LIST_MAX][WAYOLED_MONITOR_NAME_MAX];
    int monitor_count;
} wayoled_profile_t;

#define CONFIG_LIST_MAX 64

int config_load_profile(const char *name, wayoled_profile_t *out);
void config_default_profile(wayoled_profile_t *out);
int config_list_profiles(char out_names[][CONFIG_PROFILE_NAME_MAX]);

#endif
