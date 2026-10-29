/* SPDX-License-Identifier: GPL-3.0-only */
#include "app/startup.h"
#include "app/options.h"
#include "app/positions.h"
#include "app/render.h"
#include "app/screen_events.h"
#include "localization/language.h"
#include "platform/audio.h"
#include "platform/launch.h"
#include "platform/menu_button.h"
#include "platform/system_config.h"
#include "platform/timing.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef MAINUI_ONION
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#endif

#ifndef MAINUI_VERSION
#define MAINUI_VERSION "0-dev"
#endif

/* Temporary startup diagnostics. Use 64-bit milliseconds on the 32-bit device. */
static long long startup_ms(void)
{
    struct timespec t = {0};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

#ifdef MAINUI_ONION
/* RetroArch pans the framebuffer for double/triple buffering and resets the
 * pan on a normal exit. An App that exits through pressMenu2Kill SIGKILLs it,
 * leaving the pan non-zero. Our single-buffered SDL surface is drawn at offset
 * 0, so every frame would be invisible. Best effort: never fails startup. */
static void reset_framebuffer_pan(const char *when)
{
    int fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "Cannot open /dev/fb0 to check pan (%s): %s\n", when, strerror(errno));
        return;
    }
    struct fb_var_screeninfo var;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
        fprintf(stderr, "Cannot read framebuffer pan (%s): %s\n", when, strerror(errno));
    }
    else if (var.xoffset != 0 || var.yoffset != 0) {
        unsigned x = var.xoffset, y = var.yoffset;
        var.xoffset = 0;
        var.yoffset = 0;
        if (ioctl(fd, FBIOPAN_DISPLAY, &var) < 0 && ioctl(fd, FBIOPUT_VSCREENINFO, &var) < 0) {
            fprintf(stderr, "Cannot reset framebuffer pan from %u,%u (%s): %s\n", x, y, when,
                    strerror(errno));
        }
        else {
            fprintf(stderr, "Reset framebuffer pan from %u,%u to 0,0 (%s)\n", x, y, when);
        }
    }
    close(fd);
}
#endif

/* Miyoo's SDL saves the framebuffer settings when video starts and writes them
 * back in SDL_Quit (FB_VideoQuit: FBIOPUT_VSCREENINFO with saved_vinfo). After
 * a killed RetroArch that restores its non-zero pan, and the next program's
 * single-buffered SDL draws off screen. Stock MainUI never calls SDL_Quit on a
 * normal exit. Keep the clean shutdown and reset the pan afterwards instead.
 * The reset before SDL_Init normally makes the saved pan 0 already. */
static void quit_sdl(const MainUIApp *ui)
{
#ifdef MAINUI_ONION
    bool device = ui->real_device && !ui->snapshot;
#else
    (void)ui;
#endif
    SDL_Quit();
#ifdef MAINUI_ONION
    if (device) {
        reset_framebuffer_pan("exit");
    }
#endif
}

int mainui_setup_session(MainUIApp *ui, int argc, char **argv)
{
    MainUIOptions options;
    int parsed = mainui_options_parse(&options, argc, argv);
    if (parsed >= 0) {
        ui->informational = options.informational;
        return parsed;
    }
    ui->dir = options.dir;
    ui->base = options.base;
    ui->config_dir = options.config_dir;
    ui->list_path = options.list_path;
    ui->snapshot = options.snapshot;
    ui->handoff_dir = options.handoff_dir;
    ui->device_directory = options.device_directory;
    ui->sd = options.sd;
    ui->system_name = options.system_name;
    ui->input_script = options.input_script;
    ui->input_text = options.input_text;
    ui->real_device = options.real_device;
    ui->battery_override = options.battery_override;
    ui->battery_charging = options.battery_charging;
    ui->start_systems = options.start_systems;
    ui->refresh_caches = options.refresh_caches;
    ui->snapshot_elapsed = options.snapshot_elapsed;
    ui->battery_percent = options.battery_percent;
    ui->device_adapter = (MainUIDeviceAdapter){0};
    ui->device_status = (MainUIDeviceStatus){.battery = -1, .sleeping = -1, .wifi = -1, .lid = -1};
    ui->next_device_check = 0;
    ui->device_enabled = false;
#ifdef MAINUI_ONION
    ui->handheld_input = true;
#else
    ui->handheld_input = false;
#endif
    if (ui->real_device || ui->device_directory) {
        if ((ui->real_device && ui->device_directory) || !ui->sd ||
            !mainui_device_adapter_open(&ui->device_adapter,
                                        ui->real_device ? DEVICE_ONION : DEVICE_SIMULATED,
                                        ui->real_device ? "/tmp" : ui->device_directory) ||
            (ui->real_device && !mainui_device_signals_install())) {
            fprintf(stderr, "Device adapter unavailable or conflicting options\n");
            return 2;
        }
        ui->device_enabled = true;
        if (!ui->handoff_dir) {
            ui->handoff_dir = ui->device_adapter.runtime;
        }
        ui->device_status = mainui_device_status(&ui->device_adapter);
        if (!ui->battery_override) {
            ui->battery_percent = ui->device_status.battery;
        }
    }
    if (ui->sd) {
        /* Patcher startup contract: restore hidden Recent data before readers,
         * regardless of whether the Recent menu is visible. */
        if (!mainui_library_restore_recent(ui->sd)) {
            fprintf(stderr, "Could not restore hidden Recent list.\n");
        }
        if (!mainui_catalog_path(ui->fallback_path, ui->sd, ui->sd, "miyoo/app") ||
            !mainui_catalog_path(ui->settings_path, ui->sd, ui->sd, "system.json") ||
            !mainui_catalog_path(ui->default_config, ui->sd, ui->sd, ".tmp_update/config")) {
            return 2;
        }
        if (!ui->base) {
            ui->base = ui->dir ? ui->dir : ui->fallback_path;
        }
        if (!ui->config_dir) {
            ui->config_dir = ui->default_config;
        }
        if (!ui->dir) {
            char *text = mainui_read_text(ui->settings_path, 1024 * 1024);
            cJSON *system_settings = text ? cJSON_Parse(text) : NULL;
            const cJSON *active = cJSON_GetObjectItemCaseSensitive(system_settings, "theme");
            const char *value = cJSON_IsString(active) ? active->valuestring : "";
            ui->dir = ui->base;
            if (*value && strcmp(value, "./")) {
                if (mainui_catalog_path(ui->resolved_theme, ui->sd, ui->sd, value)) {
                    ui->dir = ui->resolved_theme;
                }
                else {
                    /* Like a missing theme folder: the stock theme still starts. */
                    fprintf(stderr, "Ignoring unusable theme path in system.json: %.80s\n", value);
                }
            }
            cJSON_Delete(system_settings);
            free(text);
        }
    }
    if (!ui->base) {
        ui->base = ui->dir;
    }
    if (ui->config_dir) {
        mainui_config_load(&ui->config, ui->config_dir);
    }
    else {
        mainui_config_parse(&ui->config, NULL, NULL, NULL, NULL);
    }
    ui->list = calloc(1, sizeof *ui->list);
    ui->catalog = ui->sd ? calloc(1, sizeof *ui->catalog) : NULL;
    bool catalog_ok =
        ui->catalog && mainui_catalog_open(ui->catalog, ui->sd, ui->config.case_sensitive);
    if (ui->catalog && !catalog_ok && ui->catalog->unreadable) {
        /* An unusable Emu leaves an empty Games list; Apps, settings and
         * shutdown still work, which beats a launcher that cannot start. */
        fprintf(stderr, "%s; starting with an empty Games list\n", ui->catalog->error);
        catalog_ok = true;
    }
    if (!ui->list || (ui->sd && !catalog_ok) ||
        (!ui->sd && !mainui_load_list(ui->list, ui->list_path))) {
        fprintf(stderr, "%s\n",
                ui->catalog ? ui->catalog->error : "Cannot read list or allocate catalog");
        if (ui->catalog) {
            mainui_catalog_close(ui->catalog);
        }
        free(ui->catalog);
        if (ui->list) {
            mainui_close_list(ui->list);
        }
        free(ui->list);
        return 2;
    }
    if (ui->refresh_caches && ui->catalog &&
        !mainui_browser_refresh_all(ui->sd, ui->config.case_sensitive, ui->catalog->error)) {
        fprintf(stderr, "%s\n", ui->catalog->error);
        mainui_catalog_close(ui->catalog);
        free(ui->catalog);
        mainui_close_list(ui->list);
        free(ui->list);
        return 4;
    }
    if (ui->system_name) {
        int found = -1;
        for (int i = 0; i < ui->catalog->pages[0].count; i++) {
            if (!strcmp(ui->catalog->pages[0].entries[i].label, ui->system_name)) {
                found = i;
                break;
            }
        }
        if (found < 0 || !mainui_catalog_enter(ui->catalog, found)) {
            fprintf(stderr, "Cannot open requested system: %s\n", ui->system_name);
            mainui_catalog_close(ui->catalog);
            free(ui->catalog);
            mainui_close_list(ui->list);
            free(ui->list);
            return 2;
        }
        mainui_grid_restore(&ui->catalog->pages[0].view, ui->catalog->pages[0].count, found, 4, 2);
    }
    return -1;
}

int mainui_setup_video(MainUIApp *ui)
{
    if (ui->snapshot) {
        SDL_putenv("SDL_VIDEODRIVER=dummy");
    }
#ifdef MAINUI_ONION
    /* Reset before SDL saves the framebuffer settings, so SDL_Quit restores
     * pan 0 rather than a killed RetroArch's buffer (a brief flash of its last
     * frame on exit). The resets after SDL_SetVideoMode and SDL_Quit stay. */
    if (ui->real_device && !ui->snapshot) {
        reset_framebuffer_pan("before SDL");
    }
#endif
    long long t0 = startup_ms();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        if (ui->catalog) {
            mainui_catalog_close(ui->catalog);
        }
        free(ui->catalog);
        mainui_close_list(ui->list);
        free(ui->list);
        return 3;
    }
    long long t1 = startup_ms();
    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF: %s\n", TTF_GetError());
        quit_sdl(ui);
        if (ui->catalog) {
            mainui_catalog_close(ui->catalog);
        }
        free(ui->catalog);
        mainui_close_list(ui->list);
        free(ui->list);
        return 3;
    }
    long long t2 = startup_ms();
    /* Request the real device surface; our software screen already buffers
     * drawing. Host and deterministic snapshots retain software video. */
    Uint32 video_flags = ui->real_device && !ui->snapshot ? SDL_HWSURFACE : SDL_SWSURFACE;
    ui->display = SDL_SetVideoMode(640, 480, 32, video_flags);
    long long t3 = startup_ms();
#ifdef MAINUI_ONION
    if (ui->display && ui->real_device) {
        reset_framebuffer_pan("startup");
    }
#endif
    ui->screen = ui->display;
    if (ui->display && ui->real_device) {
        ui->screen = SDL_CreateRGBSurface(
            SDL_SWSURFACE, 640, 480, ui->display->format->BitsPerPixel, ui->display->format->Rmask,
            ui->display->format->Gmask, ui->display->format->Bmask, 0);
    }
    if (!ui->screen || !mainui_theme_open_sd(&ui->theme, ui->dir, ui->base, ui->sd, &ui->config)) {
        fprintf(stderr, "Cannot initialize theme/display: %s\n",
                ui->screen && *ui->theme.error ? ui->theme.error : SDL_GetError());
        if (ui->screen && ui->screen != ui->display) {
            SDL_FreeSurface(ui->screen);
        }
        TTF_Quit();
        quit_sdl(ui);
        if (ui->catalog) {
            mainui_catalog_close(ui->catalog);
        }
        free(ui->catalog);
        mainui_close_list(ui->list);
        free(ui->list);
        return 3;
    }
    if (ui->sd) {
        mainui_language_load(ui->sd, ui->theme.fallback);
    }
    /* Which build runs where, for reports that come with only a log. */
    fprintf(stderr, "[startup] Open MainUI %s, device %d, sd %s, theme %s\n", MAINUI_VERSION,
            ui->device_status.model.id, ui->sd ? ui->sd : "-", ui->theme.directory);
    /* Kept deliberately for startup timing diagnostics. */
    fprintf(stderr, "[startup] sdl-init %lld ttf-init %lld set-mode %lld theme %lld\n", t1 - t0,
            t2 - t1, t3 - t2, startup_ms() - t3);
    SDL_WM_SetCaption("Open MainUI", NULL);
    SDL_EnableKeyRepeat(ui->config.repeat_delay, ui->config.repeat_interval);
    SDL_EnableUNICODE(1);
    return -1;
}

static bool environment_name(const char *entry, const char *name)
{
    size_t length = strlen(name);
    return !strncmp(entry, name, length) && entry[length] == '=';
}

static char **video_environment(const char *directory, const char *libraries, const char *home,
                                char **home_value, char **libraries_value, char **path_value)
{
    const char *old_libraries = getenv("LD_LIBRARY_PATH");
    const char *old_path = getenv("PATH");
    size_t count = 0;
    for (char **entry = environ; entry && *entry; entry++) {
        if (!environment_name(*entry, "HOME") && !environment_name(*entry, "LD_LIBRARY_PATH") &&
            !environment_name(*entry, "PATH")) {
            count++;
        }
    }
    char **environment = calloc(count + 4, sizeof *environment);
    size_t home_size = strlen(home) + sizeof "HOME=";
    size_t library_size = sizeof "LD_LIBRARY_PATH=" + strlen(libraries) +
                          (old_libraries && *old_libraries ? strlen(old_libraries) + 1 : 0);
    size_t path_size = sizeof "PATH=" + strlen(directory) + 5 + (old_path ? strlen(old_path) : 0);
    *home_value = malloc(home_size);
    *libraries_value = malloc(library_size);
    *path_value = malloc(path_size);
    if (!environment || !*home_value || !*libraries_value || !*path_value) {
        free(environment);
        free(*home_value);
        free(*libraries_value);
        free(*path_value);
        return NULL;
    }
    snprintf(*home_value, home_size, "HOME=%s", home);
    snprintf(*libraries_value, library_size, "LD_LIBRARY_PATH=%s%s%s", libraries,
             old_libraries && *old_libraries ? ":" : "",
             old_libraries && *old_libraries ? old_libraries : "");
    snprintf(*path_value, path_size, "PATH=%s/bin:%s", directory, old_path ? old_path : "");
    size_t index = 0;
    for (char **entry = environ; entry && *entry; entry++) {
        if (!environment_name(*entry, "HOME") && !environment_name(*entry, "LD_LIBRARY_PATH") &&
            !environment_name(*entry, "PATH")) {
            environment[index++] = *entry;
        }
    }
    environment[index++] = *home_value;
    environment[index++] = *libraries_value;
    environment[index] = *path_value;
    return environment;
}

static bool wait_media_child(pid_t child, int *exit_code)
{
    if (child <= 0)
        return false;
    int status = 0, inputs[16];
    for (int i = 0; i < (int)(sizeof inputs / sizeof inputs[0]); i++) {
        char device[32];
        snprintf(device, sizeof device, "/dev/input/event%d", i);
        inputs[i] = open(device, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    }
    bool stop_sent = false, kill_sent = false, finished = false;
    Uint32 stop_at = 0;
    while (!finished) {
        pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child || (waited < 0 && errno != EINTR)) {
            finished = true;
            if (waited == child)
                *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -2;
            continue;
        }
        for (size_t device = 0; device < sizeof inputs / sizeof inputs[0]; device++) {
            if (inputs[device] < 0)
                continue;
            struct input_event events[16];
            ssize_t bytes = read(inputs[device], events, sizeof events);
            if (bytes > 0) {
                size_t count = (size_t)bytes / sizeof events[0];
                for (size_t i = 0; i < count; i++) {
                    bool back = events[i].code == KEY_LEFTCTRL || events[i].code == KEY_B ||
                                events[i].code == KEY_ESC;
                    if (events[i].type == EV_KEY && back && events[i].value != 2 && !stop_sent) {
                        stop_sent = true;
                        stop_at = SDL_GetTicks();
                        kill(child, SIGTERM);
                    }
                }
            }
        }
        if (stop_sent && !kill_sent && SDL_GetTicks() - stop_at > 500) {
            kill_sent = true;
            kill(child, SIGKILL);
        }
        poll(NULL, 0, 20);
    }
    for (size_t device = 0; device < sizeof inputs / sizeof inputs[0]; device++) {
        if (inputs[device] >= 0)
            close(inputs[device]);
    }
    return *exit_code == 0;
}

static bool run_ffplay(const char *sd, const char *path)
{
    struct VideoPaths {
        char directory[4096], player[4096], libraries[4096];
        char log_directory[4096], log_path[4096], position_file[4096], position_backup[4096];
    } *paths = calloc(1, sizeof *paths);

    if (!paths) {
        return false;
    }
    if (snprintf(paths->directory, sizeof paths->directory, "%s/Emu/ffplay", sd) >=
            (int)sizeof paths->directory ||
        snprintf(paths->player, sizeof paths->player, "%s/bin/ffplay", paths->directory) >=
            (int)sizeof paths->player ||
        snprintf(paths->libraries, sizeof paths->libraries, "%s/libs", paths->directory) >=
            (int)sizeof paths->libraries ||
        snprintf(paths->log_directory, sizeof paths->log_directory, "%s/.tmp_update/logs", sd) >=
            (int)sizeof paths->log_directory ||
        snprintf(paths->log_path, sizeof paths->log_path, "%s/MainUI-video.log",
                 paths->log_directory) >= (int)sizeof paths->log_path ||
        snprintf(paths->position_file, sizeof paths->position_file, "%s/pos.cfg",
                 paths->directory) >= (int)sizeof paths->position_file ||
        snprintf(paths->position_backup, sizeof paths->position_backup, "%s/.MainUI-pos.cfg-backup",
                 paths->directory) >= (int)sizeof paths->position_backup) {
        free(paths);
        return false;
    }
    struct stat info;
    if (access(paths->player, X_OK) != 0 || stat(paths->libraries, &info) != 0 ||
        !S_ISDIR(info.st_mode)) {
        free(paths);
        return false;
    }
    mkdir(paths->log_directory, 0755);
    if (access(paths->position_backup, F_OK) == 0) {
        unlink(paths->position_file);
        rename(paths->position_backup, paths->position_file);
    }
    bool had_position_file = access(paths->position_file, F_OK) == 0;
    bool position_hidden =
        had_position_file && rename(paths->position_file, paths->position_backup) == 0;
    int log = open(paths->log_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (log < 0) {
        snprintf(paths->log_path, sizeof paths->log_path, "%s", "/tmp/MainUI-video.log");
        log = open(paths->log_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    }
    if (log >= 0) {
        dprintf(log, "video=%s\nstart_seconds=0\nposcfg_hidden=%d\n", path, position_hidden);
    }

    char home_template[] = "/tmp/mainui-ffplay-XXXXXX";
    char *home = mkdtemp(home_template);
    if (!home) {
        if (log >= 0)
            close(log);
        if (position_hidden)
            rename(paths->position_backup, paths->position_file);
        free(paths);
        return false;
    }
    char *home_value = NULL, *libraries_value = NULL, *path_value = NULL;
    char **environment = video_environment(paths->directory, paths->libraries, home, &home_value,
                                           &libraries_value, &path_value);
    if (!environment) {
        if (log >= 0)
            close(log);
        rmdir(home);
        if (position_hidden)
            rename(paths->position_backup, paths->position_file);
        free(paths);
        return false;
    }
    char *const arguments[] = {paths->player, "-loglevel", "info", "-vf",        "hflip,vflip",
                               "-ss",         "0",         "-i",   (char *)path, NULL};
    struct sigaction reset = {.sa_handler = SIG_DFL};
    sigemptyset(&reset.sa_mask);
    sigset_t empty;
    sigemptyset(&empty);
    fflush(NULL);
    FILE *governor = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "wb");
    if (governor) {
        fputs("performance\n", governor);
        fclose(governor);
    }
    bool remove_stay_awake = access("/tmp/stay_awake", F_OK) != 0;
    int stay_awake = open("/tmp/stay_awake", O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (stay_awake >= 0)
        close(stay_awake);
    Uint32 started = SDL_GetTicks();
    pid_t child = fork();
    if (child == 0) {
        sigaction(SIGCHLD, &reset, NULL);
        sigaction(SIGINT, &reset, NULL);
        sigaction(SIGTERM, &reset, NULL);
        sigaction(SIGHUP, &reset, NULL);
        sigprocmask(SIG_SETMASK, &empty, NULL);
        if (chdir(home) != 0)
            _exit(127);
        if (log >= 0) {
            if (dup2(log, STDOUT_FILENO) < 0 || dup2(log, STDERR_FILENO) < 0)
                _exit(127);
            close(log);
        }
        execve(paths->player, arguments, environment);
        _exit(127);
    }
    if (log >= 0)
        close(log);
    int exit_code = -1;
    bool success = wait_media_child(child, &exit_code);
    log = open(paths->log_path, O_WRONLY | O_APPEND | O_CLOEXEC);
    if (log >= 0) {
        dprintf(log, "\n[MainUI] elapsed_ms=%u ffplay_exit=%d success=%d\n",
                SDL_GetTicks() - started, exit_code, success);
        close(log);
    }
    if (position_hidden) {
        unlink(paths->position_file);
        if (rename(paths->position_backup, paths->position_file) != 0) {
            fprintf(stderr, "[video] Could not restore FFplay position file: %s\n",
                    strerror(errno));
        }
    }
    else if (!had_position_file) {
        unlink(paths->position_file);
    }
    char position_file[4096];
    if (snprintf(position_file, sizeof position_file, "%s/pos.cfg", home) <
        (int)sizeof position_file) {
        unlink(position_file);
    }
    rmdir(home);
    free(environment);
    free(home_value);
    free(libraries_value);
    free(path_value);
    free(paths);
    if (remove_stay_awake)
        unlink("/tmp/stay_awake");
    return success;
}

static bool run_green_pdf(const char *sd, const char *pdf)
{
    struct GreenPaths {
        char system[4096], player[4096], libraries[4096], log_directory[4096], log[4096];
    } *paths = calloc(1, sizeof *paths);

    if (!paths)
        return false;
    if (snprintf(paths->system, sizeof paths->system, "%s/.tmp_update", sd) >=
            (int)sizeof paths->system ||
        snprintf(paths->player, sizeof paths->player, "%s/bin/green/green", paths->system) >=
            (int)sizeof paths->player ||
        snprintf(paths->libraries, sizeof paths->libraries, "%s/lib/parasyte", paths->system) >=
            (int)sizeof paths->libraries ||
        snprintf(paths->log_directory, sizeof paths->log_directory, "%s/.tmp_update/logs", sd) >=
            (int)sizeof paths->log_directory ||
        snprintf(paths->log, sizeof paths->log, "%s/MainUI-pdf.log", paths->log_directory) >=
            (int)sizeof paths->log) {
        free(paths);
        return false;
    }
    struct stat info;
    if (access(paths->player, X_OK) != 0 || stat(paths->libraries, &info) != 0 ||
        !S_ISDIR(info.st_mode)) {
        free(paths);
        return false;
    }
    char *home = getenv("HOME");
    char *home_value = NULL, *libraries_value = NULL, *path_value = NULL;
    char **environment =
        video_environment(paths->system, paths->libraries, home && *home ? home : paths->system,
                          &home_value, &libraries_value, &path_value);
    if (!environment) {
        free(paths);
        return false;
    }
    char *const arguments[] = {paths->player, (char *)pdf, NULL};
    char previous_governor[32] = "";
    FILE *governor = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "rb");
    if (governor) {
        if (fgets(previous_governor, sizeof previous_governor, governor)) {
            previous_governor[strcspn(previous_governor, "\r\n")] = 0;
        }
        fclose(governor);
    }
    governor = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "wb");
    if (governor) {
        fputs("performance\n", governor);
        fclose(governor);
    }
    mkdir(paths->log_directory, 0755);
    int log = open(paths->log, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (log >= 0)
        dprintf(log, "pdf=%s\n", pdf);
    bool remove_stay_awake = access("/tmp/stay_awake", F_OK) != 0;
    int stay_awake = open("/tmp/stay_awake", O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (stay_awake >= 0)
        close(stay_awake);
    struct sigaction reset = {.sa_handler = SIG_DFL};
    sigemptyset(&reset.sa_mask);
    sigset_t empty;
    sigemptyset(&empty);
    fflush(NULL);
    pid_t child = fork();
    if (child == 0) {
        sigaction(SIGCHLD, &reset, NULL);
        sigaction(SIGINT, &reset, NULL);
        sigaction(SIGTERM, &reset, NULL);
        sigaction(SIGHUP, &reset, NULL);
        sigprocmask(SIG_SETMASK, &empty, NULL);
        if (chdir(paths->system) != 0)
            _exit(127);
        if (log >= 0 && (dup2(log, STDOUT_FILENO) < 0 || dup2(log, STDERR_FILENO) < 0))
            _exit(127);
        if (log >= 0)
            close(log);
        execve(paths->player, arguments, environment);
        _exit(127);
    }
    if (log >= 0)
        close(log);
    int exit_code = -1;
    bool success = wait_media_child(child, &exit_code);
    if (*previous_governor) {
        governor = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "wb");
        if (governor) {
            fprintf(governor, "%s\n", previous_governor);
            fclose(governor);
        }
    }
    log = open(paths->log, O_WRONLY | O_APPEND | O_CLOEXEC);
    if (log >= 0) {
        dprintf(log, "\n[MainUI] exit=%d success=%d\n", exit_code, success);
        close(log);
    }
    free(environment);
    free(home_value);
    free(libraries_value);
    free(path_value);
    if (remove_stay_awake)
        unlink("/tmp/stay_awake");
    free(paths);
    return success;
}

static bool resume_device_video(MainUIApp *ui)
{
#ifdef MAINUI_ONION
    reset_framebuffer_pan("before video resume");
#endif
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "[video] SDL video resume failed: %s\n", SDL_GetError());
        return false;
    }
    ui->display = SDL_SetVideoMode(640, 480, 32, SDL_HWSURFACE);
#ifdef MAINUI_ONION
    if (ui->display)
        reset_framebuffer_pan("video resume");
#endif
    ui->screen = ui->display;
    if (ui->display) {
        ui->screen = SDL_CreateRGBSurface(
            SDL_SWSURFACE, 640, 480, ui->display->format->BitsPerPixel, ui->display->format->Rmask,
            ui->display->format->Gmask, ui->display->format->Bmask, 0);
    }
    if (!ui->screen) {
        fprintf(stderr, "[video] SDL display restore failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_WM_SetCaption("Open MainUI", NULL);
    SDL_EnableKeyRepeat(ui->config.repeat_delay, ui->config.repeat_interval);
    SDL_EnableUNICODE(1);
    SDL_Event stale[16];
    while (SDL_PeepEvents(stale, 16, SDL_GETEVENT, SDL_KEYDOWNMASK | SDL_KEYUPMASK) > 0) {
    }
    memset(ui->held, 0, sizeof ui->held);
    ui->x_armed = ui->y_armed = ui->select_armed = ui->start_armed = false;
    ui->select_brightness = false;
    ui->return_latched = false;
    return true;
}

void mainui_play_video(MainUIApp *ui, const char *path)
{
    if (!ui->real_device || !ui->sd) {
        mainui_media_video_finished(&ui->media, false);
        snprintf(ui->media.status, sizeof ui->media.status,
                 "Video playback requires the handheld build.");
        return;
    }
    bool resume_audio = mainui_audio_available();
    if (ui->screen && ui->screen != ui->display)
        SDL_FreeSurface(ui->screen);
    ui->screen = ui->display = NULL;
    mainui_audio_close();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    bool success = run_ffplay(ui->sd, path);
    bool video_ready = resume_device_video(ui);
    if (video_ready && resume_audio) {
        cJSON *system = mainui_system_read(ui->sd);
        int volume = mainui_system_monitor_value(system, "bgmvol");
        int audiofix = -1;
        if (!mainui_device_setting_value(&ui->device_adapter, "audiofix", &audiofix)) {
            audiofix = -1;
        }
        mainui_audio_open(ui->theme.directory, ui->theme.fallback, volume,
                          audiofix == 0 ? 4096 : 1024);
        cJSON_Delete(system);
        mainui_audio_pause(ui->device_enabled && ui->device_status.sleeping == 1);
    }
    mainui_media_video_finished(&ui->media, success);
    ui->cached_start = -1;
    ui->idle_tick = false;
    if (!video_ready) {
        ui->running = false;
        ui->status = 3;
    }
}

void mainui_play_pdf(MainUIApp *ui, const char *path)
{
    if (!ui->real_device || !ui->sd) {
        snprintf(ui->media.status, sizeof ui->media.status,
                 "PDF Reader requires the handheld build.");
        return;
    }
    bool resume_audio = mainui_audio_available();
    if (ui->screen && ui->screen != ui->display)
        SDL_FreeSurface(ui->screen);
    ui->screen = ui->display = NULL;
    mainui_audio_close();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    run_green_pdf(ui->sd, path);
    bool video_ready = resume_device_video(ui);
    if (video_ready && resume_audio) {
        cJSON *system = mainui_system_read(ui->sd);
        int volume = mainui_system_monitor_value(system, "bgmvol");
        int audiofix = -1;
        if (!mainui_device_setting_value(&ui->device_adapter, "audiofix", &audiofix)) {
            audiofix = -1;
        }
        mainui_audio_open(ui->theme.directory, ui->theme.fallback, volume,
                          audiofix == 0 ? 4096 : 1024);
        cJSON_Delete(system);
        mainui_audio_pause(ui->device_enabled && ui->device_status.sleeping == 1);
    }
    mainui_media_video_finished(&ui->media, true);
    ui->cached_start = -1;
    ui->idle_tick = false;
    if (!video_ready) {
        ui->running = false;
        ui->status = 3;
    }
}

/* The return from a launch of Onion's Search tool (search, clear, or its
 * settings), as opposed to a game Search found. */
static bool search_tool_return(const cJSON *record)
{
    const cJSON *tool = cJSON_GetObjectItemCaseSensitive(record, "launch");
    const cJSON *rom = cJSON_GetObjectItemCaseSensitive(record, "rompath");
    return cJSON_IsString(tool) && cJSON_IsString(rom) &&
           !strcmp(tool->valuestring, "/mnt/SDCARD/App/Search/launch.sh") &&
           (!strcmp(rom->valuestring, tool->valuestring) || !strcmp(rom->valuestring, "search") ||
            !strcmp(rom->valuestring, "clear") || !strncmp(rom->valuestring, "setstate:", 9) ||
            strstr(rom->valuestring, "/App/Search/data/") ||
            cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(record, "app_action")));
}

/* The stock state.json, which stock MainUI follows when it starts. Onion's
 * X-button shortcut (keymon) starts Search by killing MainUI, so no return
 * envelope exists; Search then leaves Games -> Search there. That state names
 * the console only by its label: copy it out for comparing with the Search
 * system found by its data folder (empty unless the last screen is a console
 * list). Onion's setState, run at boot for the startup tab and by its Expert
 * app, leaves the main menu and one tab. Returns that tab, or -1. */
static int stock_state_read(const char *directory, char label[256])
{
    label[0] = 0;
    char path[4096];
    if (snprintf(path, sizeof path, "%s/state.json", directory) >= (int)sizeof path) {
        return -1;
    }
    char *text = mainui_read_text(path, 64 * 1024);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "list");
    int count = cJSON_GetArraySize(list);
    const cJSON *last = cJSON_GetArrayItem(list, count - 1);
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(last, "type");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(last, "emuname");
    if (cJSON_IsNumber(type) && type->valueint == 5 && cJSON_IsString(name)) {
        snprintf(label, 256, "%s", name->valuestring);
    }
    /* setState's page types, in stock's numbering. */
    static const int types[] = {10, 2, 1, 16, 3};
    static const MainUIMenuSection tabs[] = {MAINUI_MENU_RECENTS, MAINUI_MENU_FAVORITES,
                                             MAINUI_MENU_GAMES, MAINUI_MENU_EXPERT,
                                             MAINUI_MENU_APPS};
    int tab = -1;
    for (int i = 0; count == 2 && cJSON_IsNumber(type) && i < 5; ++i) {
        if (type->valueint == types[i]) {
            tab = tabs[i];
        }
    }
    cJSON_Delete(root);
    free(text);
    return tab;
}

/* Labels as stock pads them (" Search "), compared without the padding. */
static bool same_label(const char *a, const char *b)
{
    while (*a == ' ') {
        a++;
    }
    while (*b == ' ') {
        b++;
    }
    size_t la = strlen(a), lb = strlen(b);
    while (la && a[la - 1] == ' ') {
        la--;
    }
    while (lb && b[lb - 1] == ' ') {
        lb--;
    }
    return la == lb && !strncmp(a, b, la);
}

void mainui_restore_session(MainUIApp *ui)
{
    ui->preview_sync_once = true;
    mainui_viewport_restore(
        &ui->view, ui->catalog ? ui->catalog->pages[ui->catalog->depth].count : ui->list->count,
        ui->config.rows, 0, 0, ui->config.rows - 1);
    ui->home = ui->catalog && !ui->system_name && !ui->start_systems;
    ui->settings_open = false;
    ui->language_open = false;
    ui->context = (MainUIContext){0};
    ui->home_context = (MainUIContext){0};
    mainui_context_load(&ui->home_context, ui->config_dir, ui->sd);
    ui->context_record = NULL;
    ui->context_open = false;
    ui->confirmation = -1;
    ui->settings = (MainUIStockSettings){0};
    ui->settings_page = (MainUISettingsPage){0};
    ui->settings_keyboard = 0;
    ui->search = (MainUISearch){0};
    ui->catalog_generation = 0;
    ui->search_confirm_held = false;
    ui->timer_interval = 0;
    ui->search_keyboard = false;
    ui->wifi_was_visible = false;
    ui->apps = NULL;
    ui->games = ui->catalog;
    ui->expert = NULL;
    ui->games_view = ui->view;
    ui->expert_view = (MainUIViewport){0};
    ui->apps_view = (MainUIViewport){0};
    memset(ui->saved_library_views, 0, sizeof ui->saved_library_views);
    memset(ui->message_title, 0, sizeof ui->message_title);
    memset(ui->message_body, 0, sizeof ui->message_body);
    ui->library = NULL;
    /* Without markers, opening a section or a menu tries again. */
    ui->favorites = NULL;
    mainui_markers_refresh(ui, true);
    memset(ui->favorite_rows, 0, sizeof ui->favorite_rows);
    ui->favorite_editor = (MainUIFavoriteEditor){0};
    ui->name_input = (MainUINameInput){0};
    ui->name_action = CONTEXT_FAVORITE_CREATE;
    ui->systems_view = ui->view;
    mainui_menu_load(&ui->menu, ui->config_dir, &ui->config);
    mainui_viewport_restore(&ui->home_view, ui->menu.count, 4, 0, 0, 3);
    if (ui->catalog && !ui->catalog->depth) {
        mainui_grid_restore(&ui->view, ui->view.total, 0, 4, 2);
    }
    ui->theme.battery_percent = ui->battery_percent;
    ui->theme.battery_charging =
        ui->battery_override ? ui->battery_charging : ui->device_status.charging;
    mainui_menu_view_open(&ui->menu_view, &ui->theme);
    /* Snapshots and scripted input draw at once, with every icon in place. */
    ui->menu_view.defer_selected = !ui->snapshot && !ui->input_script;
    if (ui->handoff_dir && !ui->system_name && !ui->start_systems) {
        cJSON *returned = mainui_launch_take_return(ui->handoff_dir);
        /* A restored catalog is read fresh from the SD card, so it already has
         * the Games -> Search that Search created while MainUI was away. */
        bool from_search = search_tool_return(cJSON_GetObjectItemCaseSensitive(returned, "record"));
        /* No envelope: MainUI was killed, not left through its own launch. */
        char stock_label[256] = "";
        /* A launch still pending owns the state.json written for it: it is
         * neither followed nor removed. */
        char state_path[4096], pending_path[4096];
        struct stat pending;
        bool paths = snprintf(state_path, sizeof state_path, "%s/state.json", ui->handoff_dir) <
                         (int)sizeof state_path &&
                     snprintf(pending_path, sizeof pending_path, "%s/cmd_to_run.sh",
                              ui->handoff_dir) < (int)sizeof pending_path;
        bool idle = paths && lstat(pending_path, &pending) != 0 && errno == ENOENT;
        int stock_tab = idle ? stock_state_read(ui->handoff_dir, stock_label) : -1;
        if (returned) {
            stock_label[0] = 0;
        }
        MainUISession restored = {0};
        bool resumed =
            returned &&
            mainui_session_restore(&restored, ui->sd, ui->config.case_sensitive, ui->config.rows,
                                   cJSON_GetObjectItemCaseSensitive(returned, "resume"),
                                   cJSON_GetObjectItemCaseSensitive(returned, "record"));
        if (returned && !resumed) {
            const cJSON *section = cJSON_GetObjectItemCaseSensitive(
                cJSON_GetObjectItemCaseSensitive(returned, "resume"), "section");
            fprintf(stderr,
                    "[return] the saved screen (section %d) could not be restored; "
                    "starting on Home\n",
                    cJSON_IsNumber(section) ? section->valueint : -1);
        }
        if (resumed) {
            mainui_viewport_restore(&ui->home_view, ui->menu.count, 4, restored.home.selected,
                                    restored.home.start, restored.home.end);
            if (restored.home_only) {
                ui->home = true;
            }
            else if (restored.section == MAINUI_MENU_SETTINGS) {
                mainui_stock_settings_load(&ui->settings, ui->config_dir, ui->sd,
                                           ui->device_status.model.id);
                ui->settings.selected = restored.view.selected;
                if (ui->settings.selected < 0 || ui->settings.selected >= ui->settings.count) {
                    ui->settings.selected = 0;
                }
                ui->settings.start = restored.view.start;
                if (ui->settings.start < 0 || ui->settings.selected < ui->settings.start ||
                    ui->settings.selected >= ui->settings.start + 6) {
                    ui->settings.start = ui->settings.selected >= 6 ? ui->settings.selected - 5 : 0;
                }
                ui->home = true;
                ui->settings_open = true;
            }
            else if (restored.library) {
                ui->library = restored.library;
                restored.library = NULL;
                ui->systems_view = ui->view;
                ui->view = restored.view;
                ui->home = false;
            }
            else if (restored.section == MAINUI_MENU_APPS) {
                ui->apps = restored.catalog;
                restored.catalog = NULL;
                ui->apps_view = restored.view;
                ui->home = true;
            }
            else {
                if (restored.section == MAINUI_MENU_EXPERT) {
                    ui->expert = restored.catalog;
                    ui->expert_view = restored.view;
                }
                else {
                    mainui_catalog_close(ui->games);
                    free(ui->games);
                    ui->games = restored.catalog;
                    ui->games_view = restored.view;
                }
                ui->catalog = restored.catalog;
                restored.catalog = NULL;
                ui->view = restored.view;
                ui->home = false;
            }
        }
        const cJSON *resume = cJSON_GetObjectItemCaseSensitive(returned, "resume");
        const cJSON *query = cJSON_GetObjectItemCaseSensitive(resume, "search_query");
        if (!ui->home && !ui->library && cJSON_IsString(query) &&
            mainui_search_open(&ui->search, ui->catalog, &ui->view, query->valuestring,
                               ui->config.rows)) {
            if (mainui_search_restore_view(
                    &ui->search, cJSON_GetObjectItemCaseSensitive(resume, "search_view"),
                    cJSON_GetObjectItemCaseSensitive(returned, "record"), ui->config.rows)) {
                ui->library = ui->search.results;
                ui->view = ui->search.view;
            }
            else {
                fprintf(stderr, "[return] the Search results could not be restored\n");
                mainui_search_close(&ui->search);
            }
        }
        else if (!ui->home && !ui->library && cJSON_IsString(query)) {
            fprintf(stderr, "[return] the Search results could not be reopened\n");
        }
        /* After Search started by keymon: only if the stock state names the
         * console that is the Search system. */
        bool stock_search = false;
        for (int i = 0; *stock_label && i < ui->games->pages[0].count && !stock_search; ++i) {
            stock_search = mainui_catalog_search_system(ui->games, i) &&
                           same_label(ui->games->pages[0].entries[i].label, stock_label);
        }
        if (stock_search && !from_search) {
            fprintf(stderr, "[return] no return file; following the stock state.json to "
                            "Games -> Search\n");
        }
        if (from_search || stock_search) {
            bool found = false;
            while (ui->games->depth) {
                mainui_catalog_back(ui->games);
            }
            for (int i = 0; i < ui->games->pages[0].count; ++i) {
                if (mainui_catalog_search_system(ui->games, i)) {
                    found = true;
                    MainUIViewport target;
                    mainui_grid_restore(&target, ui->games->pages[0].count, i, 4, 2);
                    if (mainui_browser_enter(ui->games, &target, ui->config.rows)) {
                        if (ui->library == ui->search.results) {
                            mainui_search_close(&ui->search);
                            ui->library = NULL;
                        }
                        else if (ui->library) {
                            mainui_library_close(ui->library);
                            free(ui->library);
                            ui->library = NULL;
                        }
                        if (ui->apps) {
                            mainui_catalog_close(ui->apps);
                            free(ui->apps);
                            ui->apps = NULL;
                        }
                        ui->catalog = ui->games;
                        ui->view = ui->games_view = target;
                        ui->home = ui->settings_open = false;
                    }
                    else {
                        fprintf(stderr, "Search returned, but Games -> Search did not open: %s\n",
                                ui->games->error);
                    }
                    break;
                }
            }
            if (!found) {
                /* After Clear search, for example: the ordinary list stands. */
                fprintf(stderr, "Search returned; Games has no Search entry\n");
            }
        }
        /* A tab left by setState opens, as in stock: at boot the startup tab
         * set in Tweaks, after keymon killed MainUI for a shortcut to Onion's
         * Expert app, or on return from that app started from Apps (MainUI's
         * own launch from Apps left Apps there). */
        bool from_apps = resumed && restored.section == MAINUI_MENU_APPS;
        if (stock_tab >= 0 && (!returned || (from_apps && stock_tab != MAINUI_MENU_APPS)) &&
            !from_search && !stock_search && ui->home && !ui->settings_open) {
            fprintf(stderr, "[return] following the stock state.json to %s\n",
                    mainui_menu_label(stock_tab));
            if (ui->apps) {
                mainui_catalog_close(ui->apps);
                free(ui->apps);
                ui->apps = NULL;
            }
            for (int i = 0; i < ui->menu.count; ++i) {
                if (ui->menu.sections[i] == (MainUIMenuSection)stock_tab) {
                    mainui_viewport_move(&ui->home_view, 4, i - ui->home_view.selected, false);
                    break;
                }
            }
            mainui_screen_home_key(ui, SDLK_RETURN, stock_tab);
        }
        /* Read once, as stock unlinks it after restoring: a later start
         * without a new state (after keymon's GameSwitcher, say) is on Home. */
        if (idle) {
            mainui_remove_file(state_path);
        }
        mainui_session_close(&restored);
        cJSON_Delete(returned);
    }
}

void mainui_setup_render(MainUIApp *ui)
{
    memset(ui->labels, 0, sizeof ui->labels);
    memset(ui->highlighted, 0, sizeof ui->highlighted);
    ui->cached_start = -1;
    ui->status = 0;
    ui->heading_color = ui->theme.title_color;
    /* Opening a startup tab (Recents, Favorites) may have drawn one already. */
    if (ui->heading) {
        SDL_FreeSurface(ui->heading);
    }
    ui->heading = TTF_RenderUTF8_Blended(ui->theme.title_font,
                                         ui->search.results ? ui->search.title
                                         : ui->library      ? mainui_library_heading(ui->library)
                                         : ui->catalog      ? mainui_catalog_heading(ui->catalog)
                                                            : "Games",
                                         ui->heading_color);
    ui->timer = NULL;
    ui->selected_at = SDL_GetTicks();
    /* Keymon's long press of Menu arrives as a lone release, which SDL drops;
     * read Menu releases from the input device itself (platform/menu_button.h). */
    if (ui->real_device && !ui->snapshot && !mainui_menu_button_open("/dev/input/event0")) {
        fprintf(stderr, "Cannot read Menu releases from /dev/input/event0; Menu opens no "
                        "context menu\n");
    }
    /* Stock reads keymon's audiofix before loading system.json over it, and
     * opens the mixer with 4096 sample frames with Onion's audio fix (its
     * audioserver) off, as here, and 128 with it on. Here it is 1024 with it
     * on, on purpose: 128 wakes the audio callback eight times as often and
     * raised MainUI's idle CPU use (1.6 % against 0.9 % with 1.0.7's 1024,
     * which played without problems). The latency stays about 21 ms at
     * 48 kHz, unnoticeable for theme sounds and music. Read like stock, from
     * the value keymon already has; the host keeps 1024. */
    int audiofix = -1;
    if (ui->real_device && !ui->snapshot &&
        !mainui_device_setting_value(&ui->device_adapter, "audiofix", &audiofix)) {
        audiofix = -1;
    }
    /* As stock at start: the button mapping in system.json goes to the button
     * driver, which remaps the buttons (X as B, say) for every program. Nothing
     * else in Onion writes it. */
    /* As stock at every start: keymon's live settings from system.json, and
     * the display brightness from them. */
    if (ui->real_device && !ui->snapshot && ui->sd &&
        !mainui_device_settings_load(&ui->device_adapter, ui->sd)) {
        fprintf(stderr, "Cannot load the live settings from system.json\n");
    }
    if (ui->real_device && !ui->snapshot && ui->sd &&
        !mainui_keymap_apply(ui->sd, "/sys/module/gpio_keys_polled/parameters/button_config")) {
        fprintf(stderr, "Cannot hand the button mapping to the button driver\n");
    }
    /* Snapshot tests stay silent. Playback is optional on the host. */
    if (!ui->snapshot) {
        cJSON *system_config = mainui_system_read(ui->sd ? ui->sd : ".");
        /* Stock's 8 when the value is missing or not a number. */
        int level = mainui_system_monitor_value(system_config, "bgmvol");
        if (ui->real_device && audiofix < 0) {
            audiofix = mainui_system_monitor_value(system_config, "audiofix");
        }
        int chunk = audiofix == 0 ? 4096 : 1024;
        /* mainui_audio_open() logs the specific reason. */
        mainui_audio_open(ui->theme.directory, ui->theme.fallback, level, chunk);
        cJSON_Delete(system_config);
    }
    mainui_audio_pause(ui->device_enabled && ui->device_status.sleeping == 1);
    ui->sound_selection = -1;
    ui->sound_section = -1;
    ui->running = true;
    ui->launch_pending = false;
    ui->letter_jump = (MainUILetterJump){0};
    memset(ui->held, 0, sizeof ui->held);
    if (ui->device_enabled) {
        mainui_about_start(&ui->about_job, &ui->device_adapter, ui->sd);
    }
    /* Stock's start-up line, printed before its first frame. Onion's clock
     * overlay (libmainuihooks.so, preloaded) waits for this printf format
     * before it draws the time on each SDL_Flip. Called through a pointer:
     * a fortified build would call __printf_chk, which it does not see. */
    if (ui->real_device && !ui->snapshot) {
        int (*volatile print)(const char *, ...) = printf;
        print("%d run windows\n", 1);
        fflush(stdout);
    }
}

int mainui_teardown(MainUIApp *ui)
{
    mainui_menu_button_close();
    /* A launch saved it already, for the same view: no input came since. */
    if (ui->catalog && !ui->library && !ui->home && !ui->snapshot && !ui->launch_pending &&
        !mainui_positions_save(ui->catalog, &ui->view)) {
        fprintf(stderr, "Could not save ROM-list position on exit.\n");
    }
    mainui_about_close(&ui->about_job);
    mainui_device_job_close(&ui->device_job);
    mainui_catalog_job_close(&ui->catalog_job);
    if (ui->timer) {
        SDL_RemoveTimer(ui->timer);
        ui->timer = 0;
    }
#ifdef MAINUI_ONION
    /* The loop has already submitted the launch blank. Stop every SDL caller before
     * releasing the device video/audio drivers, then skip unrelated memory
     * cleanup. Host and snapshot runs retain full cleanup for leak checking. */
    if (ui->launch_pending && !ui->snapshot && ui->status == 0) {
        if (ui->preview.thread) {
            SDL_WaitThread(ui->preview.thread, NULL);
            ui->preview.thread = NULL;
        }
        mainui_audio_close();
        quit_sdl(ui);
        mainui_mark(MAINUI_MARK_EXIT);
        mainui_timing_report();
        fflush(stdout);
        fflush(stderr);
        _Exit(0);
    }
#endif
    for (int i = 0; i < 20; i++) {
        if (ui->highlighted[i]) {
            SDL_FreeSurface(ui->highlighted[i]);
        }
        if (ui->labels[i]) {
            SDL_FreeSurface(ui->labels[i]);
        }
    }
    if (ui->heading) {
        SDL_FreeSurface(ui->heading);
    }
    if (ui->library) {
        mainui_favorite_editor_close(&ui->favorite_editor);
        mainui_library_close(ui->library);
    }
    free(ui->library);
    if (ui->favorites) {
        mainui_library_close(ui->favorites);
    }
    free(ui->favorites);
    if (ui->apps) {
        mainui_catalog_close(ui->apps);
    }
    free(ui->apps);
    cJSON_Delete(ui->context_record);
    mainui_name_input_close(&ui->name_input);
    mainui_favorite_editor_close(&ui->favorite_editor);
    mainui_languages_close(&ui->languages);
    mainui_language_close();
    mainui_details_close(&ui->details);
    mainui_media_close(&ui->media);
    mainui_gamelist_metadata_close();
    mainui_preview_close(&ui->preview);
    mainui_menu_view_close(&ui->menu_view);
    mainui_audio_close();
    mainui_theme_close(&ui->theme);
    if (ui->screen != ui->display) {
        SDL_FreeSurface(ui->screen);
    }
    TTF_Quit();
    quit_sdl(ui);
    if (ui->catalog) {
        mainui_catalog_close(ui->catalog);
    }
    if (ui->expert && ui->expert != ui->catalog) {
        mainui_catalog_close(ui->expert);
    }
    if (ui->games && ui->games != ui->catalog) {
        mainui_catalog_close(ui->games);
    }
    free(ui->expert);
    free(ui->games);
    mainui_close_list(ui->list);
    free(ui->list);
    mainui_mark(MAINUI_MARK_EXIT);
    mainui_timing_report();
    return ui->status;
}
