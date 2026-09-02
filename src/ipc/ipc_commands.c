#include "ipc_commands.h"
#include "../core/profile.h"
#include "../core/config.h"
#include "../core/monitor.h"
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

static void extract_monitor(const char *args, char *rest, size_t rest_max,
                             char *monitor, size_t monitor_max) {
    monitor[0] = '\0';
    rest[0] = '\0';

    char buf[IPC_CMD_MAX];
    strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    size_t rest_off = 0;
    char *save = NULL;
    char *tok = strtok_r(buf, " ", &save);
    while (tok) {
        if (strcmp(tok, "--monitor") == 0) {
            tok = strtok_r(NULL, " ", &save);
            if (tok) {
                strncpy(monitor, tok, monitor_max - 1);
                monitor[monitor_max - 1] = '\0';
            }
        } else {
            int n = snprintf(rest + rest_off, rest_max - rest_off, "%s%s",
                              rest_off ? " " : "", tok);
            if (n > 0 && (size_t)n < rest_max - rest_off)
                rest_off += (size_t)n;
        }
        tok = strtok_r(NULL, " ", &save);
    }
}

static void cmd_status(wayoled_state_t *st, const char *args, const char *monitor, char *resp, size_t max) {
    (void)args;
    wayoled_monitor_t *targets[WAYOLED_MAX_MONITORS];
    char err[128];
    int n = monitor_resolve(st, monitor, NULL, 0, 0, targets, err, sizeof(err));
    if (n < 0) { snprintf(resp, max, "%s", err); return; }

    size_t off = 0;
    int w = snprintf(resp, max, "idle=%d paused=%d brightness=%ld%%\n",
        st->idle.is_idle, st->paused,
        raw_to_percent(&st->backlight, st->backlight.current_brightness));
    if (w > 0 && (size_t)w < max) off = (size_t)w;

    for (int i = 0; i < n && off < max; i++) {
        wayoled_monitor_t *m = targets[i];
        char temp_field[16];
        if (m->colortemp_enabled)
            snprintf(temp_field, sizeof(temp_field), "%d", m->colortemp_kelvin);
        else
            snprintf(temp_field, sizeof(temp_field), "off");

        w = snprintf(resp + off, max - off,
            "%s: dimmed=%d manual=%d static_count=%d profile=%s pinned=%d refresh=%d colortemp=%s gamma=%.2f:%.2f:%.2f\n",
            m->name, m->dimmed, m->manual_override, m->static_count,
            m->profile[0] ? m->profile : "default", m->profile_pinned, m->refresh_in_progress, temp_field,
            m->gamma_r, m->gamma_g, m->gamma_b);
        if (w < 0 || (size_t)w >= max - off)
            break;
        off += (size_t)w;
    }
}

static void cmd_dim(wayoled_state_t *st, const char *args, const char *monitor, char *resp, size_t max) {
    (void)args;
    wayoled_monitor_t *targets[WAYOLED_MAX_MONITORS];
    char err[128];
    int n = monitor_resolve(st, monitor, NULL, 0, 0, targets, err, sizeof(err));
    if (n < 0) { snprintf(resp, max, "%s", err); return; }

    int ok = 0;
    for (int i = 0; i < n; i++) {
        wayoled_monitor_t *m = targets[i];
        if (!m->dimmer.available)
            continue;
        dimmer_fade_start(&m->dimmer, m->dim_factor, DIMMER_FADE_MS);
        m->dimmed = 1;
        m->manual_override = 1;
        ok++;
    }

    snprintf(resp, max, ok ? "ok\n" : "err no gamma control\n");
}

static void cmd_restore(wayoled_state_t *st, const char *args, const char *monitor, char *resp, size_t max) {
    (void)args;
    wayoled_monitor_t *targets[WAYOLED_MAX_MONITORS];
    char err[128];
    int n = monitor_resolve(st, monitor, NULL, 0, 0, targets, err, sizeof(err));
    if (n < 0) { snprintf(resp, max, "%s", err); return; }

    int ok = 0;
    for (int i = 0; i < n; i++) {
        wayoled_monitor_t *m = targets[i];
        if (!m->dimmer.available)
            continue;
        dimmer_fade_start(&m->dimmer, 1.0, DIMMER_FADE_MS);
        m->dimmed = 0;
        m->manual_override = 0;
        m->static_count = 0;
        ok++;
    }

    snprintf(resp, max, ok ? "ok\n" : "err no gamma control\n");
}

static void cmd_pause(wayoled_state_t *st, const char *args, const char *monitor, char *resp, size_t max) {
    (void)args; (void)monitor;
    st->paused = 1;
    snprintf(resp, max, "ok\n");
}

static void cmd_resume(wayoled_state_t *st, const char *args, const char *monitor, char *resp, size_t max) {
    (void)args; (void)monitor;
    st->paused = 0;
    snprintf(resp, max, "ok\n");
}

static void cmd_brightness(wayoled_state_t *st, const char *args, const char *monitor, char *resp, size_t max) {
    if (!st->backlight_available) {
        snprintf(resp, max, "err no backlight device available\n");
        return;
    }

    long floor = 0;
    if (monitor && monitor[0]) {
        wayoled_monitor_t *m = monitor_find(st, monitor);
        if (!m) { snprintf(resp, max, "err unknown monitor '%s'\n", monitor); return; }
        floor = m->min_safe_brightness;
    } else {
        for (int i = 0; i < st->monitor_count; i++)
            if (st->monitors[i].min_safe_brightness > floor)
                floor = st->monitors[i].min_safe_brightness;
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
        if (value < floor)
            value = floor;
        st->backlight.target_brightness = percent_to_raw(&st->backlight, value);
        snprintf(resp, max, "ok target=%ld%%\n", value);
        return;
    }

    if (strcmp(sub, "step") == 0 && n == 2) {
        long cur_pct = raw_to_percent(&st->backlight, st->backlight.target_brightness);
        long next_pct = cur_pct + value;
        if (next_pct < floor)
            next_pct = floor;
        if (next_pct > 100)
            next_pct = 100;
        st->backlight.target_brightness = percent_to_raw(&st->backlight, next_pct);
        snprintf(resp, max, "ok target=%ld%%\n", next_pct);
        return;
    }

    snprintf(resp, max, "err usage: brightness get|set <pct>|step <+-pct>\n");
}

static void cmd_refresh(wayoled_state_t *st, const char *args, const char *monitor, char *resp, size_t max) {
    if (!st->cap_pixel_refresh) {
        snprintf(resp, max, "err pixel-refresh unavailable (needs wlr-layer-shell-v1)\n");
        return;
    }

    int stop = (strcmp(args, "stop") == 0);
    if (!stop && args[0] != '\0') {
        snprintf(resp, max, "err usage: refresh|refresh stop\n");
        return;
    }

    wayoled_monitor_t *targets[WAYOLED_MAX_MONITORS];
    char err[128];
    int n = monitor_resolve(st, monitor, NULL, 0, 0, targets, err, sizeof(err));
    if (n < 0) { snprintf(resp, max, "%s", err); return; }

    if (stop) {
        int stopped = 0;
        for (int i = 0; i < n; i++) {
            wayoled_monitor_t *m = targets[i];
            if (!m->refresh_in_progress)
                continue;
            if (kill(m->refresh_pid, SIGTERM) == 0)
                stopped++;
        }
        snprintf(resp, max, stopped ? "ok stopping\n" : "err not running\n");
        return;
    }

    int started = 0;
    for (int i = 0; i < n; i++) {
        wayoled_monitor_t *m = targets[i];
        if (m->refresh_in_progress)
            continue;

        pid_t pid = fork();
        if (pid < 0)
            continue;

        if (pid == 0) {
            int rc = refresh_cycle_run(m->name);
            _exit(rc == 0 ? 0 : (rc == 1 ? 2 : 1));
        }

        m->refresh_pid = pid;
        m->refresh_in_progress = 1;
        started++;
    }

    snprintf(resp, max, started ? "started\n" : "err already running\n");
}

static void cmd_profile(wayoled_state_t *st, const char *args, const char *monitor, char *resp, size_t max) {
    if (args[0] == '\0') {
        wayoled_monitor_t *targets[WAYOLED_MAX_MONITORS];
        char err[128];
        int n = monitor_resolve(st, monitor, NULL, 0, 1, targets, err, sizeof(err));
        if (n < 0) { snprintf(resp, max, "%s", err); return; }

        size_t off = 0;
        for (int i = 0; i < n && off < max; i++) {
            wayoled_monitor_t *m = targets[i];
            int w = snprintf(resp + off, max - off, "%s: profile=%s pinned=%d\n",
                m->name, m->profile[0] ? m->profile : "default", m->profile_pinned);
            if (w < 0 || (size_t)w >= max - off)
                break;
            off += (size_t)w;
        }
        return;
    }

    wayoled_profile_t prof;
    if (config_load_profile(args, &prof) != 0) {
        snprintf(resp, max, "err profile '%s' not found\n", args);
        return;
    }

    wayoled_monitor_t *targets[WAYOLED_MAX_MONITORS];
    char err[128];
    int n = monitor_resolve(st, monitor, prof.monitors, prof.monitor_count, 1, targets, err, sizeof(err));
    if (n < 0) { snprintf(resp, max, "%s", err); return; }

    for (int i = 0; i < n; i++) {
        profile_apply(targets[i], args);
        targets[i]->profile_pinned = 1;
    }

    snprintf(resp, max, "ok profile=%s pinned=1\n", args);
}

static void cmd_auto(wayoled_state_t *st, const char *args, const char *monitor, char *resp, size_t max) {
    (void)args;
    wayoled_monitor_t *targets[WAYOLED_MAX_MONITORS];
    char err[128];
    int n = monitor_resolve(st, monitor, NULL, 0, 0, targets, err, sizeof(err));
    if (n < 0) { snprintf(resp, max, "%s", err); return; }

    for (int i = 0; i < n; i++)
        targets[i]->profile_pinned = 0;

    snprintf(resp, max, "ok pinned=0\n");
}

static void cmd_profiles(wayoled_state_t *st, const char *args, const char *monitor, char *resp, size_t max) {
    (void)st; (void)args; (void)monitor;

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

static void cmd_colortemp(wayoled_state_t *st, const char *args, const char *monitor, char *resp, size_t max) {
    wayoled_monitor_t *targets[WAYOLED_MAX_MONITORS];
    char err[128];
    int n = monitor_resolve(st, monitor, NULL, 0, 0, targets, err, sizeof(err));
    if (n < 0) { snprintf(resp, max, "%s", err); return; }

    if (args[0] == '\0' || strcmp(args, "get") == 0) {
        size_t off = 0;
        for (int i = 0; i < n && off < max; i++) {
            wayoled_monitor_t *m = targets[i];
            int w;
            if (m->colortemp_enabled)
                w = snprintf(resp + off, max - off, "%s: enabled=1 kelvin=%d day=%d night=%d\n",
                    m->name, m->colortemp_kelvin, m->day_temp, m->night_temp);
            else
                w = snprintf(resp + off, max - off, "%s: enabled=0\n", m->name);
            if (w < 0 || (size_t)w >= max - off)
                break;
            off += (size_t)w;
        }
        return;
    }

    if (strcmp(args, "on") == 0) {
        int ok = 0;
        for (int i = 0; i < n; i++) {
            wayoled_monitor_t *m = targets[i];
            if (!m->dimmer.available)
                continue;
            m->colortemp_enabled = 1;
            m->colortemp_kelvin = 0;
            colortemp_tick(m);
            ok++;
        }
        snprintf(resp, max, ok ? "ok enabled=1\n" : "err no gamma control\n");
        return;
    }

    if (strcmp(args, "off") == 0) {
        int ok = 0;
        for (int i = 0; i < n; i++) {
            wayoled_monitor_t *m = targets[i];
            if (!m->dimmer.available)
                continue;
            m->colortemp_enabled = 0;
            dimmer_set_colortemp(&m->dimmer, 1.0, 1.0, 1.0);
            m->colortemp_kelvin = 0;
            ok++;
        }
        snprintf(resp, max, ok ? "ok enabled=0\n" : "err no gamma control\n");
        return;
    }

    snprintf(resp, max, "err usage: colortemp get|on|off\n");
}

static void cmd_monitors(wayoled_state_t *st, const char *args, const char *monitor, char *resp, size_t max) {
    (void)args; (void)monitor;
    size_t off = 0;
    for (int i = 0; i < st->monitor_count && off < max; i++) {
        int w = snprintf(resp + off, max - off, "%s\n", st->monitors[i].name);
        if (w < 0 || (size_t)w >= max - off)
            break;
        off += (size_t)w;
    }
    if (st->monitor_count == 0)
        snprintf(resp, max, "err no monitors\n");
}

static const char *const HELP_TEXT =
    "usage: oledctl <command> [args] [--monitor NAME]\n"
    "\n"
    "status                      show daemon state\n"
    "dim | restore               force gamma dimming on/off\n"
    "pause | resume              suspend/resume automatic dimming\n"
    "brightness get|set|step     report or adjust backlight percentage\n"
    "refresh [stop]              run or cancel the pixel-refresh sweep\n"
    "profile [name]              show or switch+pin the active profile\n"
    "profiles                    list available profile names\n"
    "auto                        unpin, hand control back to the scheduler\n"
    "colortemp get|on|off        show or toggle time-of-day color warmth\n"
    "gamma get|<v>|<r:g:b>|reset  show or set the per-channel gamma curve\n"
    "monitors                    list detected output names\n"
    "capabilities                report which features the compositor supports\n"
    "help                        show this text\n";

static void cmd_gamma(wayoled_state_t *st, const char *args, const char *monitor, char *resp, size_t max) {
    wayoled_monitor_t *targets[WAYOLED_MAX_MONITORS];
    char err[128];
    int n = monitor_resolve(st, monitor, NULL, 0, 0, targets, err, sizeof(err));
    if (n < 0) { snprintf(resp, max, "%s", err); return; }

    if (args[0] == '\0' || strcmp(args, "get") == 0) {
        size_t off = 0;
        for (int i = 0; i < n && off < max; i++) {
            wayoled_monitor_t *m = targets[i];
            int w = snprintf(resp + off, max - off, "%s: gamma=%.2f:%.2f:%.2f\n",
                m->name, m->gamma_r, m->gamma_g, m->gamma_b);
            if (w < 0 || (size_t)w >= max - off)
                break;
            off += (size_t)w;
        }
        return;
    }

    double r, g, b;
    if (strcmp(args, "reset") == 0) {
        r = g = b = 1.0;
    } else {
        int got = sscanf(args, "%lf:%lf:%lf", &r, &g, &b);
        if (got == 1)
            g = b = r;
        else if (got != 3) {
            snprintf(resp, max, "err usage: gamma get|<v>|<r:g:b>|reset\n");
            return;
        }
    }

    if (r < 0.1 || r > 10.0 || g < 0.1 || g > 10.0 || b < 0.1 || b > 10.0) {
        snprintf(resp, max, "err gamma out of range (0.1-10.0)\n");
        return;
    }

    int ok = 0;
    for (int i = 0; i < n; i++) {
        wayoled_monitor_t *m = targets[i];
        if (!m->dimmer.available)
            continue;
        m->gamma_r = r;
        m->gamma_g = g;
        m->gamma_b = b;
        dimmer_set_gamma(&m->dimmer, r, g, b);
        ok++;
    }

    if (!ok) {
        snprintf(resp, max, "err no gamma control\n");
        return;
    }
    snprintf(resp, max, "ok gamma=%.2f:%.2f:%.2f\n", r, g, b);
}

static void cmd_capabilities(wayoled_state_t *st, const char *args, const char *monitor, char *resp, size_t max) {
    (void)args; (void)monitor;
    snprintf(resp, max,
        "static_content=%d gamma=%d pixel_refresh=%d backlight=%d idle=1\n",
        st->cap_static_content, st->cap_gamma, st->cap_pixel_refresh, st->backlight_available);
}

static void cmd_help(wayoled_state_t *st, const char *args, const char *monitor, char *resp, size_t max) {
    (void)st; (void)args; (void)monitor;
    snprintf(resp, max, "%s", HELP_TEXT);
}

typedef struct {
    const char *name;
    void (*fn)(wayoled_state_t *, const char *, const char *, char *, size_t);
} ipc_cmd_entry_t;

static const ipc_cmd_entry_t commands[] = {
    { "status",       cmd_status },
    { "dim",          cmd_dim },
    { "restore",      cmd_restore },
    { "pause",        cmd_pause },
    { "resume",       cmd_resume },
    { "brightness",   cmd_brightness },
    { "refresh",      cmd_refresh },
    { "profile",      cmd_profile },
    { "profiles",     cmd_profiles },
    { "auto",         cmd_auto },
    { "colortemp",    cmd_colortemp },
    { "gamma",        cmd_gamma },
    { "monitors",     cmd_monitors },
    { "capabilities", cmd_capabilities },
    { "help",         cmd_help },
};

void ipc_dispatch(wayoled_state_t *st, const char *cmd) {
    char name[32] = {0};
    const char *raw_args = "";

    const char *sep = strchr(cmd, ' ');
    if (sep) {
        size_t len = (size_t)(sep - cmd);
        if (len >= sizeof(name))
            len = sizeof(name) - 1;
        memcpy(name, cmd, len);
        raw_args = sep + 1;
    } else {
        strncpy(name, cmd, sizeof(name) - 1);
    }

    char args[IPC_CMD_MAX] = {0};
    char monitor[WAYOLED_MONITOR_NAME_MAX] = {0};
    extract_monitor(raw_args, args, sizeof(args), monitor, sizeof(monitor));

    char resp[IPC_RESP_MAX] = {0};

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (strcmp(name, commands[i].name) == 0) {
            commands[i].fn(st, args, monitor, resp, sizeof(resp));
            ipc_server_respond(&st->ipc, resp);
            return;
        }
    }

    snprintf(resp, sizeof(resp), "err unknown command\n");
    ipc_server_respond(&st->ipc, resp);
}
