#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <sys/timerfd.h>
#include <sys/wait.h>

#include "state.h"
#include "wayland_globals.h"
#include "profile.h"
#include "../colortemp/colortemp.h"
#include "../ipc/ipc_commands.h"
#include "../wayland/refresh_cycle.h"

#define TICK_MS 50
#define STEP_FRACTION 0.15
#define IDLE_TIMEOUT_MS 60000
#define GRID_BLOCK_SIZE 32
#define MAX_DIFF_RATIO 0.02
#define STATIC_CHECK_INTERVAL_MS 30000
#define SCHEDULE_CHECK_INTERVAL_MS 60000
#define COLORTEMP_CHECK_INTERVAL_MS 60000
#define CONNECT_RETRY_INITIAL_MS 1000
#define CONNECT_RETRY_MAX_MS 30000
#define CONNECT_RETRY_LOG_EVERY 10

static volatile sig_atomic_t g_running = 1;

static void handle_signal(int signum) {
    (void)signum;
    g_running = 0;
}

static struct wl_display *connect_with_retry(void) {
    int attempt = 0;
    int delay_ms = CONNECT_RETRY_INITIAL_MS;

    while (g_running) {
        struct wl_display *display = wl_display_connect(NULL);
        if (display)
            return display;

        attempt++;
        if (attempt == 1 || attempt % CONNECT_RETRY_LOG_EVERY == 0) {
            fprintf(stderr, "wayoled: no Wayland display yet (attempt %d), retrying every %dms\n",
                attempt, delay_ms);
        }

        struct timespec ts = {
            .tv_sec = delay_ms / 1000,
            .tv_nsec = (delay_ms % 1000) * 1000000L,
        };
        nanosleep(&ts, NULL);

        if (delay_ms < CONNECT_RETRY_MAX_MS)
            delay_ms = delay_ms * 2 < CONNECT_RETRY_MAX_MS ? delay_ms * 2 : CONNECT_RETRY_MAX_MS;
    }

    return NULL;
}

static void arm_timer(int fd, int interval_ms) {
    struct itimerspec ts = {0};
    ts.it_value.tv_sec = interval_ms / 1000;
    ts.it_value.tv_nsec = (interval_ms % 1000) * 1000000L;
    ts.it_interval = ts.it_value;
    timerfd_settime(fd, 0, &ts, NULL);
}

static void drain_timer(int fd) {
    uint64_t expirations;
    if (read(fd, &expirations, sizeof(expirations)) < 0)
        return;
}

static void check_static_content(wayoled_state_t *st, wayoled_monitor_t *mon) {
    if (!mon->screencopy_available)
        return;

    if (screencopy_capture(&mon->screencopy, st->display) != 0)
        return;

    int count = 0;
    uint64_t *hashes = screencopy_grid_hashes(&mon->screencopy, GRID_BLOCK_SIZE, &count);
    if (!hashes)
        return;

    if (mon->last_hashes && count == mon->last_hash_count) {
        double diff_ratio = screencopy_grid_diff_ratio(mon->last_hashes, hashes, count);
        mon->static_count = (diff_ratio <= MAX_DIFF_RATIO) ? mon->static_count + 1 : 0;
    }

    free(mon->last_hashes);
    mon->last_hashes = hashes;
    mon->last_hash_count = count;

    if (!mon->manual_override && !st->paused && mon->risk_monitor_enabled) {
        int risk = (mon->static_count >= mon->static_threshold_polls) && st->idle.is_idle;
        if (risk && !mon->dimmed && mon->dimmer.available) {
            fprintf(stderr, "wayoled: %s static content + idle detected, dimming\n", mon->name);
            dimmer_fade_start(&mon->dimmer, mon->dim_factor, DIMMER_FADE_MS);
            mon->dimmed = 1;
        }
    }
}

static void fades_tick(wayoled_state_t *st) {
    int any = 0;
    for (int i = 0; i < st->monitor_count; i++) {
        if (st->monitors[i].dimmer.fading) {
            dimmer_fade_tick(&st->monitors[i].dimmer);
            any = 1;
        }
    }
    if (any)
        wl_display_flush(st->display);
}

static void reap_refresh(wayoled_state_t *st) {
    for (int i = 0; i < st->monitor_count; i++) {
        wayoled_monitor_t *mon = &st->monitors[i];
        if (!mon->refresh_in_progress)
            continue;

        int status = 0;
        pid_t r = waitpid(mon->refresh_pid, &status, WNOHANG);
        if (r != mon->refresh_pid)
            continue;

        mon->refresh_in_progress = 0;

        if (WIFEXITED(status) && WEXITSTATUS(status) == 2)
            fprintf(stderr, "wayoled: %s refresh cycle cancelled\n", mon->name);
        else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
            fprintf(stderr, "wayoled: %s refresh cycle finished\n", mon->name);
        else
            fprintf(stderr, "wayoled: %s refresh cycle failed\n", mon->name);
    }
}

static void check_schedule(wayoled_state_t *st) {
    if (st->scheduler.count == 0)
        return;

    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);

    for (int i = 0; i < st->monitor_count; i++) {
        wayoled_monitor_t *mon = &st->monitors[i];
        if (mon->profile_pinned)
            continue;

        const char *target = scheduler_profile_for_time(&st->scheduler, local.tm_hour, local.tm_min, mon->name);
        if (target && strcmp(target, mon->profile) != 0)
            profile_apply(mon, target);
    }
}

static void on_tick(wayoled_state_t *st, int *ms_since_static, int *ms_since_schedule,
                     int *ms_since_colortemp, int *was_idle) {
    if (st->backlight_available)
        backlight_tick(&st->backlight, STEP_FRACTION);

    reap_refresh(st);

    if (*was_idle && !st->idle.is_idle) {
        for (int i = 0; i < st->monitor_count; i++) {
            wayoled_monitor_t *mon = &st->monitors[i];
            if (!mon->dimmed || !mon->dimmer.available)
                continue;
            fprintf(stderr, "wayoled: activity detected, restoring %s\n", mon->name);
            dimmer_fade_start(&mon->dimmer, 1.0, DIMMER_FADE_MS);
            mon->dimmed = 0;
            mon->manual_override = 0;
            mon->static_count = 0;
        }
    }
    *was_idle = st->idle.is_idle;

    *ms_since_static += TICK_MS;
    if (*ms_since_static >= STATIC_CHECK_INTERVAL_MS) {
        *ms_since_static = 0;
        if (!st->paused)
            for (int i = 0; i < st->monitor_count; i++)
                check_static_content(st, &st->monitors[i]);
    }

    *ms_since_schedule += TICK_MS;
    if (*ms_since_schedule >= SCHEDULE_CHECK_INTERVAL_MS) {
        *ms_since_schedule = 0;
        check_schedule(st);
    }

    *ms_since_colortemp += TICK_MS;
    if (*ms_since_colortemp >= COLORTEMP_CHECK_INTERVAL_MS) {
        *ms_since_colortemp = 0;
        for (int i = 0; i < st->monitor_count; i++)
            colortemp_tick(&st->monitors[i]);
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--refresh") == 0)
        return refresh_cycle_run(argc > 2 ? argv[2] : NULL) == 0 ? 0 : 1;

    struct sigaction sa = { .sa_handler = handle_signal };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    wayoled_state_t st = {0};
    st.ipc.client_fd = -1;
    st.bl_watcher.inotify_fd = -1;

    scheduler_load(&st.scheduler);

    st.display = connect_with_retry();
    if (!st.display)
        return 0;

    wayland_globals_t g;
    if (wayland_globals_bind(st.display, &g) != 0) {
        fprintf(stderr, "wayoled: compositor missing wl_output\n");
        return 1;
    }
    st.seat = g.seat;
    st.shm = g.shm;
    st.screencopy_manager = g.screencopy_manager;
    st.gamma_manager = g.gamma_manager;

    if (!g.seat || !g.idle_notifier) {
        fprintf(stderr, "wayoled: compositor missing wl_seat or ext_idle_notifier_v1\n");
        return 1;
    }

    if (idle_watch_init(&st.idle, g.seat, g.idle_notifier, IDLE_TIMEOUT_MS) != 0)
        return 1;

    int have_capture = g.shm && g.screencopy_manager;
    st.cap_pixel_refresh = g.layer_shell != NULL;

    st.monitor_count = g.output_count;
    for (int i = 0; i < g.output_count; i++) {
        wayoled_monitor_t *mon = &st.monitors[i];
        strncpy(mon->name, g.outputs[i].name, sizeof(mon->name) - 1);
        mon->name[sizeof(mon->name) - 1] = '\0';
        mon->output = g.outputs[i].output;

        if (have_capture &&
            screencopy_init(&mon->screencopy, g.shm, g.screencopy_manager, mon->output) == 0) {
            mon->screencopy_available = 1;
            st.cap_static_content = 1;
        }

        if (g.gamma_manager && dimmer_init(&mon->dimmer, g.gamma_manager, mon->output) == 0) {
            dimmer_confirm(&mon->dimmer, st.display);
            if (mon->dimmer.available)
                st.cap_gamma = 1;
        }

        profile_apply(mon, "default");
    }

    if (backlight_detect(&st.backlight) == 0) {
        st.backlight_available = 1;
        if (watcher_init(&st.bl_watcher, &st.backlight) != 0) {
            fprintf(stderr, "wayoled: inotify watcher failed, brightness changes won't be tracked live\n");
            st.bl_watcher.inotify_fd = -1;
        }
    } else {
        fprintf(stderr, "wayoled: continuing without backlight control\n");
        st.backlight_available = 0;
    }

    if (ipc_server_init(&st.ipc) != 0) {
        fprintf(stderr, "wayoled: IPC server failed to start, continuing without it\n");
        st.ipc.listen_fd = -1;
    } else {
        fprintf(stderr, "wayoled: IPC listening on %s\n", IPC_SOCKET_PATH);
    }

    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    arm_timer(timer_fd, TICK_MS);

    fprintf(stderr, "wayoled: capabilities:\n");
    fprintf(stderr, "  static-content detection : %s\n",
        st.cap_static_content ? "on (wlr-screencopy-v1)"
                              : "off (wlr-screencopy-v1 unavailable)");
    fprintf(stderr, "  gamma dim / temp / curve : %s\n",
        st.cap_gamma ? "on (wlr-gamma-control-v1)"
                     : "off (wlr-gamma-control-v1 unavailable)");
    fprintf(stderr, "  pixel-refresh sweep      : %s\n",
        st.cap_pixel_refresh ? "on (wlr-layer-shell-v1)"
                             : "off (wlr-layer-shell-v1 unavailable)");
    fprintf(stderr, "  backlight control        : %s\n",
        st.backlight_available ? st.backlight.brightness_path
                               : "off (no /sys/class/backlight device)");
    fprintf(stderr, "  idle detection           : on (ext-idle-notify-v1)\n");
    fprintf(stderr, "wayoled: daemon started (monitors=%d)\n", st.monitor_count);

    int ms_since_static = 0;
    int ms_since_schedule = 0;
    int ms_since_colortemp = 0;
    int was_idle = 0;

    for (int i = 0; i < st.monitor_count; i++)
        colortemp_tick(&st.monitors[i]);

    while (g_running) {
        while (wl_display_prepare_read(st.display) != 0)
            wl_display_dispatch_pending(st.display);
        wl_display_flush(st.display);

        struct pollfd fds[4] = {
            { wl_display_get_fd(st.display), POLLIN, 0 },
            { st.bl_watcher.inotify_fd,       POLLIN, 0 },
            { st.ipc.listen_fd,               POLLIN, 0 },
            { timer_fd,                       POLLIN, 0 },
        };

        int n = poll(fds, 4, -1);
        if (n < 0) {
            wl_display_cancel_read(st.display);
            if (errno == EINTR)
                continue;
            break;
        }

        if (fds[0].revents & POLLIN)
            wl_display_read_events(st.display);
        else
            wl_display_cancel_read(st.display);

        if (wl_display_dispatch_pending(st.display) < 0) {
            fprintf(stderr, "wayoled: Wayland connection lost, exiting\n");
            break;
        }

        if (st.bl_watcher.inotify_fd >= 0 && (fds[1].revents & POLLIN))
            watcher_poll(&st.bl_watcher, &st.backlight);

        if (fds[3].revents & POLLIN) {
            drain_timer(timer_fd);
            on_tick(&st, &ms_since_static, &ms_since_schedule, &ms_since_colortemp, &was_idle);
        }

        if (st.ipc.listen_fd >= 0 && (fds[2].revents & POLLIN)) {
            char cmd[IPC_CMD_MAX];
            if (ipc_server_poll(&st.ipc, cmd, sizeof(cmd)) == 1)
                ipc_dispatch(&st, cmd);
        }

        fades_tick(&st);
    }

    fprintf(stderr, "wayoled: shutting down\n");

    for (int i = 0; i < st.monitor_count; i++) {
        wayoled_monitor_t *mon = &st.monitors[i];
        if (mon->refresh_in_progress) {
            kill(mon->refresh_pid, SIGTERM);
            waitpid(mon->refresh_pid, NULL, 0);
        }
        if (mon->dimmer.available)
            dimmer_reset(&mon->dimmer);
    }
    wl_display_flush(st.display);
    wl_display_roundtrip(st.display);

    for (int i = 0; i < st.monitor_count; i++) {
        wayoled_monitor_t *mon = &st.monitors[i];
        free(mon->last_hashes);
        dimmer_destroy(&mon->dimmer);
        if (mon->screencopy_available)
            screencopy_destroy(&mon->screencopy);
    }
    close(timer_fd);
    ipc_server_destroy(&st.ipc);
    idle_watch_destroy(&st.idle);
    if (st.backlight_available) {
        watcher_close(&st.bl_watcher);
        backlight_close(&st.backlight);
    }
    wl_display_disconnect(st.display);
    return 0;
}
