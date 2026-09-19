#include "overlay.h"
#include <string.h>
#include <stdio.h>
#include <stddef.h>

#define OV_SCALE      1.5f
#define OV_LINE_H     20

typedef enum { OVT_BOOL, OVT_INT, OVT_ENUM } OvType;

typedef struct {
    const char *label;
    OvType      type;
    size_t      offset;   /* offsetof(Config, field) */
    int         min, max; /* OVT_INT range */
} OvItem;

/* Each section is a fixed list; NULL label ends the list. */
static const OvItem SECTIONS[OV_SECTION_COUNT][8] = {
    [OV_GENERAL] = {
        { "Fullscreen",        OVT_BOOL, offsetof(Config, fullscreen), 0, 0 },
        { "Smoothing",         OVT_BOOL, offsetof(Config, smoothing),  0, 0 },
        { "Fast mode (2 MHz)", OVT_BOOL, offsetof(Config, fast),       0, 0 },
        { "Model",             OVT_ENUM, offsetof(Config, model),      0, 2 },
        { "Scale",             OVT_INT,  offsetof(Config, scale),      1, 4 },
        { "Reset to defaults", OVT_ENUM, (size_t)-1, 0, 0 },  /* action */
        { NULL, 0, 0, 0, 0 },
    },
    [OV_VIDEO] = {
        { "CRT effect",        OVT_BOOL, offsetof(Config, crt_enabled),    0, 0 },
        { "Scanlines",         OVT_INT,  offsetof(Config, crt_scanlines),  0, 95 },
        { "Brightness",        OVT_INT,  offsetof(Config, crt_brightness), 50, 100 },
        { "Contrast",          OVT_INT,  offsetof(Config, crt_contrast),   50, 150 },
        { NULL, 0, 0, 0, 0 },
    },
    [OV_CAPTURE] = {
        { "GIF width",         OVT_INT, offsetof(Config, gif_width), 160, 640 },
        { "GIF fps",           OVT_INT, offsetof(Config, gif_fps),   5, 50 },
        { "Optimize with ffmpeg", OVT_BOOL, offsetof(Config, gif_ffmpeg), 0, 0 },
        { "Save & close",      OVT_ENUM, (size_t)-1, 0, 0 },  /* action */
        { NULL, 0, 0, 0, 0 },
    },
};

static const char *section_name(OvSection s) {
    static const char *names[OV_SECTION_COUNT] = {
        "General", "Video", "Capture"
    };
    return names[s];
}

static const OvItem *items_of(OvSection s) {
    return SECTIONS[s];
}

static const char *item_value(const Config *cfg, const OvItem *it, char *buf,
                              size_t bufsz) {
    if (it->type == OVT_BOOL) {
        bool *p = (bool *)((char *)cfg + it->offset);
        return *p ? "on" : "off";
    }
    if (it->type == OVT_INT) {
        int *p = (int *)((char *)cfg + it->offset);
        snprintf(buf, bufsz, "%d", *p);
        return buf;
    }
    /* OVT_ENUM */
    int *p = (int *)((char *)cfg + it->offset);
    static const char *models[] = { "C128DCR", "C128", "C128D" };
    if (it->offset == (size_t)-1) return "action";
    if (*p >= 0 && *p < 3) return models[*p];
    return "?";
}

static void item_adjust(Config *cfg, const OvItem *it, int delta) {
    if (it->type == OVT_BOOL) {
        bool *p = (bool *)((char *)cfg + it->offset);
        *p = !*p;
    } else if (it->type == OVT_INT) {
        int *p = (int *)((char *)cfg + it->offset);
        *p += delta;
        if (*p < it->min) *p = it->min;
        if (*p > it->max) *p = it->max;
    } else if (it->type == OVT_ENUM) {
        int *p = (int *)((char *)cfg + it->offset);
        *p = (*p + 1) % 3;
    }
}

void overlay_init(Overlay *ov, Config *cfg, C128 *c128) {
    memset(ov, 0, sizeof(*ov));
    ov->cfg = cfg;
    ov->c128 = c128;
}

void overlay_quit(Overlay *ov) {
    (void)ov;
}

bool overlay_is_visible(const Overlay *ov) {
    return ov->visible;
}

bool overlay_handle_event(Overlay *ov, SDL_Event *ev) {
    if (!ov->visible) return false;
    if (ev->type != SDL_EVENT_KEY_DOWN) return true; /* consume everything while open */

    const OvItem *items = items_of(ov->section);
    int count = 0;
    while (items[count].label) count++;

    switch (ev->key.scancode) {
        case SDL_SCANCODE_LEFT:
        case SDL_SCANCODE_RIGHT: {
            int dir = (ev->key.scancode == SDL_SCANCODE_RIGHT) ? 1 : -1;
            ov->section = (OvSection)((ov->section + dir + OV_SECTION_COUNT) % OV_SECTION_COUNT);
            ov->row = 0;
            break;
        }
        case SDL_SCANCODE_UP:   if (ov->row > 0) ov->row--; break;
        case SDL_SCANCODE_DOWN: if (ov->row < count - 1) ov->row++; break;
        case SDL_SCANCODE_RETURN: {
            const OvItem *it = &items[ov->row];
            if (it->offset == (size_t)-1) {
                if (!strcmp(it->label, "Reset to defaults")) {
                    config_set_defaults(ov->cfg);
                    ov->dirty = true;
                } else if (!strcmp(it->label, "Save & close")) {
                    config_save(ov->cfg, CONFIG_NAME);
                    ov->visible = false;
                }
            } else {
                item_adjust(ov->cfg, it, 1);
                ov->dirty = true;
            }
            break;
        }
        case SDL_SCANCODE_F9:
            /* F9 saves-and-closes. */
            config_save(ov->cfg, CONFIG_NAME);
            ov->visible = false;
            break;
        case SDL_SCANCODE_ESCAPE:
            if (ov->dirty) {
                ov->state = OV_STATE_CONFIRM;
            } else {
                ov->visible = false;
            }
            break;
        default:
            break;
    }

    if (ov->state == OV_STATE_CONFIRM) {
        if (ev->key.scancode == SDL_SCANCODE_Y || ev->key.scancode == SDL_SCANCODE_RETURN) {
            *ov->cfg = ov->saved;      /* discard */
            ov->visible = false;
            ov->state = OV_STATE_MENU;
        } else if (ev->key.scancode == SDL_SCANCODE_N || ev->key.scancode == SDL_SCANCODE_ESCAPE) {
            ov->state = OV_STATE_MENU;
        }
    }

    return true;
}

void overlay_tick(Overlay *ov) {
    (void)ov;
}

void overlay_render(const Overlay *ov, SDL_Renderer *r) {
    if (!ov->visible) return;

    int ww, wh;
    SDL_GetWindowSize(ov->c128->display.window, &ww, &wh);

    SDL_SetRenderScale(r, OV_SCALE, OV_SCALE);
    int lw = (int)(ww / OV_SCALE);
    int lh = (int)(wh / OV_SCALE);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 200);
    SDL_FRect bg = { 0, 0, (float)lw, (float)lh };
    SDL_RenderFillRect(r, &bg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    /* Section tabs. */
    SDL_SetRenderDrawColor(r, 0x30, 0x40, 0x60, 255);
    SDL_FRect tabbar = { 10, 10, (float)lw - 20, 22 };
    SDL_RenderFillRect(r, &tabbar);
    SDL_SetRenderDrawColor(r, 0xFF, 0xFF, 0xFF, 255);
    float tx = 20;
    for (int s = 0; s < OV_SECTION_COUNT; s++) {
        const char *name = section_name((OvSection)s);
        int w = (int)(strlen(name) * 8);
        SDL_RenderDebugText(r, tx, 14, name);
        tx += w + 20;
    }

    /* Menu items. */
    const OvItem *items = items_of(ov->section);
    float y = 48;
    char vbuf[64];
    for (int i = 0; items[i].label; i++) {
        if (i == ov->row) {
            SDL_SetRenderDrawColor(r, 0x80, 0x60, 0x20, 255);
            SDL_FRect hl = { 10, y, (float)lw - 20, OV_LINE_H - 4 };
            SDL_RenderFillRect(r, &hl);
        }
        SDL_SetRenderDrawColor(r, 0xFF, 0xFF, 0xFF, 255);
        SDL_RenderDebugText(r, 20, y, items[i].label);
        const char *val = item_value(ov->cfg, &items[i], vbuf, sizeof(vbuf));
        SDL_RenderDebugText(r, (float)(lw - 20 - (int)strlen(val) * 8), y, val);
        y += OV_LINE_H;
    }

    /* Footer. */
    SDL_SetRenderDrawColor(r, 0xAA, 0xAA, 0xAA, 255);
    const char *footer = "Left/Right section  Up/Down select  Enter toggle  F9 save  Esc close";
    SDL_RenderDebugText(r, (float)((lw - (int)strlen(footer) * 8) / 2), (float)(lh - 20), footer);

    if (ov->state == OV_STATE_CONFIRM) {
        SDL_SetRenderDrawColor(r, 0, 0, 0, 200);
        SDL_FRect box = { (float)(lw / 2 - 120), (float)(lh / 2 - 20), 240, 40 };
        SDL_RenderFillRect(r, &box);
        SDL_SetRenderDrawColor(r, 0xFF, 0xFF, 0xFF, 255);
        const char *msg = "Discard changes? Y/N";
        SDL_RenderDebugText(r, (float)(lw / 2 - (int)strlen(msg) * 4), (float)(lh / 2 - 6), msg);
    }

    SDL_SetRenderScale(r, 1.0f, 1.0f);
}
