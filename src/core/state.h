#ifndef WAYOLED_STATE_H
#define WAYOLED_STATE_H

#include <wayland-client.h>
#include <sys/types.h>
#include <stdint.h>

#include "../backlight/backlight.h"
#include "../backlight/inotify_watch.h"
#include "../wayland/idle_notify.h"
#include "../wayland/screencopy.h"
#include "dimmer.h"
#include "scheduler.h"
#include "wayland_globals.h"
#include "../ipc/ipc_server.h"

#define WAYOLED_PROFILE_MAX 32

typedef struct {
    char name[WAYOLED_MONITOR_NAME_MAX];
    struct wl_output *output;

    dimmer_t dimmer;
    screencopy_t screencopy;
    int screencopy_available;

    int dimmed;
    int manual_override;
    int static_count;
    uint64_t *last_hashes;
    int last_hash_count;

    double dim_factor;
    double gamma_r;
    double gamma_g;
    double gamma_b;
    long min_safe_brightness;
    int static_threshold_polls;
    int risk_monitor_enabled;

    int colortemp_enabled;
    int day_temp;
    int night_temp;
    int colortemp_kelvin;

    int refresh_in_progress;
    pid_t refresh_pid;

    char profile[WAYOLED_PROFILE_MAX];
    int profile_pinned;
} wayoled_monitor_t;

typedef struct {
    struct wl_display *display;
    struct wl_seat *seat;
    struct wl_shm *shm;
    struct zwlr_screencopy_manager_v1 *screencopy_manager;
    struct zwlr_gamma_control_manager_v1 *gamma_manager;

    backlight_dev_t backlight;
    backlight_watcher_t bl_watcher;
    int backlight_available;
    idle_watch_t idle;
    ipc_server_t ipc;
    scheduler_t scheduler;

    int paused;

    wayoled_monitor_t monitors[WAYOLED_MAX_MONITORS];
    int monitor_count;
} wayoled_state_t;

#endif
