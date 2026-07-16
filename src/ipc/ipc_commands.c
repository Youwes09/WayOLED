#include "ipc_commands.h"
#include "../core/profile.h"
#include "../core/config.h"
#include "../colortemp/colortemp.h"
#include "../wayland/refresh_cycle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

static long percent_to_raw(const backlight_dev_t *dev, long percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return (percent * dev->max_brightness) / 100;
}

static long raw_to_percent(const backlight_dev_t *dev, long raw) {
    if (dev->max_brightness <= 0)
        return 0;
    return (raw * 100) / dev->max_brightness;
}

static void cmd_status(wayoled_state_t *st, const char *args, char *resp, size_t max) {
    (void)args;
    char temp_field[16];
    if (st->colortemp_enabled)
        snprintf(temp_field, sizeof(temp_field), "%d", st->colortemp_kelvin);
    else
        snprintf(temp_field, sizeof(temp_field), "off");

    snprintf(resp, max,
        "dimmed=%d manual=%d paused=%d idle=%d static_count=%d brightness=%ld%% profile=%s pinned=%d refresh=%d colortemp=%s\n",
        st->dimmed, st->manual_override, st->paused, st->idle.is_idle,
        st->static_count, raw_to_percent(&st->backlight, st->backlight.current_brightness),
        st->profile[0] ? st->profile : "default", st->profile_pinned, st->refresh_in_progress, temp_field);
}

static void cmd_dim(wayoled_state_t *st, const char *args, char *resp, size_t max) {
    (void)args;
    if (!st->dimmer.available) {
        snprintf(resp, max, "err no gamma control\n");
        return;
    }
    dimmer_transition(&st->dimmer, st->display, 1.0, st->dim_factor, 20, 15000);
    st->dimmed = 1;
    st->manual_override = 1;
    snprintf(resp, max, "ok\n");
}

static void cmd_restore(wayoled_state_t *st, const char *args, char *resp, size_t max) {
    (void)args;
    if (!st->dimmer.available) {
        snprintf(resp, max, "err no gamma control\n");
        return;
    }
    dimmer_transition(&st->dimmer, st->display, st->dim_factor, 1.0, 20, 15000);
    st->dimmed = 0;
    st->manual_override = 0;
    st->static_count = 0;
    snprintf(resp, max, "ok\n");
}

static void cmd_pause(wayoled_state_t *st, const char *args, char *resp, size_t max) {
    (void)args;
    st->paused = 1;
    snprintf(resp, max, "ok\n");
}

static void cmd_resume(wayoled_state_t *st, const char *args, char *resp, size_t max) {
    (void)args;
    st->paused = 0;
    snprintf(resp, max, "ok\n");
}

static void cmd_brightness(wayoled_state_t *st, const char *args, char *resp, size_t max) {
    if (!st->backlight_available) {
        snprintf(resp, max, "err no backlight device available\n");
        return;
    }

    char sub[32] = {0};
    long value = 0;
    int n = sscanf(args, "%31s %ld", sub, &value);

    if (n < 1) {
        snprintf(resp, max, "err usage: brightness get|set <pct>|step <+-pct>\n");
        return;
    }

    if (strcmp(sub, "get") == 0) {
        snprintf(resp, max, "current=%ld%% raw=%ld/%ld\n",
            raw_to_percent(&st->backlight, st->backlight.current_brightness),
            st->backlight.current_brightness, st->backlight.max_brightness);
        return;
    }

    if (strcmp(sub, "set") == 0 && n == 2) {
        if (value < st->min_safe_brightness)
            value = st->min_safe_brightness;
        st->backlight.target_brightness = percent_to_raw(&st->backlight, value);
        snprintf(resp, max, "ok target=%ld%%\n", value);
        return;
    }

    if (strcmp(sub, "step") == 0 && n == 2) {
        long cur_pct = raw_to_percent(&st->backlight, st->backlight.target_brightness);
        long next_pct = cur_pct + value;
        if (next_pct < st->min_safe_brightness)
            next_pct = st->min_safe_brightness;
        if (next_pct > 100)
            next_pct = 100;
        st->backlight.target_brightness = percent_to_raw(&st->backlight, next_pct);
        snprintf(resp, max, "ok target=%ld%%\n", next_pct);
        return;
    }

    snprintf(resp, max, "err usage: brightness get|set <pct>|step <+-pct>\n");
}

static void cmd_refresh(wayoled_state_t *st, const char *args, char *resp, size_t max) {
    if (strcmp(args, "stop") == 0) {
        if (!st->refresh_in_progress) {
            snprintf(resp, max, "err not running\n");
            return;
        }
        if (kill(st->refresh_pid, SIGTERM) != 0) {
            snprintf(resp, max, "err failed to signal refresh process\n");
            return;
        }
        snprintf(resp, max, "ok stopping\n");
        return;
    }

    if (args[0] != '\0') {
        snprintf(resp, max, "err usage: refresh|refresh stop\n");
        return;
    }

    if (st->refresh_in_progress) {
        snprintf(resp, max, "err already running\n");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        snprintf(resp, max, "err fork failed\n");
        return;
    }

    if (pid == 0) {
        int rc = refresh_cycle_run();
        _exit(rc == 0 ? 0 : (rc == 1 ? 2 : 1));
    }

    st->refresh_pid = pid;
    st->refresh_in_progress = 1;
    snprintf(resp, max, "started\n");
}

static void cmd_profile(wayoled_state_t *st, const char *args, char *resp, size_t max) {
    if (args[0] == '\0') {
        snprintf(resp, max, "profile=%s pinned=%d\n", st->profile[0] ? st->profile : "default", st->profile_pinned);
        return;
    }

    if (profile_apply(st, args) != 0) {
        snprintf(resp, max, "err profile '%s' not found\n", args);
        return;
    }

    st->profile_pinned = 1;
    snprintf(resp, max, "ok profile=%s pinned=1\n", st->profile);
}

static void cmd_auto(wayoled_state_t *st, const char *args, char *resp, size_t max) {
    (void)args;
    st->profile_pinned = 0;
    snprintf(resp, max, "ok pinned=0\n");
}

static void cmd_profiles(wayoled_state_t *st, const char *args, char *resp, size_t max) {
    (void)st;
    (void)args;

    char names[CONFIG_LIST_MAX][CONFIG_PROFILE_NAME_MAX];
    int count = config_list_profiles(names);

    size_t off = 0;
    for (int i = 0; i < count && off < max; i++) {
        int n = snprintf(resp + off, max - off, "%s\n", names[i]);
        if (n < 0 || (size_t)n >= max - off)
            break;
        off += (size_t)n;
    }

    if (count == 0)
        snprintf(resp, max, "err no profiles found\n");
}

static const char *const HELP_TEXT =
    "status                    show daemon state\n"
    "dim                       force gamma dimming on\n"
    "restore                   force gamma dimming off\n"
    "pause                     suspend automatic static/idle dimming\n"
    "resume                    resume automatic static/idle dimming\n"
    "brightness get            report current/target backlight percentage\n"
    "brightness set <0-100>    set target backlight percentage\n"
    "brightness step <+-N>     adjust target backlight by N percent\n"
    "refresh                   run the pixel-refresh sweep\n"
    "refresh stop              cancel an in-progress pixel-refresh sweep\n"
    "profile [name]            show or switch+pin the active profile\n"
    "profiles                  list available profile names\n"
    "auto                      unpin, hand control back to the scheduler\n"
    "colortemp get|on|off      show or toggle time-of-day color warmth\n"
    "help                      show this text\n";

static void cmd_help(wayoled_state_t *st, const char *args, char *resp, size_t max) {
    (void)st;
    (void)args;
    snprintf(resp, max, "%s", HELP_TEXT);
}

static void cmd_colortemp(wayoled_state_t *st, const char *args, char *resp, size_t max) {
    if (!st->dimmer.available) {
        snprintf(resp, max, "err no gamma control\n");
        return;
    }

    if (args[0] == '\0' || strcmp(args, "get") == 0) {
        if (st->colortemp_enabled)
            snprintf(resp, max, "enabled=1 kelvin=%d day=%d night=%d\n",
                st->colortemp_kelvin, st->day_temp, st->night_temp);
        else
            snprintf(resp, max, "enabled=0\n");
        return;
    }

    if (strcmp(args, "on") == 0) {
        st->colortemp_enabled = 1;
        st->colortemp_kelvin = 0;
        colortemp_tick(st);
        snprintf(resp, max, "ok enabled=1\n");
        return;
    }

    if (strcmp(args, "off") == 0) {
        st->colortemp_enabled = 0;
        dimmer_set_colortemp(&st->dimmer, 1.0, 1.0, 1.0);
        st->colortemp_kelvin = 0;
        snprintf(resp, max, "ok enabled=0\n");
        return;
    }

    snprintf(resp, max, "err usage: colortemp get|on|off\n");
}

typedef struct {
    const char *name;
    void (*fn)(wayoled_state_t *, const char *, char *, size_t);
} ipc_cmd_entry_t;

static const ipc_cmd_entry_t commands[] = {
    { "status",     cmd_status },
    { "dim",        cmd_dim },
    { "restore",    cmd_restore },
    { "pause",      cmd_pause },
    { "resume",     cmd_resume },
    { "brightness", cmd_brightness },
    { "refresh",    cmd_refresh },
    { "profile",    cmd_profile },
    { "profiles",   cmd_profiles },
    { "auto",       cmd_auto },
    { "colortemp",  cmd_colortemp },
    { "help",       cmd_help },
};

void ipc_dispatch(wayoled_state_t *st, const char *cmd) {
    char name[32] = {0};
    const char *args = "";

    const char *sep = strchr(cmd, ' ');
    if (sep) {
        size_t len = (size_t)(sep - cmd);
        if (len >= sizeof(name))
            len = sizeof(name) - 1;
        memcpy(name, cmd, len);
        args = sep + 1;
    } else {
        strncpy(name, cmd, sizeof(name) - 1);
    }

    char resp[IPC_RESP_MAX] = {0};

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (strcmp(name, commands[i].name) == 0) {
            commands[i].fn(st, args, resp, sizeof(resp));
            ipc_server_respond(&st->ipc, resp);
            return;
        }
    }

    snprintf(resp, sizeof(resp), "err unknown command\n");
    ipc_server_respond(&st->ipc, resp);
}