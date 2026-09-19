#pragma once
#include <stdbool.h>
#include <stdint.h>

#define CONFIG_PATH_MAX  512
#define CONFIG_NAME      "1986.conf"

/* C128DCR model selection */
typedef enum {
    C128_MODEL_DCR = 0,   /* the flat C128DCR with integrated 1571 */
    C128_MODEL_C128 = 1,  /* the older, taller C128 */
    C128_MODEL_C128D = 2  /* the C128D (DCR's plastic sibling) */
} C128Model;

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
    int        gif_width;          /* F6 GIF capture width */
    int        gif_fps;            /* F6 GIF capture fps */
    bool       gif_ffmpeg;         /* optimize GIF via ffmpeg if present */
    char       rom_dir[CONFIG_PATH_MAX];  /* directory holding machine ROMs */
} Config;

void config_set_defaults(Config *cfg);
bool config_load(Config *cfg, const char *path);   /* returns false if missing */
bool config_save(const Config *cfg, const char *path);
