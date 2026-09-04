#define _POSIX_C_SOURCE 200809L
#include "ipc_client.h"
#include "../src/ipc/ipc_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void print_usage(const char *prog) {
    fprintf(stderr, "usage: %s <command> [args]\n", prog);
    fprintf(stderr, "run '%s help' for the full command list\n", prog);
}

static int valid_profile_name(const char *name) {
    if (name[0] == '\0' || name[0] == '.')
        return 0;
    for (const char *c = name; *c; c++) {
        if (*c == '/')
            return 0;
    }
    return 1;
}

static void mkdir_parents(char *path) {
    for (char *p = path + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(path, 0755);
            *p = '/';
        }
    }
    mkdir(path, 0755);
}

static int write_profile_lines(const char *path, const char *monitor_line, const char *area_line) {
    char *lines[256];
    size_t count = 0;
    int have_monitor = 0, have_area = 0;

    FILE *in = fopen(path, "r");
    if (in) {
        char buf[256];
        while (count < sizeof(lines) / sizeof(lines[0]) - 2 && fgets(buf, sizeof(buf), in)) {
            buf[strcspn(buf, "\n")] = '\0';

            if (strncmp(buf, "monitor=", 8) == 0) {
                lines[count++] = strdup(monitor_line);
                have_monitor = 1;
            } else if (strncmp(buf, "mask_area=", 10) == 0) {
                lines[count++] = strdup(area_line);
                have_area = 1;
            } else {
                lines[count++] = strdup(buf);
            }
        }
        fclose(in);
    }

    if (!have_monitor)
        lines[count++] = strdup(monitor_line);
    if (!have_area)
        lines[count++] = strdup(area_line);

    FILE *out = fopen(path, "w");
    if (!out) {
        for (size_t i = 0; i < count; i++)
            free(lines[i]);
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        fprintf(out, "%s\n", lines[i]);
        free(lines[i]);
    }
    fclose(out);
    return 0;
}

static int cmd_mask_region(const char *write_profile) {
    if (write_profile && !valid_profile_name(write_profile)) {
        fprintf(stderr, "oledctl: invalid profile name '%s'\n", write_profile);
        return 1;
    }

    FILE *p = popen("slurp -f '%o %X:%Y:%W:%H' 2>/dev/null", "r");
    if (!p) {
        fprintf(stderr, "oledctl: failed to run slurp, is it installed?\n");
        return 1;
    }

    char line[128] = {0};
    char *got = fgets(line, sizeof(line), p);
    int rc = pclose(p);
    line[strcspn(line, "\n")] = '\0';

    if (!got || rc != 0 || line[0] == '\0') {
        fprintf(stderr, "oledctl: selection cancelled, or slurp is not installed\n");
        return 1;
    }

    char output[64] = {0};
    char rect[64] = {0};
    if (sscanf(line, "%63s %63s", output, rect) != 2) {
        fprintf(stderr, "oledctl: could not parse slurp output: %s\n", line);
        return 1;
    }

    char monitor_line[80], area_line[80];
    snprintf(monitor_line, sizeof(monitor_line), "monitor=%s", output);
    snprintf(area_line, sizeof(area_line), "mask_area=%s", rect);

    if (!write_profile) {
        printf("%s\n%s\n", monitor_line, area_line);
        return 0;
    }

    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "oledctl: HOME not set, cannot locate profile directory\n");
        return 1;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/.config/wayoled/profiles", home);
    mkdir_parents(path);
    snprintf(path, sizeof(path), "%s/.config/wayoled/profiles/%s.conf", home, write_profile);

    if (write_profile_lines(path, monitor_line, area_line) != 0) {
        fprintf(stderr, "oledctl: failed to write %s\n", path);
        return 1;
    }

    printf("wrote %s\n%s\n%s\n", path, monitor_line, area_line);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "mask-region") == 0) {
        if (argc == 2)
            return cmd_mask_region(NULL);
        if (argc == 4 && strcmp(argv[2], "--write") == 0)
            return cmd_mask_region(argv[3]);
        fprintf(stderr, "usage: %s mask-region [--write PROFILE]\n", argv[0]);
        return 1;
    }

    char cmd[IPC_CMD_MAX] = {0};
    size_t off = 0;

    for (int i = 1; i < argc; i++) {
        int n = snprintf(cmd + off, sizeof(cmd) - off, "%s%s", i > 1 ? " " : "", argv[i]);
        if (n < 0 || (size_t)n >= sizeof(cmd) - off)
            break;
        off += (size_t)n;
    }

    char resp[IPC_RESP_MAX] = {0};
    if (ipc_client_send(cmd, resp, sizeof(resp)) != 0)
        return 1;

    fputs(resp, stdout);
    return (strncmp(resp, "err", 3) == 0) ? 1 : 0;
}
