#ifndef WAYOLED_COLORTEMP_H
#define WAYOLED_COLORTEMP_H

#include "../core/state.h"

void colortemp_kelvin_to_rgb(int kelvin, double *r, double *g, double *b);
void colortemp_tick(wayoled_monitor_t *mon);

#endif
