#include "overlay.h"
#include <string.h>
#include <stdio.h>
#include <stddef.h>

#define OV_SCALE      1.5f
#define OV_LINE_H     20
#define OV_VALUE_X    140

#define MEDIA_ITEM_COUNT 3

static void overlay_file_callback(void *userdata, const char * const *files,
                                  int filter);

/* Persist the current config and hide the overlay. */
static void overlay_close(Overlay *ov) {
    char path[CONFIG_PATH_MAX];
    config_path(path, sizeof(path));
    config_save(ov->cfg, path);
    ov->visible = false;
}

static const char *const MODELS[] = { "C128DCR", "C128", "C128D" };

static const char *media_label(int row) {
    static const char *const labels[MEDIA_ITEM_COUNT] = {
        "Disk Drive", "Tape", "Cartridge"
    };
    return labels[row];
}

static const char *media_extension(int row) {
    static const char *const exts[MEDIA_ITEM_COUNT] = {
        ".d64", ".tap", ".crt"
    };
    return exts[row];
}

/* The selected file path (or NULL) for a Media row. */
static const char *media_path(const Overlay *ov, int row) {
    if (row == 0) return ov->cfg->disk_path;
    if (row == 1) return ov->cfg->tape_path;
    return ov->cfg->cart_path;
}

static void open_media_dialog(Overlay *ov, int row) {
    static const SDL_DialogFileFilter disk_filters[] = {
        { "D64 disk images", "d64;D64" },
        { "All files",       "*"       },
    };
    static const SDL_DialogFileFilter tape_filters[] = {
        { "TAP tapes", "tap;TAP" },
        { "All files", "*"       },
    };
    static const SDL_DialogFileFilter cart_filters[] = {
        { "CRT cartridges", "crt;CRT" },
        { "All files",      "*"       },
    };
    const SDL_DialogFileFilter *filters = disk_filters;
    if (row == 0) {
        ov->dialog_kind = OV_DIALOG_DISK;
        filters = disk_filters;
    } else if (row == 1) {
        ov->dialog_kind = OV_DIALOG_TAPE;
        filters = tape_filters;
    } else {
        ov->dialog_kind = OV_DIALOG_CART;
        filters = cart_filters;
    }
    ov->dialog_ready = false;
    ov->dialog_failed = false;
    ov->dialog_error[0] = '\0';
    SDL_ShowOpenFileDialog(overlay_file_callback, ov,
                           ov->c128 ? ov->c128->display.window : NULL,
                           filters, 2, NULL, false);
}

/* Folder picker for the ROM directory (General section). */
static void open_rom_dialog(Overlay *ov) {
    ov->dialog_kind   = OV_DIALOG_ROM;
    ov->dialog_ready  = false;
    ov->dialog_failed = false;
    ov->dialog_error[0] = '\0';
    SDL_ShowOpenFolderDialog(overlay_file_callback, ov,
                             ov->c128 ? ov->c128->display.window : NULL,
                             ov->cfg->rom_dir[0] ? ov->cfg->rom_dir : NULL,
                             false);
}

void overlay_init(Overlay *ov, Config *cfg, C128 *c128) {
    memset(ov, 0, sizeof(*ov));
    ov->cfg  = cfg;
    ov->c128 = c128;
    ov->dialog_kind = OV_DIALOG_NONE;
}

void overlay_quit(Overlay *ov) {
    (void)ov;
}

bool overlay_is_visible(const Overlay *ov) {
    return ov->visible;
}

bool overlay_handle_event(Overlay *ov, SDL_Event *ev) {
    if (ev->type != SDL_EVENT_KEY_DOWN)
        return ov->visible;   /* consume everything while open */

    /* Ignore auto-repeat: holding F9 (or an arrow key) would otherwise toggle
     * the overlay on and off (or jump rows/sections) several times. */
    if (ev->key.repeat)
        return ov->visible;

    SDL_Scancode sc = ev->key.scancode;

    /* F9 always toggles the overlay. */
    if (sc == SDL_SCANCODE_F9) {
        if (!ov->visible) {
            ov->visible = true;
            ov->section = OV_GENERAL;
            ov->row     = 0;
        } else {
            overlay_close(ov);
        }
        return true;
    }

    if (!ov->visible) return false;

    switch (sc) {
        case SDL_SCANCODE_LEFT:
        case SDL_SCANCODE_RIGHT: {
            int dir = (sc == SDL_SCANCODE_RIGHT) ? 1 : -1;
            ov->section = (OvSection)((ov->section + dir + OV_SECTION_COUNT)
                                      % OV_SECTION_COUNT);
            ov->row = 0;
            break;
        }
        case SDL_SCANCODE_UP:
            if (ov->row > 0) ov->row--;
            break;
        case SDL_SCANCODE_DOWN:
            if (ov->row < MEDIA_ITEM_COUNT - 1) ov->row++;
            break;
        case SDL_SCANCODE_RETURN:
            if (ov->section == OV_MEDIA)
                open_media_dialog(ov, ov->row);
            else
                open_rom_dialog(ov);
            break;
        case SDL_SCANCODE_ESCAPE:
            overlay_close(ov);
            break;
        default:
            break;
    }
    return true;
}

void overlay_tick(Overlay *ov) {
    if (ov->dialog_failed) {
        SDL_MemoryBarrierAcquire();
        ov->dialog_failed = false;
        ov->dialog_kind   = OV_DIALOG_NONE;
        fprintf(stderr, "1986: file picker unavailable: %s\n",
                ov->dialog_error[0] ? ov->dialog_error : "unknown SDL error");
        return;
    }
    if (!ov->dialog_ready) return;

    SDL_MemoryBarrierAcquire();
    ov->dialog_ready = false;
    OvDialogKind kind = ov->dialog_kind;
    ov->dialog_kind = OV_DIALOG_NONE;

    char *dest = NULL;
    if (kind == OV_DIALOG_DISK)      dest = ov->cfg->disk_path;
    else if (kind == OV_DIALOG_TAPE) dest = ov->cfg->tape_path;
    else if (kind == OV_DIALOG_CART) dest = ov->cfg->cart_path;
    else if (kind == OV_DIALOG_ROM)  dest = ov->cfg->rom_dir;
    if (dest) {
        snprintf(dest, CONFIG_PATH_MAX, "%s", ov->dialog_path);
        char path[CONFIG_PATH_MAX];
        config_path(path, sizeof(path));
        config_save(ov->cfg, path);
    }
}

static void overlay_file_callback(void *userdata, const char * const *files,
                                  int filter) {
    (void)filter;
    Overlay *ov = userdata;
    if (!files) {
        snprintf(ov->dialog_error, sizeof(ov->dialog_error), "%s",
                 SDL_GetError());
        SDL_MemoryBarrierRelease();
        ov->dialog_failed = true;
    } else if (files[0]) {
        snprintf(ov->dialog_path, sizeof(ov->dialog_path), "%s", files[0]);
        SDL_MemoryBarrierRelease();
        ov->dialog_ready = true;
    } else {
        ov->dialog_kind = OV_DIALOG_NONE;
    }
}

/* Draw one "label  value" row. */
static void draw_row(SDL_Renderer *r, int lw, float y,
                     const char *label, const char *value, bool highlight) {
    if (highlight) {
        SDL_SetRenderDrawColor(r, 0x80, 0x60, 0x20, 255);
        SDL_FRect hl = { 10, y, (float)lw - 20, OV_LINE_H - 4 };
        SDL_RenderFillRect(r, &hl);
    }
    SDL_SetRenderDrawColor(r, 0xFF, 0xFF, 0xFF, 255);
    SDL_RenderDebugText(r, 20, y, label);
    if (value)
        SDL_RenderDebugText(r, (float)OV_VALUE_X, y, value);
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
    static const char *const names[OV_SECTION_COUNT] = { "General", "Media" };
    float tx = 20;
    for (int s = 0; s < OV_SECTION_COUNT; s++) {
        bool active = (s == (int)ov->section);
        SDL_SetRenderDrawColor(r, active ? 0xFF : 0xC0,
                               active ? 0xFF : 0xC0,
                               active ? 0xFF : 0xC0, 255);
        SDL_RenderDebugText(r, tx, 14, names[s]);
        tx += (float)((int)strlen(names[s]) * 8 + 20);
    }

    float y = 48;

    if (ov->section == OV_GENERAL) {
        char machine[64];
        const char *model = (ov->cfg->model >= 0 && ov->cfg->model < 3)
                          ? MODELS[ov->cfg->model] : "C128";
        snprintf(machine, sizeof(machine), "Commodore %s", model);

        draw_row(r, lw, y, "Machine", machine, false); y += OV_LINE_H;
        draw_row(r, lw, y, "CPU", "MOS 8502 @ 2 MHz + Z80A (CP/M)", false);
        y += OV_LINE_H;
        draw_row(r, lw, y, "Memory", "128 KB", false); y += OV_LINE_H;
        draw_row(r, lw, y, "Video", "VIC-IIe (40-col) + 8563 VDC (80-col)", false);
        y += OV_LINE_H;
        draw_row(r, lw, y, "Sound", "SID 6581", false); y += OV_LINE_H;
#ifdef PACKAGE_VERSION
        draw_row(r, lw, y, "Emulator", PACKAGE_VERSION, false); y += OV_LINE_H;
#endif
        y += OV_LINE_H;
        const char *rd = ov->cfg->rom_dir[0] ? ov->cfg->rom_dir
                                              : "(executable directory)";
        draw_row(r, lw, y, "ROMs", rd, true);
    } else {
        for (int i = 0; i < MEDIA_ITEM_COUNT; i++) {
            const char *path = media_path(ov, i);
            char vbuf[CONFIG_PATH_MAX + 8];
            if (path && path[0])
                snprintf(vbuf, sizeof(vbuf), "%s (%s)", media_extension(i), path);
            else
                snprintf(vbuf, sizeof(vbuf), "<none> (%s)", media_extension(i));
            draw_row(r, lw, y, media_label(i), vbuf, i == ov->row);
            y += OV_LINE_H;
        }
    }

    /* Footer. */
    SDL_SetRenderDrawColor(r, 0xAA, 0xAA, 0xAA, 255);
    const char *footer = "Left/Right section  Up/Down select  Enter choose file  F9/Esc close";
    SDL_RenderDebugText(r, (float)((lw - (int)strlen(footer) * 8) / 2),
                        (float)(lh - 20), footer);

    SDL_SetRenderScale(r, 1.0f, 1.0f);
}
