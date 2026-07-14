#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <sys/timerfd.h>
#include <sys/wait.h>

#include "state.h"
#include "wayland_globals.h"
#include "profile.h"
#include "../ipc/ipc_commands.h"
#include "../wayland/refresh_cycle.h"

#define TICK_MS 50
#define STEP_FRACTION 0.15
#define IDLE_TIMEOUT_MS 60000
#define GRID_BLOCK_SIZE 32
#define MAX_DIFF_RATIO 0.02
#define STATIC_CHECK_INTERVAL_MS 30000
#define SCHEDULE_CHECK_INTERVAL_MS 60000

static void arm_timer(int fd, int interval_ms) {
    struct itimerspec ts = {0};
    ts.it_value.tv_sec = interval_ms / 1000;
    ts.it_value.tv_nsec = (interval_ms % 1000) * 1000000L;
    ts.it_interval = ts.it_value;
    timerfd_settime(fd, 0, &ts, NULL);
}

static void drain_timer(int fd) {
    uint64_t expirations;
    read(fd, &expirations, sizeof(expirations));
}

static void check_static_content(wayoled_state_t *st) {
    if (screencopy_capture(&st->screencopy, st->display) != 0)
        return;

    int count = 0;
    uint64_t *hashes = screencopy_grid_hashes(&st->screencopy, GRID_BLOCK_SIZE, &count);
    if (!hashes)
        return;

    if (st->last_hashes && count == st->last_hash_count) {
        double diff_ratio = screencopy_grid_diff_ratio(st->last_hashes, hashes, count);
        st->static_count = (diff_ratio <= MAX_DIFF_RATIO) ? st->static_count + 1 : 0;
    }

    free(st->last_hashes);
    st->last_hashes = hashes;
    st->last_hash_count = count;

    if (!st->manual_override && !st->paused) {
        int risk = (st->static_count >= st->static_threshold_polls) && st->idle.is_idle;
        if (risk && !st->dimmed && st->dimmer.available) {
            fprintf(stderr, "wayoled: static content + idle detected, dimming\n");
            dimmer_transition(&st->dimmer, st->display, 1.0, st->dim_factor, 20, 15000);
            st->dimmed = 1;
        }
    }
}

static void reap_refresh(wayoled_state_t *st) {
    if (!st->refresh_in_progress)
        return;

    int status = 0;
    pid_t r = waitpid(st->refresh_pid, &status, WNOHANG);
    if (r == st->refresh_pid) {
        st->refresh_in_progress = 0;
        fprintf(stderr, "wayoled: refresh cycle finished\n");
    }
}

static void check_schedule(wayoled_state_t *st) {
    if (st->profile_pinned || st->scheduler.count == 0)
        return;

    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);

    const char *target = scheduler_profile_for_time(&st->scheduler, local.tm_hour, local.tm_min);
    if (target && strcmp(target, st->profile) != 0)
        profile_apply(st, target);
}

static void on_tick(wayoled_state_t *st, int *ms_since_static, int *ms_since_schedule, int *was_idle) {
    if (st->backlight_available)
        backlight_tick(&st->backlight, STEP_FRACTION);

    reap_refresh(st);

    if (*was_idle && !st->idle.is_idle && st->dimmed && st->dimmer.available) {
        fprintf(stderr, "wayoled: activity detected, restoring immediately\n");
        dimmer_transition(&st->dimmer, st->display, st->dim_factor, 1.0, 20, 15000);
        st->dimmed = 0;
        st->manual_override = 0;
        st->static_count = 0;
    }
    *was_idle = st->idle.is_idle;

    *ms_since_static += TICK_MS;
    if (*ms_since_static >= STATIC_CHECK_INTERVAL_MS) {
        *ms_since_static = 0;
        if (!st->paused)
            check_static_content(st);
    }

    *ms_since_schedule += TICK_MS;
    if (*ms_since_schedule >= SCHEDULE_CHECK_INTERVAL_MS) {
        *ms_since_schedule = 0;
        check_schedule(st);
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--refresh") == 0)
        return refresh_cycle_run() == 0 ? 0 : 1;

    wayoled_state_t st = {0};
    st.ipc.client_fd = -1;
    st.bl_watcher.inotify_fd = -1;

    profile_apply(&st, "default");
    scheduler_load(&st.scheduler);

    st.display = wl_display_connect(NULL);
    if (!st.display) {
        fprintf(stderr, "wayoled: failed to connect to Wayland display\n");
        return 1;
    }

    wayland_globals_t g;
    wayland_globals_bind(st.display, &g);
    st.seat = g.seat;
    st.shm = g.shm;
    st.output = g.output;
    st.screencopy_manager = g.screencopy_manager;
    st.gamma_manager = g.gamma_manager;

    if (!g.seat || !g.idle_notifier) {
        fprintf(stderr, "wayoled: compositor missing wl_seat or ext_idle_notifier_v1\n");
        return 1;
    }
    if (!g.shm || !g.screencopy_manager || !g.output) {
        fprintf(stderr, "wayoled: compositor missing shm/screencopy/output\n");
        return 1;
    }

    if (idle_watch_init(&st.idle, g.seat, g.idle_notifier, IDLE_TIMEOUT_MS) != 0)
        return 1;

    if (screencopy_init(&st.screencopy, g.shm, g.screencopy_manager, g.output) != 0)
        return 1;

    if (dimmer_init(&st.dimmer, g.gamma_manager, g.output) == 0)
        dimmer_confirm(&st.dimmer, st.display);

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

    fprintf(stderr, "wayoled: daemon started (backlight=%s profile=%s)\n",
        st.backlight_available ? st.backlight.brightness_path : "unavailable", st.profile);

    int ms_since_static = 0;
    int ms_since_schedule = 0;
    int was_idle = 0;

    for (;;) {
        wl_display_flush(st.display);

        struct pollfd fds[4] = {
            { wl_display_get_fd(st.display), POLLIN, 0 },
            { st.bl_watcher.inotify_fd,       POLLIN, 0 },
            { st.ipc.listen_fd,               POLLIN, 0 },
            { timer_fd,                       POLLIN, 0 },
        };

        int n = poll(fds, 4, -1);
        if (n < 0)
            continue;

        if (fds[0].revents & POLLIN)
            wl_display_dispatch(st.display);

        if (st.bl_watcher.inotify_fd >= 0 && (fds[1].revents & POLLIN))
            watcher_poll(&st.bl_watcher, &st.backlight);

        if (fds[3].revents & POLLIN) {
            drain_timer(timer_fd);
            on_tick(&st, &ms_since_static, &ms_since_schedule, &was_idle);
        }

        if (st.ipc.listen_fd >= 0 && (fds[2].revents & POLLIN)) {
            char cmd[IPC_CMD_MAX];
            if (ipc_server_poll(&st.ipc, cmd, sizeof(cmd)) == 1)
                ipc_dispatch(&st, cmd);
        }
    }

    free(st.last_hashes);
    dimmer_destroy(&st.dimmer);
    ipc_server_destroy(&st.ipc);
    screencopy_destroy(&st.screencopy);
    idle_watch_destroy(&st.idle);
    if (st.backlight_available) {
        watcher_close(&st.bl_watcher);
        backlight_close(&st.backlight);
    }
    wl_display_disconnect(st.display);
    return 0;
}
