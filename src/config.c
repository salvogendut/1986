#include "compat_win.h"
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
    cfg->col_mode_80 = true;
    cfg->vdc_ram_kb = 64;
    cfg->gif_width = 320;
    cfg->gif_fps = 25;
    cfg->gif_ffmpeg = false;
    cfg->rom_dir[0] = '\0';
    cfg->disk_path[0] = '\0';
    cfg->disk2_path[0] = '\0';
    cfg->tape_path[0] = '\0';
    cfg->cart_path[0] = '\0';
    cfg->u36_path[0] = '\0';
    cfg->drive_unit = 8;
    cfg->drive2_unit = 9;
    cfg->drive_type = 1571;   /* Commodore 1571 */
    cfg->drive2_type = 1571;
    cfg->real_disk_drive = false;
    cfg->second_drive = false;
    cfg->tinker = false;
    cfg->one_display = false;
    cfg->display_change_reset = false;
    cfg->notify_mode = NOTIFY_MODE_SCREEN;
    cfg->tape_audio_monitor = false;
    cfg->tape_video_monitor = false;
    cfg->debug_overlay = false;
    cfg->joystick_hidapi = false;
    cfg->main_input_port = 2;
    cfg->joy_port_mode[0] = JOYPORT_JOYSTICK;
    cfg->joy_port_mode[1] = JOYPORT_JOYSTICK;
}

void config_normalize_drive_units(Config *cfg) {
    if (cfg->drive_unit < 8 || cfg->drive_unit > 11) cfg->drive_unit = 8;
    if (cfg->drive2_unit < 8 || cfg->drive2_unit > 11 ||
        cfg->drive2_unit == cfg->drive_unit)
        cfg->drive2_unit = cfg->drive_unit == 8 ? 9 : 8;
}

/* Resolve the config file location: $HOME/.config/1986/1986.conf, falling
 * back to a relative "1986.conf" if HOME is unset. Creates the directory. */
void config_path(char *out, size_t sz) {
    /* Isolated test runs (and portable installations) can select an explicit
     * config file without redirecting the entire process home directory. */
    const char *override = getenv("C128_CONFIG_PATH");
    if (override && *override) {
        snprintf(out, sz, "%s", override);
        return;
    }
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
    else if (!strcasecmp(key, "display_columns")) cfg->col_mode_80 = atoi(value) != 40;
    else if (!strcasecmp(key, "vdc_ram_kb")) cfg->vdc_ram_kb = atoi(value);
    else if (!strcasecmp(key, "gif_width"))     cfg->gif_width = atoi(value);
    else if (!strcasecmp(key, "gif_fps"))       cfg->gif_fps = atoi(value);
    else if (!strcasecmp(key, "gif_ffmpeg"))    cfg->gif_ffmpeg = atoi(value) != 0;
    else if (!strcasecmp(key, "rom_dir")) {
        snprintf(cfg->rom_dir, sizeof(cfg->rom_dir), "%s", value);
    }
    else if (!strcasecmp(key, "disk")) {
        snprintf(cfg->disk_path, sizeof(cfg->disk_path), "%s", value);
    }
    else if (!strcasecmp(key, "disk2")) {
        snprintf(cfg->disk2_path, sizeof(cfg->disk2_path), "%s", value);
    }
    else if (!strcasecmp(key, "tape")) {
        snprintf(cfg->tape_path, sizeof(cfg->tape_path), "%s", value);
    }
    else if (!strcasecmp(key, "cart")) {
        snprintf(cfg->cart_path, sizeof(cfg->cart_path), "%s", value);
    }
    else if (!strcasecmp(key, "u36")) {
        snprintf(cfg->u36_path, sizeof(cfg->u36_path), "%s", value);
    }
    else if (!strcasecmp(key, "last_disk_dir")) {
        snprintf(cfg->last_disk_dir, sizeof(cfg->last_disk_dir), "%s", value);
    }
    else if (!strcasecmp(key, "last_disk2_dir")) {
        snprintf(cfg->last_disk2_dir, sizeof(cfg->last_disk2_dir), "%s", value);
    }
    else if (!strcasecmp(key, "last_tape_dir")) {
        snprintf(cfg->last_tape_dir, sizeof(cfg->last_tape_dir), "%s", value);
    }
    else if (!strcasecmp(key, "last_cart_dir")) {
        snprintf(cfg->last_cart_dir, sizeof(cfg->last_cart_dir), "%s", value);
    }
    else if (!strcasecmp(key, "last_u36_dir")) {
        snprintf(cfg->last_u36_dir, sizeof(cfg->last_u36_dir), "%s", value);
    }
    else if (!strcasecmp(key, "drive_unit")) cfg->drive_unit = atoi(value);
    else if (!strcasecmp(key, "drive2_unit")) cfg->drive2_unit = atoi(value);
    else if (!strcasecmp(key, "drive_type")) cfg->drive_type = atoi(value);
    else if (!strcasecmp(key, "drive2_type")) cfg->drive2_type = atoi(value);
    else if (!strcasecmp(key, "real_disk_drive")) cfg->real_disk_drive = atoi(value) != 0;
    else if (!strcasecmp(key, "second_drive")) cfg->second_drive = atoi(value) != 0;
    else if (!strcasecmp(key, "tinker"))      cfg->tinker = atoi(value) != 0;
    else if (!strcasecmp(key, "one_display")) cfg->one_display = atoi(value) != 0;
    else if (!strcasecmp(key, "display_change_reset")) cfg->display_change_reset = atoi(value) != 0;
    else if (!strcasecmp(key, "notify_mode")) cfg->notify_mode = (NotifyMode)atoi(value);
    else if (!strcasecmp(key, "tape_audio_monitor")) cfg->tape_audio_monitor = atoi(value) != 0;
    else if (!strcasecmp(key, "tape_video_monitor")) cfg->tape_video_monitor = atoi(value) != 0;
    else if (!strcasecmp(key, "debug_overlay"))      cfg->debug_overlay = atoi(value) != 0;
    else if (!strcasecmp(key, "joystick_hidapi"))    cfg->joystick_hidapi = atoi(value) != 0;
    else if (!strcasecmp(key, "main_input_port"))    cfg->main_input_port = atoi(value);
    else if (!strcasecmp(key, "joy_port_1_mode"))    cfg->joy_port_mode[0] = (JoyPortMode)atoi(value);
    else if (!strcasecmp(key, "joy_port_2_mode"))    cfg->joy_port_mode[1] = (JoyPortMode)atoi(value);
}

bool config_load(Config *cfg, const char *path) {
    config_set_defaults(cfg);
    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[600];
    while (fgets(line, sizeof(line), f)) parse_line(cfg, line);
    fclose(f);
    config_normalize_drive_units(cfg);
    if (cfg->drive_type != 1571 && cfg->drive_type != 1581) cfg->drive_type = 1571;
    if (cfg->drive2_type != 1571 && cfg->drive2_type != 1581) cfg->drive2_type = 1571;
    if (cfg->vdc_ram_kb != 16 && cfg->vdc_ram_kb != 64) cfg->vdc_ram_kb = 64;
    if (cfg->main_input_port != 1 && cfg->main_input_port != 2) cfg->main_input_port = 2;
    for (int i = 0; i < 2; ++i)
        if (cfg->joy_port_mode[i] != JOYPORT_JOYSTICK &&
            cfg->joy_port_mode[i] != JOYPORT_MOUSE)
            cfg->joy_port_mode[i] = JOYPORT_JOYSTICK;
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
    fprintf(f, "display_columns = %d\n", cfg->col_mode_80 ? 80 : 40);
    fprintf(f, "vdc_ram_kb = %d\n", cfg->vdc_ram_kb);
    fprintf(f, "gif_width = %d\n", cfg->gif_width);
    fprintf(f, "gif_fps = %d\n", cfg->gif_fps);
    fprintf(f, "gif_ffmpeg = %d\n", cfg->gif_ffmpeg ? 1 : 0);
    fprintf(f, "rom_dir = %s\n", cfg->rom_dir);
    fprintf(f, "disk = %s\n", cfg->disk_path);
    fprintf(f, "disk2 = %s\n", cfg->disk2_path);
    fprintf(f, "tape = %s\n", cfg->tape_path);
    fprintf(f, "cart = %s\n", cfg->cart_path);
    fprintf(f, "u36 = %s\n", cfg->u36_path);
    fprintf(f, "last_disk_dir = %s\n", cfg->last_disk_dir);
    fprintf(f, "last_disk2_dir = %s\n", cfg->last_disk2_dir);
    fprintf(f, "last_tape_dir = %s\n", cfg->last_tape_dir);
    fprintf(f, "last_cart_dir = %s\n", cfg->last_cart_dir);
    fprintf(f, "last_u36_dir = %s\n", cfg->last_u36_dir);
    fprintf(f, "drive_unit = %d\n", cfg->drive_unit);
    fprintf(f, "drive2_unit = %d\n", cfg->drive2_unit);
    fprintf(f, "drive_type = %d\n", cfg->drive_type);
    fprintf(f, "drive2_type = %d\n", cfg->drive2_type);
    fprintf(f, "real_disk_drive = %d\n", cfg->real_disk_drive ? 1 : 0);
    fprintf(f, "second_drive = %d\n", cfg->second_drive ? 1 : 0);
    fprintf(f, "tinker = %d\n", cfg->tinker ? 1 : 0);
    fprintf(f, "one_display = %d\n", cfg->one_display ? 1 : 0);
    fprintf(f, "display_change_reset = %d\n", cfg->display_change_reset ? 1 : 0);
    fprintf(f, "notify_mode = %d\n", (int)cfg->notify_mode);
    fprintf(f, "tape_audio_monitor = %d\n", cfg->tape_audio_monitor ? 1 : 0);
    fprintf(f, "tape_video_monitor = %d\n", cfg->tape_video_monitor ? 1 : 0);
    fprintf(f, "debug_overlay = %d\n", cfg->debug_overlay ? 1 : 0);
    fprintf(f, "joystick_hidapi = %d\n", cfg->joystick_hidapi ? 1 : 0);
    fprintf(f, "main_input_port = %d\n", cfg->main_input_port);
    fprintf(f, "joy_port_1_mode = %d\n", cfg->joy_port_mode[0]);
    fprintf(f, "joy_port_2_mode = %d\n", cfg->joy_port_mode[1]);
    fclose(f);
    return true;
}

bool config_save_column_mode(const char *path, bool col_mode_80) {
    Config stored;
    config_load(&stored, path); /* also installs defaults when path is absent */
    stored.col_mode_80 = col_mode_80;
    return config_save(&stored, path);
}

bool config_save_input_port(const char *path, int port) {
    if (port != 1 && port != 2) return false;
    Config stored;
    config_load(&stored, path);
    stored.main_input_port = port;
    return config_save(&stored, path);
}
