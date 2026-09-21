#pragma once
#include <stdbool.h>
#include <SDL3/SDL.h>
#include "config.h"
#include "c128.h"

typedef enum {
    OV_GENERAL = 0,
    OV_MEDIA,
    OV_ADVANCED,   /* shown only when Config.tinker is enabled */
    OV_SECTION_COUNT
} OvSection;

/* Pending native file-dialog request. */
typedef enum {
    OV_DIALOG_NONE = 0,
    OV_DIALOG_DISK,   /* .d64, .d71, or .d81 */
    OV_DIALOG_DISK2,  /* second IEC drive image */
    OV_DIALOG_TAPE,   /* .tap */
    OV_DIALOG_CART,   /* .crt */
    OV_DIALOG_ROM,    /* ROM directory (folder picker) */
} OvDialogKind;

typedef struct {
    bool         visible;
    bool         about_visible;
    OvSection    section;
    int          row;
    Config      *cfg;
    C128        *c128;

    /* Pending native file-dialog result (set by the SDL dialog callback). */
    OvDialogKind dialog_kind;
    bool         dialog_ready;
    bool         dialog_failed;
    char         dialog_path[CONFIG_PATH_MAX];
    char         dialog_error[256];
} Overlay;

void overlay_init(Overlay *ov, Config *cfg, C128 *c128);
void overlay_quit(Overlay *ov);

/* Returns true if the event was consumed by the overlay. */
bool overlay_handle_event(Overlay *ov, SDL_Event *ev);

/* Draw the overlay on top of the current renderer frame (before display_flip). */
void overlay_render(const Overlay *ov, SDL_Renderer *r);

/* Call once per frame to process any pending file-dialog result. */
void overlay_tick(Overlay *ov);

bool overlay_is_visible(const Overlay *ov);
