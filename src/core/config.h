#ifndef WAYOLED_CONFIG_H
#define WAYOLED_CONFIG_H

#define CONFIG_PROFILE_NAME_MAX 32
#define CONFIG_DIR "/etc/wayoled/profiles"
#define CONFIG_USER_DIR_SUFFIX "/.config/wayoled/profiles"

typedef struct {
    char name[CONFIG_PROFILE_NAME_MAX];
    double dim_factor;
    int static_threshold_polls;
    long min_safe_brightness;
    int risk_monitor_enabled;
} wayoled_profile_t;

#define CONFIG_LIST_MAX 64

int config_load_profile(const char *name, wayoled_profile_t *out);
void config_default_profile(wayoled_profile_t *out);

// Enumerates profiles from ~/.config/wayoled/profiles/ and CONFIG_DIR,
// de-duplicated by name with user profiles taking priority. Writes up to
// CONFIG_LIST_MAX names into out_names (each up to CONFIG_PROFILE_NAME_MAX)
// and returns the count.
int config_list_profiles(char out_names[][CONFIG_PROFILE_NAME_MAX]);

#endif
