#pragma once
#include "types.h"
#include "c128.h"
#include <SDL3/SDL.h>
#include <stdbool.h>

typedef struct Monitor Monitor;

Monitor     *monitor_create(C128 *c);
void         monitor_destroy(Monitor *mon);
void         monitor_open(Monitor *mon);
bool         monitor_is_open(const Monitor *mon);
bool         monitor_handle_event(Monitor *mon, SDL_Event *e);
void         monitor_render(Monitor *mon);
SDL_WindowID monitor_window_id(const Monitor *mon);
