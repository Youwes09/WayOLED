#include "profile.h"
#include "config.h"

#include <stdio.h>
#include <string.h>

int profile_apply(wayoled_state_t *st, const char *name) {
    wayoled_profile_t prof;

    if (config_load_profile(name, &prof) != 0) {
        fprintf(stderr, "wayoled: profile '%s' not found, keeping current settings\n", name);
        return -1;
    }

    st->dim_factor = prof.dim_factor;
    st->static_threshold_polls = prof.static_threshold_polls;
    st->min_safe_brightness = prof.min_safe_brightness;
    st->risk_monitor_enabled = prof.risk_monitor_enabled;
    st->colortemp_enabled = prof.colortemp_enabled;
    st->day_temp = prof.day_temp;
    st->night_temp = prof.night_temp;
    strncpy(st->profile, prof.name, sizeof(st->profile) - 1);
    st->profile[sizeof(st->profile) - 1] = '\0';

    if (!st->colortemp_enabled && st->dimmer.available) {
        dimmer_set_colortemp(&st->dimmer, 1.0, 1.0, 1.0);
        st->colortemp_kelvin = 0;
    }

    fprintf(stderr, "wayoled: profile switched to '%s'\n", st->profile);
    return 0;
}