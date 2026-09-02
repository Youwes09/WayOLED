#include "profile.h"
#include "config.h"

#include <stdio.h>
#include <string.h>

int profile_apply(wayoled_monitor_t *mon, const char *name) {
    wayoled_profile_t prof;

    if (config_load_profile(name, &prof) != 0) {
        fprintf(stderr, "wayoled: profile '%s' not found, keeping current settings\n", name);
        return -1;
    }

    mon->dim_factor = prof.dim_factor;
    mon->static_threshold_polls = prof.static_threshold_polls;
    mon->min_safe_brightness = prof.min_safe_brightness;
    mon->risk_monitor_enabled = prof.risk_monitor_enabled;
    mon->colortemp_enabled = prof.colortemp_enabled;
    mon->day_temp = prof.day_temp;
    mon->night_temp = prof.night_temp;
    mon->gamma_r = prof.gamma_r;
    mon->gamma_g = prof.gamma_g;
    mon->gamma_b = prof.gamma_b;
    strncpy(mon->profile, prof.name, sizeof(mon->profile) - 1);
    mon->profile[sizeof(mon->profile) - 1] = '\0';

    if (mon->dimmer.available)
        dimmer_set_gamma(&mon->dimmer, mon->gamma_r, mon->gamma_g, mon->gamma_b);

    if (!mon->colortemp_enabled && mon->dimmer.available) {
        dimmer_set_colortemp(&mon->dimmer, 1.0, 1.0, 1.0);
        mon->colortemp_kelvin = 0;
    }

    fprintf(stderr, "wayoled: profile switched to '%s' on %s\n", mon->profile, mon->name);
    return 0;
}
