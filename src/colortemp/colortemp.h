#ifndef WAYOLED_COLORTEMP_H
#define WAYOLED_COLORTEMP_H

#include "../core/state.h"

// Converts a color temperature in Kelvin (1000-40000) to normalized
// per-channel multipliers in [0.0, 1.0], using the Tanner Helland
// approximation. 6500K is neutral (all 1.0), lower is warmer.
void colortemp_kelvin_to_rgb(int kelvin, double *r, double *g, double *b);

// Checks the current time of day, computes the target color temperature
// by interpolating between st->night_temp and st->day_temp, and applies
// it via the dimmer's combined gamma ramp. No-op if st->colortemp_enabled
// is false or the dimmer is unavailable.
void colortemp_tick(wayoled_state_t *st);

#endif