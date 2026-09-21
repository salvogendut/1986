#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "notify.h"   /* NotifyMode */

#define CONFIG_PATH_MAX  512
#define CONFIG_NAME      "1986.conf"

/* C128DCR model selection */
typedef enum {
    C128_MODEL_DCR = 0,   /* the flat C128DCR with integrated 1571 */
    C128_MODEL_C128 = 1,  /* the older, taller C128 */
    C128_MODEL_C128D = 2  /* the C128D (DCR's plastic sibling) */
} C128Model;

typedef enum { JOYPORT_JOYSTICK = 0, JOYPORT_MOUSE = 1 } JoyPortMode;

typedef struct {
    int        scale;              /* window scale factor (1..4) */
    bool       fullscreen;
    bool       smoothing;          /* linear vs nearest texture scaling */
    bool       crt_enabled;
    int        crt_scanlines;
    int        crt_brightness;
    int        crt_contrast;
    int        crt_red;
    int        crt_green;
    int        crt_blue;
    C128Model  model;              /* which C128 variant to emulate */
    bool       fast;               /* run the 8502 at 2 MHz (C128 fast mode) */
    bool       col_mode_80;        /* latched 40/80 key: VDC vs VIC-II */
    int        gif_width;          /* F6 GIF capture width */
    int        gif_fps;            /* F6 GIF capture fps */
    bool       gif_ffmpeg;         /* optimize GIF via ffmpeg if present */
    char       rom_dir[CONFIG_PATH_MAX];  /* directory holding machine ROMs */

    /* Media files chosen in the overlay. Tape remains a placeholder. */
    char       disk_path[CONFIG_PATH_MAX];  /* D64/D71/D81 disk image */
    char       disk2_path[CONFIG_PATH_MAX]; /* second drive disk image */
    char       tape_path[CONFIG_PATH_MAX];  /* Tape .tap image */
    char       cart_path[CONFIG_PATH_MAX];  /* native C128 CRT/raw function ROM */
    char       u36_path[CONFIG_PATH_MAX];   /* internal function ROM socket */
    /* Per-picker directories survive ejection; empty uses selected media. */
    char       last_disk_dir[CONFIG_PATH_MAX];
    char       last_disk2_dir[CONFIG_PATH_MAX];
    char       last_tape_dir[CONFIG_PATH_MAX];
    char       last_cart_dir[CONFIG_PATH_MAX];
    char       last_u36_dir[CONFIG_PATH_MAX];

    /* Disk drive (Commodore 1571). */
    int        drive_unit;        /* IEC device number (8-11) */
    int        drive2_unit;       /* distinct IEC device number (8-11) */
    int        drive_type;        /* DRIVE_TYPE_* (1571) */
    bool       real_disk_drive;   /* future hardware backend preference */
    bool       second_drive;      /* expose the second virtual IEC drive */

    /* Tinker-gated Advanced overlay section. */
    bool       tinker;              /* enable the Advanced section */
    bool       one_display;         /* unified display (VIC/VDC share one window) */
    bool       display_change_reset;/* reset when switching 40<->80 display */
    NotifyMode notify_mode;         /* Notifications: off/screen/console */
    bool       tape_audio_monitor;  /* stub */
    bool       tape_video_monitor;  /* stub */
    bool       debug_overlay;       /* stub */
    bool       joystick_hidapi;     /* SDL HIDAPI backend (restart to apply) */
    int        main_input_port;     /* host gamepad/mouse targets port 1 or 2 */
    JoyPortMode joy_port_mode[2];   /* per-port joystick or 1351 mouse */
} Config;

void config_set_defaults(Config *cfg);
void config_normalize_drive_units(Config *cfg);
bool config_load(Config *cfg, const char *path);   /* returns false if missing */
bool config_save(const Config *cfg, const char *path);
/* Reload the on-disk config and update only the persistent display mode. */
bool config_save_column_mode(const char *path, bool col_mode_80);
/* Save the F1 host-port shortcut without overwriting other settings. */
bool config_save_input_port(const char *path, int port);

/* Resolve the config file location: $HOME/.config/1986/1986.conf (or a
 * relative "1986.conf" if HOME is unset). C128_CONFIG_PATH overrides this
 * for isolated/portable runs. Creates the default directory. */
void config_path(char *out, size_t sz);
