#include "colortemp.h"

#include <time.h>
#include <math.h>

#define EVENING_START_MIN (19 * 60)
#define EVENING_END_MIN   (21 * 60)
#define MORNING_START_MIN (6 * 60)
#define MORNING_END_MIN   (8 * 60)

static double clamp01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

void colortemp_kelvin_to_rgb(int kelvin, double *r, double *g, double *b) {
    double temp = (double)kelvin / 100.0;
    double red, green, blue;

    if (temp <= 66.0) {
        red = 255.0;
    } else {
        red = 329.698727446 * pow(temp - 60.0, -0.1332047592);
    }

    if (temp <= 66.0) {
        green = 99.4708025861 * log(temp) - 161.1195681661;
    } else {
        green = 288.1221695283 * pow(temp - 60.0, -0.0755148492);
    }

    if (temp >= 66.0) {
        blue = 255.0;
    } else if (temp <= 19.0) {
        blue = 0.0;
    } else {
        blue = 138.5177312231 * log(temp - 10.0) - 305.0447927307;
    }

    *r = clamp01(red / 255.0);
    *g = clamp01(green / 255.0);
    *b = clamp01(blue / 255.0);
}

static int target_kelvin(int hour, int minute, int day_temp, int night_temp) {
    int now = hour * 60 + minute;

    if (now >= EVENING_END_MIN || now < MORNING_START_MIN)
        return night_temp;
    if (now >= MORNING_END_MIN && now < EVENING_START_MIN)
        return day_temp;

    if (now >= EVENING_START_MIN && now < EVENING_END_MIN) {
        double progress = (double)(now - EVENING_START_MIN) / (EVENING_END_MIN - EVENING_START_MIN);
        return day_temp + (int)((night_temp - day_temp) * progress);
    }

    double progress = (double)(now - MORNING_START_MIN) / (MORNING_END_MIN - MORNING_START_MIN);
    return night_temp + (int)((day_temp - night_temp) * progress);
}

void colortemp_tick(wayoled_state_t *st) {
    if (!st->colortemp_enabled || !st->dimmer.available)
        return;

    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);

    int kelvin = target_kelvin(local.tm_hour, local.tm_min, st->day_temp, st->night_temp);
    if (kelvin == st->colortemp_kelvin)
        return;

    double r, g, b;
    colortemp_kelvin_to_rgb(kelvin, &r, &g, &b);
    dimmer_set_colortemp(&st->dimmer, r, g, b);
    st->colortemp_kelvin = kelvin;
}