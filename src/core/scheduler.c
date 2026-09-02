#include "scheduler.h"

#include <stdio.h>
#include <string.h>

int scheduler_load(scheduler_t *sch) {
    memset(sch, 0, sizeof(*sch));

    FILE *f = fopen(SCHEDULER_FILE, "r");
    if (!f)
        return -1;

    char line[128];
    while (sch->count < SCHEDULER_MAX_ENTRIES && fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;

        int hour, minute;
        char name[CONFIG_PROFILE_NAME_MAX];
        char monitor[WAYOLED_MONITOR_NAME_MAX] = {0};
        int n = sscanf(line, "%d:%d %31s %31s", &hour, &minute, name, monitor);
        if (n < 3)
            continue;
        if (hour < 0 || hour > 23 || minute < 0 || minute > 59)
            continue;

        schedule_entry_t *e = &sch->entries[sch->count++];
        e->hour = hour;
        e->minute = minute;
        snprintf(e->profile, sizeof(e->profile), "%s", name);
        snprintf(e->monitor, sizeof(e->monitor), "%s", monitor);
    }

    fclose(f);

    for (int i = 1; i < sch->count; i++) {
        schedule_entry_t key = sch->entries[i];
        int key_min = key.hour * 60 + key.minute;
        int j = i - 1;

        while (j >= 0 && (sch->entries[j].hour * 60 + sch->entries[j].minute) > key_min) {
            sch->entries[j + 1] = sch->entries[j];
            j--;
        }
        sch->entries[j + 1] = key;
    }

    return sch->count > 0 ? 0 : -1;
}

const char *scheduler_profile_for_time(scheduler_t *sch, int hour, int minute, const char *monitor) {
    const schedule_entry_t *match[SCHEDULER_MAX_ENTRIES];
    int count = 0;

    for (int i = 0; i < sch->count; i++) {
        if (sch->entries[i].monitor[0] && strcmp(sch->entries[i].monitor, monitor) != 0)
            continue;
        match[count++] = &sch->entries[i];
    }

    if (count == 0)
        return NULL;

    int now = hour * 60 + minute;
    const char *result = match[count - 1]->profile;

    for (int i = 0; i < count; i++) {
        int t = match[i]->hour * 60 + match[i]->minute;
        if (t <= now)
            result = match[i]->profile;
        else
            break;
    }

    return result;
}
