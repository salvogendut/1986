#include "overlay.h"
#include "notify.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#define OV_SCALE      1.5f
#define OV_LINE_H     20
#define OV_VALUE_X    140

#define MEDIA_ITEM_COUNT 4

/* Advanced section rows. */
#define ADV_SMOOTHING           0
#define ADV_REAL_CRT            1
#define ADV_CRT_SCANLINES       2
#define ADV_ONE_DISPLAY         3
#define ADV_DISPLAY_CHANGE_RESET 4
#define ADV_GIF_WIDTH           5
#define ADV_GIF_FPS             6
#define ADV_GIF_ENCODER         7
#define ADV_TAPE_AUDIO          8
#define ADV_TAPE_VIDEO          9
#define ADV_NOTIFICATIONS       10
#define ADV_DEBUG               11
#define ADV_JOY_HIDAPI          12
#define ADV_RESET               13
#define ADV_VERSION             14
#define ADV_ROWS                15

static int cycle_gif_width(int width) {
    switch (width) {
        case 384: return 320;
        case 320: return 160;
        case 160: return 640;
        default:  return 384;
    }
}

static int cycle_gif_fps(int fps) {
    switch (fps) {
        case 25: return 20;
        case 20: return 10;
        case 10: return 5;
        default: return 25;
    }
}

static void cycle_notify_mode(Config *cfg) {
    int m = (int)cfg->notify_mode + 1;
    if (m > NOTIFY_MODE_CONSOLE) m = NOTIFY_MODE_OFF;
    cfg->notify_mode = (NotifyMode)m;
    notify_set_mode(cfg->notify_mode);
}

static const char *notify_mode_name(NotifyMode m) {
    switch (m) {
        case NOTIFY_MODE_OFF:    return "off";
        case NOTIFY_MODE_SCREEN: return "screen";
        default:                 return "console";
    }
}

static void overlay_file_callback(void *userdata, const char * const *files,
                                  int filter);

static void save_config(const Overlay *ov) {
    char path[CONFIG_PATH_MAX];
    config_path(path, sizeof(path));
    config_save(ov->cfg, path);
}

/* Persist the current config and hide the overlay. */
static void overlay_close(Overlay *ov) {
    save_config(ov);
    ov->visible = false;
}

/* Replace the live disk as a physical eject followed by an insert.  The
 * configured path always describes the media that is actually attached; a
 * failed insert therefore leaves both the drive and the setting empty. */
static bool replace_disk_image(Overlay *ov, const char *path) {
    ov->cfg->disk_path[0] = '\0';

    if (drive_attach_disk(&ov->c128->drive, path) != 0) {
        fprintf(stderr, "1986: could not attach disk '%s'\n", path);
        notify_post("COULD NOT INSERT D64 DISK IMAGE");
        save_config(ov);
        return false;
    }

    if (path && path[0]) {
        snprintf(ov->cfg->disk_path, sizeof(ov->cfg->disk_path), "%s", path);
        notify_post("D64 DISK IMAGE INSERTED");
    } else {
        notify_post("D64 DISK IMAGE EJECTED");
    }
    save_config(ov);
    return true;
}

/* Apply the display-affecting config to the live window immediately, so
 * overlay changes take effect without a restart. */
static void apply_display(const Overlay *ov) {
    display_set_smoothing(&ov->c128->display, ov->cfg->smoothing);
    display_set_crt(&ov->c128->display, ov->cfg->crt_enabled,
                    ov->cfg->crt_scanlines, ov->cfg->crt_brightness,
                    ov->cfg->crt_contrast, ov->cfg->crt_red,
                    ov->cfg->crt_green, ov->cfg->crt_blue);
}

static const char *const MODELS[] = { "C128DCR", "C128", "C128D" };

static const char *media_label(int row) {
    static const char *const labels[MEDIA_ITEM_COUNT] = {
        "Disk Drive", "Disk image", "Tape", "Cartridge"
    };
    return labels[row];
}

static const char *media_extension(int row) {
    static const char *const exts[MEDIA_ITEM_COUNT] = {
        ".d64", ".d64", ".tap", ".crt"
    };
    return exts[row];
}

/* The selected file path (or NULL) for a Media row. */
static const char *media_path(const Overlay *ov, int row) {
    if (row == 0) return NULL;               /* Drive unit number (cycled) */
    if (row == 1) return ov->cfg->disk_path;
    if (row == 2) return ov->cfg->tape_path;
    return ov->cfg->cart_path;
}

/* Abbreviate the user's home directory as "~" to keep long paths short. */
static void abbrev_home(const char *path, char *out, size_t sz) {
    const char *home = getenv("HOME");
    if (home && *home && strncmp(path, home, strlen(home)) == 0)
        snprintf(out, sz, "~%s", path + strlen(home));
    else
        snprintf(out, sz, "%s", path);
}

/* The ROM directory currently in effect, for display: the configured path,
 * or the executable's "roms" subdirectory when none is set. */
static void rom_path_display(const Overlay *ov, char *out, size_t sz) {
    char path[CONFIG_PATH_MAX];
    if (ov->cfg->rom_dir[0]) {
        snprintf(path, sizeof(path), "%s", ov->cfg->rom_dir);
    } else {
        const char *base = SDL_GetBasePath();
        if (base)
            snprintf(path, sizeof(path), "%s/roms", base);
        else
            snprintf(path, sizeof(path), "roms");
    }
    abbrev_home(path, out, sz);
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
    if (row == 1) {
        ov->dialog_kind = OV_DIALOG_DISK;
        filters = disk_filters;
    } else if (row == 2) {
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

/* The Advanced section is only reachable while Tinker is on. */
static bool section_available(const Overlay *ov, OvSection s) {
    if (s == OV_ADVANCED) return ov->cfg->tinker;
    return true;
}

/* Number of selectable rows in each section. */
static int section_rows(const Overlay *ov, OvSection s) {
    (void)ov;
    switch (s) {
        case OV_GENERAL:  return 2;   /* Tinker, ROMS PATH */
        case OV_MEDIA:    return 4;   /* Disk Drive, Disk image, Tape, Cartridge */
        case OV_ADVANCED: return ADV_ROWS;
        default:          return 0;
    }
}

static void change_section(Overlay *ov, int dir) {
    int s = ov->section;
    do {
        s = (s + dir + OV_SECTION_COUNT) % OV_SECTION_COUNT;
    } while (!section_available(ov, (OvSection)s));
    ov->section = (OvSection)s;
    ov->row = 0;
}

/* Enter on the current row. */
static void overlay_activate(Overlay *ov) {
    switch (ov->section) {
        case OV_GENERAL:
            if (ov->row == 0) {
                ov->cfg->tinker = !ov->cfg->tinker;
                /* Leaving Tinker off hides Advanced; fall back to General. */
                if (!ov->cfg->tinker && ov->section == OV_ADVANCED)
                    ov->section = OV_GENERAL;
            } else {
                open_rom_dialog(ov);
            }
            break;
        case OV_MEDIA:
            if (ov->row == 0) {
                /* Cycle the disk drive device number (8-11). */
                ov->cfg->drive_unit++;
                if (ov->cfg->drive_unit > 11) ov->cfg->drive_unit = 8;
            } else {
                open_media_dialog(ov, ov->row);
            }
            break;
        case OV_ADVANCED:
            switch (ov->row) {
                case ADV_SMOOTHING:
                    ov->cfg->smoothing = !ov->cfg->smoothing;
                    apply_display(ov);
                    break;
                case ADV_REAL_CRT:
                    ov->cfg->crt_enabled = !ov->cfg->crt_enabled;
                    apply_display(ov);
                    break;
                case ADV_CRT_SCANLINES:
                    ov->cfg->crt_scanlines += 5;
                    if (ov->cfg->crt_scanlines > 95) ov->cfg->crt_scanlines = 0;
                    apply_display(ov);
                    break;
                case ADV_ONE_DISPLAY:
                    ov->cfg->one_display = !ov->cfg->one_display;
                    display_set_one_display(&ov->c128->display,
                                            ov->cfg->one_display);
                    break;
                case ADV_DISPLAY_CHANGE_RESET:
                    ov->cfg->display_change_reset = !ov->cfg->display_change_reset;
                    break;
                case ADV_GIF_WIDTH:
                    ov->cfg->gif_width = cycle_gif_width(ov->cfg->gif_width);
                    break;
                case ADV_GIF_FPS:
                    ov->cfg->gif_fps = cycle_gif_fps(ov->cfg->gif_fps);
                    break;
                case ADV_GIF_ENCODER:
                    ov->cfg->gif_ffmpeg = !ov->cfg->gif_ffmpeg;
                    break;
                case ADV_TAPE_AUDIO:
                    ov->cfg->tape_audio_monitor = !ov->cfg->tape_audio_monitor;
                    break;
                case ADV_TAPE_VIDEO:
                    ov->cfg->tape_video_monitor = !ov->cfg->tape_video_monitor;
                    break;
                case ADV_NOTIFICATIONS:
                    cycle_notify_mode(ov->cfg);
                    break;
                case ADV_DEBUG:
                    ov->cfg->debug_overlay = !ov->cfg->debug_overlay;
                    break;
                case ADV_JOY_HIDAPI:
                    ov->cfg->joystick_hidapi = !ov->cfg->joystick_hidapi;
                    break;
                case ADV_RESET:
                    config_set_defaults(ov->cfg);
                    apply_display(ov);
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
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
        case SDL_SCANCODE_RIGHT:
            change_section(ov, (sc == SDL_SCANCODE_RIGHT) ? 1 : -1);
            break;
        case SDL_SCANCODE_UP:
            if (ov->row > 0) ov->row--;
            break;
        case SDL_SCANCODE_DOWN:
            if (ov->row < section_rows(ov, ov->section) - 1) ov->row++;
            break;
        case SDL_SCANCODE_RETURN:
            overlay_activate(ov);
            break;
        case SDL_SCANCODE_ESCAPE:
            if (ov->section == OV_MEDIA && ov->row == 1 &&
                (ov->cfg->disk_path[0] || ov->c128->drive.disk_attached)) {
                replace_disk_image(ov, NULL);
            } else {
                overlay_close(ov);
            }
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

    if (kind == OV_DIALOG_DISK) {
        replace_disk_image(ov, ov->dialog_path);
        return;
    }

    char *dest = NULL;
    if (kind == OV_DIALOG_TAPE)      dest = ov->cfg->tape_path;
    else if (kind == OV_DIALOG_CART) dest = ov->cfg->cart_path;
    else if (kind == OV_DIALOG_ROM)  dest = ov->cfg->rom_dir;
    if (dest) {
        snprintf(dest, CONFIG_PATH_MAX, "%s", ov->dialog_path);
        save_config(ov);
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
    static const char *const names[OV_SECTION_COUNT] =
        { "General", "Media", "Advanced" };
    float tx = 20;
    for (int s = 0; s < OV_SECTION_COUNT; s++) {
        if (!section_available(ov, (OvSection)s)) continue;
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
        draw_row(r, lw, y, "Tinker", ov->cfg->tinker ? "On" : "Off",
                 ov->row == 0); y += OV_LINE_H;
        char rd[CONFIG_PATH_MAX];
        rom_path_display(ov, rd, sizeof(rd));
        draw_row(r, lw, y, "ROMS PATH", rd, ov->row == 1);
    } else if (ov->section == OV_MEDIA) {
        for (int i = 0; i < MEDIA_ITEM_COUNT; i++) {
            char vbuf[CONFIG_PATH_MAX + 8];
            if (i == 0) {
                /* Disk Drive: show the IEC device number. */
                snprintf(vbuf, sizeof(vbuf), "#%d", ov->cfg->drive_unit);
            } else {
                const char *path = media_path(ov, i);
                if (path && path[0])
                    snprintf(vbuf, sizeof(vbuf), "%s (%s)", media_extension(i), path);
                else
                    snprintf(vbuf, sizeof(vbuf), "<none> (%s)", media_extension(i));
            }
            draw_row(r, lw, y, media_label(i), vbuf, i == ov->row);
            y += OV_LINE_H;
        }
    } else {
        char sline[64], gline[64];
        const char *version =
#ifdef PACKAGE_VERSION
            PACKAGE_VERSION;
#else
            "unknown";
#endif
        snprintf(sline, sizeof(sline), "%d%%%s", ov->cfg->crt_scanlines,
                 ov->cfg->crt_enabled ? "" : " (inactive)");
        snprintf(gline, sizeof(gline), "%dx%d", ov->cfg->gif_width,
                 (ov->cfg->gif_width * 5) / 8);

        draw_row(r, lw, y, "Smoothing", ov->cfg->smoothing ? "On" : "Off",
                 ov->row == ADV_SMOOTHING); y += OV_LINE_H;
        draw_row(r, lw, y, "Real CRT", ov->cfg->crt_enabled ? "On" : "Off",
                 ov->row == ADV_REAL_CRT); y += OV_LINE_H;
        draw_row(r, lw, y, "CRT scanlines", sline,
                 ov->row == ADV_CRT_SCANLINES); y += OV_LINE_H;
        draw_row(r, lw, y, "Unified Display", ov->cfg->one_display ? "On" : "Off",
                 ov->row == ADV_ONE_DISPLAY); y += OV_LINE_H;
        draw_row(r, lw, y, "Display Change reset",
                 ov->cfg->display_change_reset ? "On" : "Off",
                 ov->row == ADV_DISPLAY_CHANGE_RESET); y += OV_LINE_H;
        draw_row(r, lw, y, "GIF resolution", gline,
                 ov->row == ADV_GIF_WIDTH); y += OV_LINE_H;
        {
            char fps[32];
            snprintf(fps, sizeof(fps), "%d fps", ov->cfg->gif_fps);
            draw_row(r, lw, y, "GIF frame rate", fps,
                     ov->row == ADV_GIF_FPS);
        }
        y += OV_LINE_H;
        draw_row(r, lw, y, "GIF encoder",
                 ov->cfg->gif_ffmpeg ? "FFmpeg optimize" : "built-in",
                 ov->row == ADV_GIF_ENCODER); y += OV_LINE_H;
        draw_row(r, lw, y, "Tape Audio Monitor",
                 ov->cfg->tape_audio_monitor ? "On" : "Off",
                 ov->row == ADV_TAPE_AUDIO); y += OV_LINE_H;
        draw_row(r, lw, y, "Tape Video Monitor",
                 ov->cfg->tape_video_monitor ? "On" : "Off",
                 ov->row == ADV_TAPE_VIDEO); y += OV_LINE_H;
        draw_row(r, lw, y, "Notifications", notify_mode_name(ov->cfg->notify_mode),
                 ov->row == ADV_NOTIFICATIONS); y += OV_LINE_H;
        draw_row(r, lw, y, "Debug", ov->cfg->debug_overlay ? "On" : "Off",
                 ov->row == ADV_DEBUG); y += OV_LINE_H;
        draw_row(r, lw, y, "Joystick HIDAPI",
                 ov->cfg->joystick_hidapi ? "On" : "Off",
                 ov->row == ADV_JOY_HIDAPI); y += OV_LINE_H;
        draw_row(r, lw, y, "Reset defaults", NULL,
                 ov->row == ADV_RESET); y += OV_LINE_H;
        draw_row(r, lw, y, "Version", version, ov->row == ADV_VERSION);
    }

    /* Footer. */
    SDL_SetRenderDrawColor(r, 0xAA, 0xAA, 0xAA, 255);
    const char *footer =
        (ov->section == OV_MEDIA && ov->row == 1 &&
         (ov->cfg->disk_path[0] || ov->c128->drive.disk_attached))
        ? "Left/Right section  Up/Down select  Enter choose  Esc eject  F9 close"
        : "Left/Right section  Up/Down select  Enter toggle/choose  F9/Esc close";
    SDL_RenderDebugText(r, (float)((lw - (int)strlen(footer) * 8) / 2),
                        (float)(lh - 20), footer);

    SDL_SetRenderScale(r, 1.0f, 1.0f);
}
