#pragma once
#include <stdbool.h>
#include <SDL3/SDL.h>
#include "config.h"
#include "c128.h"

typedef enum {
    OV_GENERAL = 0,
    OV_VIDEO,
    OV_CAPTURE,
    OV_SECTION_COUNT
} OvSection;

typedef enum {
    OV_STATE_MENU    = 0,
    OV_STATE_CONFIRM = 1   /* "save changes?" prompt */
} OvState;

typedef struct {
    bool      visible;
    OvSection section;
    int       row;
    OvState   state;
    bool      dirty;          /* unsaved changes */
    Config   *cfg;
    Config    saved;          /* snapshot taken when the overlay opens */
    C128     *c128;
} Overlay;

void overlay_init(Overlay *ov, Config *cfg, C128 *c128);
void overlay_quit(Overlay *ov);

/* Returns true if the event was consumed by the overlay. */
bool overlay_handle_event(Overlay *ov, SDL_Event *ev);

/* Draw the overlay on top of the current renderer frame (before display_flip). */
void overlay_render(const Overlay *ov, SDL_Renderer *r);

/* Call once per frame; currently only drives the confirm sub-state timing. */
void overlay_tick(Overlay *ov);

bool overlay_is_visible(const Overlay *ov);
