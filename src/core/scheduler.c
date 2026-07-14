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
        if (sscanf(line, "%d:%d %31s", &hour, &minute, name) != 3)
            continue;
        if (hour < 0 || hour > 23 || minute < 0 || minute > 59)
            continue;

        schedule_entry_t *e = &sch->entries[sch->count++];
        e->hour = hour;
        e->minute = minute;
        strncpy(e->profile, name, sizeof(e->profile) - 1);
        e->profile[sizeof(e->profile) - 1] = '\0';
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

const char *scheduler_profile_for_time(scheduler_t *sch, int hour, int minute) {
    if (sch->count == 0)
        return NULL;

    int now = hour * 60 + minute;
    const char *result = sch->entries[sch->count - 1].profile;

    for (int i = 0; i < sch->count; i++) {
        int t = sch->entries[i].hour * 60 + sch->entries[i].minute;
        if (t <= now)
            result = sch->entries[i].profile;
        else
            break;
    }

    return result;
}
