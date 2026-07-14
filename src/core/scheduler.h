#ifndef WAYOLED_SCHEDULER_H
#define WAYOLED_SCHEDULER_H

#include "config.h"

#define SCHEDULER_MAX_ENTRIES 16
#define SCHEDULER_FILE "/etc/wayoled/schedule.conf"

typedef struct {
    int hour;
    int minute;
    char profile[CONFIG_PROFILE_NAME_MAX];
} schedule_entry_t;

typedef struct {
    schedule_entry_t entries[SCHEDULER_MAX_ENTRIES];
    int count;
} scheduler_t;

int scheduler_load(scheduler_t *sch);
const char *scheduler_profile_for_time(scheduler_t *sch, int hour, int minute);

#endif
