#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>

void config_set_defaults(Config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->scale = 2;
    cfg->fullscreen = false;
    cfg->smoothing = false;
    cfg->crt_enabled = false;
    cfg->crt_scanlines = 35;
    cfg->crt_brightness = 100;
    cfg->crt_contrast = 100;
    cfg->crt_red = 100;
    cfg->crt_green = 100;
    cfg->crt_blue = 100;
    cfg->model = C128_MODEL_DCR;
    cfg->fast = false;
    cfg->gif_width = 320;
    cfg->gif_fps = 25;
    cfg->gif_ffmpeg = false;
    cfg->rom_dir[0] = '\0';
    cfg->disk_path[0] = '\0';
    cfg->tape_path[0] = '\0';
    cfg->cart_path[0] = '\0';
    cfg->tinker = false;
    cfg->one_display = false;
    cfg->display_change_reset = false;
    cfg->notify_mode = NOTIFY_MODE_SCREEN;
    cfg->tape_audio_monitor = false;
    cfg->tape_video_monitor = false;
    cfg->debug_overlay = false;
    cfg->joystick_hidapi = false;
}

/* Resolve the config file location: $HOME/.config/1986/1986.conf, falling
 * back to a relative "1986.conf" if HOME is unset. Creates the directory. */
void config_path(char *out, size_t sz) {
    const char *home = getenv("HOME");
    if (home && *home) {
        char dir[CONFIG_PATH_MAX];
        snprintf(dir, sizeof(dir), "%s/.config", home);
        mkdir(dir, 0755);
        snprintf(dir, sizeof(dir), "%s/.config/1986", home);
        mkdir(dir, 0755);
        snprintf(out, sz, "%s/.config/1986/%s", home, CONFIG_NAME);
        return;
    }
    snprintf(out, sz, "%s", CONFIG_NAME);
}

/* Very small INI-style parser: one `key = value` per line, '#' comments. */
static void trim(char *s) {
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r')) s[--n] = '\0';
}

static void parse_line(Config *cfg, const char *line) {
    char key[64], value[512];
    if (line[0] == '#' || line[0] == ';' || line[0] == '\0') return;
    if (sscanf(line, "%63[^=] = %511[^\n\r]", key, value) != 2) return;
    trim(key);
    trim(value);

    if (!strcasecmp(key, "scale"))              cfg->scale = atoi(value);
    else if (!strcasecmp(key, "fullscreen"))    cfg->fullscreen = atoi(value) != 0;
    else if (!strcasecmp(key, "smoothing"))     cfg->smoothing = atoi(value) != 0;
    else if (!strcasecmp(key, "crt"))           cfg->crt_enabled = atoi(value) != 0;
    else if (!strcasecmp(key, "crt_scanlines")) cfg->crt_scanlines = atoi(value);
    else if (!strcasecmp(key, "crt_brightness"))cfg->crt_brightness = atoi(value);
    else if (!strcasecmp(key, "crt_contrast"))  cfg->crt_contrast = atoi(value);
    else if (!strcasecmp(key, "crt_red"))       cfg->crt_red = atoi(value);
    else if (!strcasecmp(key, "crt_green"))     cfg->crt_green = atoi(value);
    else if (!strcasecmp(key, "crt_blue"))      cfg->crt_blue = atoi(value);
    else if (!strcasecmp(key, "model"))         cfg->model = (C128Model)atoi(value);
    else if (!strcasecmp(key, "fast"))          cfg->fast = atoi(value) != 0;
    else if (!strcasecmp(key, "gif_width"))     cfg->gif_width = atoi(value);
    else if (!strcasecmp(key, "gif_fps"))       cfg->gif_fps = atoi(value);
    else if (!strcasecmp(key, "gif_ffmpeg"))    cfg->gif_ffmpeg = atoi(value) != 0;
    else if (!strcasecmp(key, "rom_dir")) {
        snprintf(cfg->rom_dir, sizeof(cfg->rom_dir), "%s", value);
    }
    else if (!strcasecmp(key, "disk")) {
        snprintf(cfg->disk_path, sizeof(cfg->disk_path), "%s", value);
    }
    else if (!strcasecmp(key, "tape")) {
        snprintf(cfg->tape_path, sizeof(cfg->tape_path), "%s", value);
    }
    else if (!strcasecmp(key, "cart")) {
        snprintf(cfg->cart_path, sizeof(cfg->cart_path), "%s", value);
    }
    else if (!strcasecmp(key, "tinker"))      cfg->tinker = atoi(value) != 0;
    else if (!strcasecmp(key, "one_display")) cfg->one_display = atoi(value) != 0;
    else if (!strcasecmp(key, "display_change_reset")) cfg->display_change_reset = atoi(value) != 0;
    else if (!strcasecmp(key, "notify_mode")) cfg->notify_mode = (NotifyMode)atoi(value);
    else if (!strcasecmp(key, "tape_audio_monitor")) cfg->tape_audio_monitor = atoi(value) != 0;
    else if (!strcasecmp(key, "tape_video_monitor")) cfg->tape_video_monitor = atoi(value) != 0;
    else if (!strcasecmp(key, "debug_overlay"))      cfg->debug_overlay = atoi(value) != 0;
    else if (!strcasecmp(key, "joystick_hidapi"))    cfg->joystick_hidapi = atoi(value) != 0;
}

bool config_load(Config *cfg, const char *path) {
    config_set_defaults(cfg);
    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[600];
    while (fgets(line, sizeof(line), f)) parse_line(cfg, line);
    fclose(f);
    return true;
}

bool config_save(const Config *cfg, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "# 1986 configuration\n");
    fprintf(f, "# This file is written by the options overlay (F9 -> save).\n\n");
    fprintf(f, "scale = %d\n", cfg->scale);
    fprintf(f, "fullscreen = %d\n", cfg->fullscreen ? 1 : 0);
    fprintf(f, "smoothing = %d\n", cfg->smoothing ? 1 : 0);
    fprintf(f, "crt = %d\n", cfg->crt_enabled ? 1 : 0);
    fprintf(f, "crt_scanlines = %d\n", cfg->crt_scanlines);
    fprintf(f, "crt_brightness = %d\n", cfg->crt_brightness);
    fprintf(f, "crt_contrast = %d\n", cfg->crt_contrast);
    fprintf(f, "crt_red = %d\n", cfg->crt_red);
    fprintf(f, "crt_green = %d\n", cfg->crt_green);
    fprintf(f, "crt_blue = %d\n", cfg->crt_blue);
    fprintf(f, "model = %d\n", (int)cfg->model);
    fprintf(f, "fast = %d\n", cfg->fast ? 1 : 0);
    fprintf(f, "gif_width = %d\n", cfg->gif_width);
    fprintf(f, "gif_fps = %d\n", cfg->gif_fps);
    fprintf(f, "gif_ffmpeg = %d\n", cfg->gif_ffmpeg ? 1 : 0);
    fprintf(f, "rom_dir = %s\n", cfg->rom_dir);
    fprintf(f, "disk = %s\n", cfg->disk_path);
    fprintf(f, "tape = %s\n", cfg->tape_path);
    fprintf(f, "cart = %s\n", cfg->cart_path);
    fprintf(f, "tinker = %d\n", cfg->tinker ? 1 : 0);
    fprintf(f, "one_display = %d\n", cfg->one_display ? 1 : 0);
    fprintf(f, "display_change_reset = %d\n", cfg->display_change_reset ? 1 : 0);
    fprintf(f, "notify_mode = %d\n", (int)cfg->notify_mode);
    fprintf(f, "tape_audio_monitor = %d\n", cfg->tape_audio_monitor ? 1 : 0);
    fprintf(f, "tape_video_monitor = %d\n", cfg->tape_video_monitor ? 1 : 0);
    fprintf(f, "debug_overlay = %d\n", cfg->debug_overlay ? 1 : 0);
    fprintf(f, "joystick_hidapi = %d\n", cfg->joystick_hidapi ? 1 : 0);
    fclose(f);
    return true;
}
