#include "overlay.h"
#include "notify.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/stat.h>

#define OV_SCALE      1.25f
#define OV_LINE_H     20
#define OV_VALUE_X    230

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "unknown"
#endif
#ifndef PROG_GIT_COMMIT
#define PROG_GIT_COMMIT "unknown"
#endif

static const char *const about_lines[] = {
    "1986 Commodore C128DCR emulator",
    "(c) 2026 salvogendut",
    "Version " PACKAGE_VERSION " (commit " PROG_GIT_COMMIT ")",
    "VICE-derived emulation core - see README"
};
#define ABOUT_LINE_COUNT ((int)(sizeof(about_lines) / sizeof(about_lines[0])))

static const char *const keyboard_map_lines[] = {
    "HOST KEY                         C128 KEY / ACTION",
    "Left/Right Shift                SHIFT",
    "Left/Right Alt                  C= (Commodore)",
    "Shift + Alt                     switch character sets",
    "Caps Lock                       Shift+C= shortcut",
    "Escape                          RUN/STOP",
    "Page Up                         RESTORE (NMI)",
    "Escape + Page Up                RUN/STOP + RESTORE",
    "Left/Right Ctrl                 CONTROL",
    "Backspace                       DEL",
    "Home                            CLR/HOME",
    "Arrow keys                      C128 cursor keys",
    "F10                             toggle 40/80 display",
    "Shift + Print Screen            hold 40/80 key",
    "Shift + F1..F8                  C128 function keys",
    "F1, F4..F12                    emulator shortcuts",
    "Enter or Escape                 close this map"
};
#define KEYBOARD_MAP_LINE_COUNT ((int)(sizeof(keyboard_map_lines) / sizeof(keyboard_map_lines[0])))

/* Logical Media rows; Drive 2 rows collapse away when disabled. */
#define MEDIA_DRIVE1   0
#define MEDIA_TYPE1    1
#define MEDIA_DISK1    2
#define MEDIA_DRIVE2   3
#define MEDIA_TYPE2    4
#define MEDIA_DISK2    5
#define MEDIA_TAPE     6
#define MEDIA_CART     7
#define MEDIA_U36      8
#define MEDIA_ITEM_COUNT 9

/* Advanced section rows. */
#define ADV_SMOOTHING           0
#define ADV_REAL_CRT            1
#define ADV_CRT_SCANLINES       2
#define ADV_ONE_DISPLAY         3
#define ADV_DISPLAY_CHANGE_RESET 4
#define ADV_VDC_RAM             5
#define ADV_REAL_DISK_DRIVE     6
#define ADV_SECOND_DRIVE        7
#define ADV_GIF_WIDTH           8
#define ADV_GIF_FPS             9
#define ADV_GIF_ENCODER         10
#define ADV_TAPE_AUDIO          11
#define ADV_TAPE_VIDEO          12
#define ADV_NOTIFICATIONS       13
#define ADV_DEBUG               14
#define ADV_JOY_HIDAPI          15
#define ADV_KEYBOARD_MAP        16
#define ADV_RESET               17
#define ADV_VERSION             18
#define ADV_ROWS                19

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
    ov->about_visible = false;
    ov->keyboard_map_visible = false;
    ov->visible = false;
}

/* Replace the live disk as a physical eject followed by an insert.  The
 * configured path always describes the media that is actually attached; a
 * failed insert therefore leaves both the drive and the setting empty. */
static bool replace_disk_image(Overlay *ov, int which, const char *path) {
    Drive *drive = which == 2 ? &ov->c128->drive2 : &ov->c128->drive;
    char *configured = which == 2 ? ov->cfg->disk2_path : ov->cfg->disk_path;
    configured[0] = '\0';

    if (drive_attach_disk(drive, path) != 0) {
        fprintf(stderr, "1986: could not attach disk '%s'\n", path);
        notify_post("COULD NOT INSERT DISK IMAGE");
        save_config(ov);
        return false;
    }

    if (path && path[0]) {
        snprintf(configured, CONFIG_PATH_MAX, "%s", path);
        char message[64];
        snprintf(message, sizeof(message), "DRIVE %d: %s MEDIA INSERTED",
                 which, disk_image_format_name(&drive->image));
        notify_post("%s", message);
    } else {
        notify_post(which == 2 ? "DRIVE 2 MEDIA EJECTED" :
                                 "DRIVE 1 MEDIA EJECTED");
    }
    save_config(ov);
    return true;
}

bool overlay_set_cartridge(Overlay *ov, const char *path) {
    char selected[CONFIG_PATH_MAX];
    if (path && strlen(path) >= sizeof(selected)) {
        notify_post("CARTRIDGE PATH TOO LONG");
        return false;
    }
    snprintf(selected, sizeof(selected), "%s", path ? path : "");
    Cartridge *cart = &ov->c128->mem.cart;
    bool had_cart = cart->attached;
    cartridge_detach(cart);
    ov->cfg->cart_path[0] = '\0';
    if (selected[0]) {
        CartridgeResult result = cartridge_attach(cart, selected);
        if (result != CART_OK) {
            fprintf(stderr, "1986: cartridge '%s': %s\n", selected,
                    cartridge_result_name(result));
            notify_post("%s", cartridge_result_name(result));
            if (had_cart) c128_reset(ov->c128);
            save_config(ov);
            return false;
        }
        snprintf(ov->cfg->cart_path, sizeof(ov->cfg->cart_path), "%s", selected);
        c128_reset(ov->c128); /* a cartridge is sampled at machine startup */
        notify_post("CARTRIDGE INSERTED - F10 SWITCHES DISPLAY");
    } else if (had_cart) {
        c128_reset(ov->c128);
        notify_post("CARTRIDGE EJECTED - MACHINE RESET");
    }
    save_config(ov);
    return true;
}

bool overlay_set_u36(Overlay *ov, const char *path) {
    char selected[CONFIG_PATH_MAX];
    if (path && strlen(path) >= sizeof(selected)) {
        notify_post("U36 ROM PATH TOO LONG");
        return false;
    }
    snprintf(selected, sizeof(selected), "%s", path ? path : "");
    Mem *mem = &ov->c128->mem;
    bool had_rom = mem->u36_attached;
    mem_detach_u36(mem);
    ov->cfg->u36_path[0] = '\0';
    if (selected[0]) {
        if (!mem_attach_u36(mem, selected)) {
            fprintf(stderr, "1986: invalid U36 ROM '%s' (expected raw 8/16/32 KiB)\n",
                    selected);
            notify_post("INVALID U36 ROM IMAGE");
            if (had_rom) c128_reset(ov->c128);
            save_config(ov);
            return false;
        }
        snprintf(ov->cfg->u36_path, sizeof(ov->cfg->u36_path), "%s", selected);
        c128_reset(ov->c128);
        notify_post("U36 ROM INSERTED - MACHINE RESET");
    } else if (had_rom) {
        c128_reset(ov->c128);
        notify_post("U36 ROM EJECTED - MACHINE RESET");
    }
    save_config(ov);
    return true;
}

static int media_item(const Overlay *ov, int row) {
    for (int item = 0; item < MEDIA_ITEM_COUNT; item++) {
        if (!ov->cfg->second_drive &&
            (item == MEDIA_DRIVE2 || item == MEDIA_TYPE2 || item == MEDIA_DISK2)) continue;
        if (!ov->cfg->real_disk_drive &&
            (item == MEDIA_TYPE1 || item == MEDIA_TYPE2)) continue;
        if (!ov->cfg->tinker && item == MEDIA_U36) continue;
        if (row-- == 0) return item;
    }
    return -1;
}

static void clear_media_entry(Overlay *ov) {
    if (ov->section != OV_MEDIA) return;

    int item = media_item(ov, ov->row);
    if (item == MEDIA_CART) {
        if (ov->cfg->cart_path[0] || ov->c128->mem.cart.attached)
            overlay_set_cartridge(ov, NULL);
        return;
    }
    if (item == MEDIA_U36) {
        if (ov->cfg->u36_path[0] || ov->c128->mem.u36_attached)
            overlay_set_u36(ov, NULL);
        return;
    }
    if (item == MEDIA_DISK1 || item == MEDIA_DISK2) {
        int which = item == MEDIA_DISK2 ? 2 : 1;
        Drive *drive = which == 2 ? &ov->c128->drive2 : &ov->c128->drive;
        char *path = which == 2 ? ov->cfg->disk2_path : ov->cfg->disk_path;
        if (path[0] || drive->disk_attached)
            replace_disk_image(ov, which, NULL);
        return;
    }

    char *path = NULL;
    if (item == MEDIA_TAPE) path = ov->cfg->tape_path;
    else if (item == MEDIA_CART) path = ov->cfg->cart_path;
    if (path && path[0]) {
        path[0] = '\0';
        save_config(ov);
        notify_post("MEDIA ENTRY CLEARED");
    }
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
        "Drive 1", "Drive 1 type", "Drive 1 image",
        "Drive 2", "Drive 2 type", "Drive 2 image",
        "Tape", "Cartridge", "U36 internal ROM"
    };
    return labels[row];
}

static const char *media_extension(int row) {
    static const char *const exts[MEDIA_ITEM_COUNT] = {
        "", "", ".d64/.d71/.d81/.prg", "", "", ".d64/.d71/.d81/.prg", ".tap",
        ".crt/.bin/.rom", ".bin/.rom"
    };
    return exts[row];
}

/* The selected file path (or NULL) for a Media row. */
static const char *media_path(const Overlay *ov, int row) {
    if (row == MEDIA_DISK1) return ov->cfg->disk_path;
    if (row == MEDIA_DISK2) return ov->cfg->disk2_path;
    if (row == MEDIA_TAPE) return ov->cfg->tape_path;
    if (row == MEDIA_CART) return ov->cfg->cart_path;
    if (row == MEDIA_U36) return ov->cfg->u36_path;
    return NULL; /* Drive unit number (cycled) */
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

static bool existing_directory(const char *path) {
    struct stat info;
    return path && path[0] && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool file_parent_directory(const char *path, char *out, size_t size) {
    const char *slash = path ? strrchr(path, '/') : NULL;
    if (!slash) return false;
    size_t length = slash == path ? 1 : (size_t)(slash - path);
    if (length >= size) return false;
    memcpy(out, path, length);
    out[length] = '\0';
    return true;
}

static char *recent_dialog_directory(Config *cfg, OvDialogKind kind) {
    switch (kind) {
        case OV_DIALOG_DISK:  return cfg->last_disk_dir;
        case OV_DIALOG_DISK2: return cfg->last_disk2_dir;
        case OV_DIALOG_TAPE:  return cfg->last_tape_dir;
        case OV_DIALOG_CART:  return cfg->last_cart_dir;
        case OV_DIALOG_U36:   return cfg->last_u36_dir;
        default:              return NULL;
    }
}

static const char *selected_dialog_path(const Config *cfg, OvDialogKind kind) {
    switch (kind) {
        case OV_DIALOG_DISK:  return cfg->disk_path;
        case OV_DIALOG_DISK2: return cfg->disk2_path;
        case OV_DIALOG_TAPE:  return cfg->tape_path;
        case OV_DIALOG_CART:  return cfg->cart_path;
        case OV_DIALOG_U36:   return cfg->u36_path;
        default:              return NULL;
    }
}

static void set_dialog_start_location(Overlay *ov) {
    ov->dialog_location[0] = '\0';
    if (ov->dialog_kind == OV_DIALOG_ROM) {
        if (existing_directory(ov->cfg->rom_dir))
            snprintf(ov->dialog_location, sizeof(ov->dialog_location), "%s",
                     ov->cfg->rom_dir);
        return;
    }

    char *recent = recent_dialog_directory(ov->cfg, ov->dialog_kind);
    if (existing_directory(recent)) {
        snprintf(ov->dialog_location, sizeof(ov->dialog_location), "%s", recent);
        return;
    }
    const char *selected = selected_dialog_path(ov->cfg, ov->dialog_kind);
    char parent[CONFIG_PATH_MAX];
    if (file_parent_directory(selected, parent, sizeof(parent)) &&
        existing_directory(parent))
        snprintf(ov->dialog_location, sizeof(ov->dialog_location), "%s", parent);
}

static void remember_dialog_directory(Overlay *ov, OvDialogKind kind,
                                      const char *selected) {
    char *recent = recent_dialog_directory(ov->cfg, kind);
    char parent[CONFIG_PATH_MAX];
    if (recent && file_parent_directory(selected, parent, sizeof(parent)) &&
        existing_directory(parent))
        snprintf(recent, CONFIG_PATH_MAX, "%s", parent);
}

static void open_media_dialog(Overlay *ov, int row) {
    static const SDL_DialogFileFilter disk_filters[] = {
        { "D64/D71/D81 disk images or PRG", "d64;D64;d71;D71;d81;D81;prg;PRG" },
        { "All files",       "*"       },
    };
    static const SDL_DialogFileFilter tape_filters[] = {
        { "TAP tapes", "tap;TAP" },
        { "All files", "*"       },
    };
    static const SDL_DialogFileFilter cart_filters[] = {
        { "C128 CRT or function ROM", "crt;CRT;bin;BIN;rom;ROM" },
        { "All files",      "*"       },
    };
    static const SDL_DialogFileFilter u36_filters[] = {
        { "U36 function ROM", "bin;BIN;rom;ROM" },
        { "All files", "*" },
    };
    const SDL_DialogFileFilter *filters = disk_filters;
    if (row == MEDIA_DISK1 || row == MEDIA_DISK2) {
        ov->dialog_kind = row == MEDIA_DISK2 ? OV_DIALOG_DISK2 : OV_DIALOG_DISK;
        filters = disk_filters;
    } else if (row == MEDIA_TAPE) {
        ov->dialog_kind = OV_DIALOG_TAPE;
        filters = tape_filters;
    } else if (row == MEDIA_U36) {
        ov->dialog_kind = OV_DIALOG_U36;
        filters = u36_filters;
    } else {
        ov->dialog_kind = OV_DIALOG_CART;
        filters = cart_filters;
    }
    ov->dialog_ready = false;
    ov->dialog_failed = false;
    ov->dialog_error[0] = '\0';
    set_dialog_start_location(ov);
    SDL_ShowOpenFileDialog(overlay_file_callback, ov,
                           ov->c128 ? ov->c128->display.window : NULL,
                           filters, 2,
                           ov->dialog_location[0] ? ov->dialog_location : NULL,
                           false);
}

/* Folder picker for the ROM directory (General section). */
static void open_rom_dialog(Overlay *ov) {
    ov->dialog_kind   = OV_DIALOG_ROM;
    ov->dialog_ready  = false;
    ov->dialog_failed = false;
    ov->dialog_error[0] = '\0';
    set_dialog_start_location(ov);
    SDL_ShowOpenFolderDialog(overlay_file_callback, ov,
                             ov->c128 ? ov->c128->display.window : NULL,
                             ov->dialog_location[0] ? ov->dialog_location : NULL,
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
    switch (s) {
        case OV_GENERAL:  return 7;   /* display, input ports, Tinker, ROMs, About */
        case OV_MEDIA:    return 4 + (ov->cfg->second_drive ? 2 : 0) +
                                 (ov->cfg->real_disk_drive ? 1 + (ov->cfg->second_drive ? 1 : 0) : 0) +
                                 (ov->cfg->tinker ? 1 : 0);
        case OV_ADVANCED: return ADV_ROWS;
        default:          return 0;
    }
}

static int next_drive_unit(int current, int other) {
    do {
        current = current >= 11 ? 8 : current + 1;
    } while (current == other);
    return current;
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
                ov->cfg->col_mode_80 = !ov->cfg->col_mode_80;
                c128_set_4080(ov->c128, ov->cfg->col_mode_80);
                display_focus_active(&ov->c128->display);
                if (ov->cfg->display_change_reset)
                    c128_reset(ov->c128);
                save_config(ov);
            } else if (ov->row == 1) {
                ov->cfg->main_input_port = ov->cfg->main_input_port == 1 ? 2 : 1;
            } else if (ov->row == 2 || ov->row == 3) {
                unsigned port = (unsigned)(ov->row - 2);
                ov->cfg->joy_port_mode[port] =
                    ov->cfg->joy_port_mode[port] == JOYPORT_MOUSE
                    ? JOYPORT_JOYSTICK : JOYPORT_MOUSE;
                joyports_set_joystick(&ov->c128->joyports, port, 0);
                joyports_mouse_button(&ov->c128->joyports, port, false, false);
                joyports_mouse_button(&ov->c128->joyports, port, true, false);
            } else if (ov->row == 4) {
                ov->cfg->tinker = !ov->cfg->tinker;
                /* Leaving Tinker off hides Advanced; fall back to General. */
                if (!ov->cfg->tinker && ov->section == OV_ADVANCED)
                    ov->section = OV_GENERAL;
            } else if (ov->row == 5) {
                open_rom_dialog(ov);
            } else {
                ov->about_visible = true;
            }
            break;
        case OV_MEDIA:
            if (media_item(ov, ov->row) == MEDIA_DRIVE1) {
                ov->cfg->drive_unit = next_drive_unit(
                    ov->cfg->drive_unit, ov->cfg->drive2_unit);
                drive_reset(&ov->c128->drive); /* abandon the old IEC address */
            } else if (media_item(ov, ov->row) == MEDIA_DRIVE2) {
                ov->cfg->drive2_unit = next_drive_unit(
                    ov->cfg->drive2_unit, ov->cfg->drive_unit);
                drive_reset(&ov->c128->drive2);
                drive_set_unit(&ov->c128->drive2, ov->cfg->drive2_unit);
            } else if (media_item(ov, ov->row) == MEDIA_TYPE1 ||
                       media_item(ov, ov->row) == MEDIA_TYPE2) {
                int *type = media_item(ov, ov->row) == MEDIA_TYPE1
                          ? &ov->cfg->drive_type : &ov->cfg->drive2_type;
                *type = *type == 1571 ? 1581 : 1571;
                notify_post(*type == 1571
                    ? "1571CR HARDWARE BACKEND UNDER DEVELOPMENT"
                    : "1581 HARDWARE BACKEND NOT YET IMPLEMENTED");
                save_config(ov);
            } else {
                open_media_dialog(ov, media_item(ov, ov->row));
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
                    display_focus_active(&ov->c128->display);
                    break;
                case ADV_DISPLAY_CHANGE_RESET:
                    ov->cfg->display_change_reset = !ov->cfg->display_change_reset;
                    break;
                case ADV_VDC_RAM:
                    ov->cfg->vdc_ram_kb = ov->cfg->vdc_ram_kb == 64 ? 16 : 64;
                    vdc_set_ram_size_kb(&ov->c128->vdc, ov->cfg->vdc_ram_kb);
                    notify_post("VDC RAM: %dK - RESET FOR SOFTWARE TO REDETECT",
                                ov->cfg->vdc_ram_kb);
                    save_config(ov);
                    break;
                case ADV_REAL_DISK_DRIVE:
                    ov->cfg->real_disk_drive = !ov->cfg->real_disk_drive;
                    notify_post(ov->cfg->real_disk_drive
                        ? "REAL DRIVE EMULATOR PENDING - USING VIRTUAL DRIVE"
                        : "FAST VIRTUAL DRIVE ACTIVE");
                    break;
                case ADV_SECOND_DRIVE:
                    ov->cfg->second_drive = !ov->cfg->second_drive;
                    drive_reset(&ov->c128->drive2);
                    drive_set_unit(&ov->c128->drive2, ov->cfg->drive2_unit);
                    notify_post(ov->cfg->second_drive
                        ? "SECOND DRIVE CONNECTED"
                        : "SECOND DRIVE DISCONNECTED");
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
                    notify_post("JOYSTICK HIDAPI CHANGE APPLIES AFTER RESTART");
                    break;
                case ADV_KEYBOARD_MAP:
                    ov->keyboard_map_visible = true;
                    break;
                case ADV_RESET:
                    config_set_defaults(ov->cfg);
                    vdc_set_ram_size_kb(&ov->c128->vdc, ov->cfg->vdc_ram_kb);
                    drive_attach_disk(&ov->c128->drive, NULL);
                    drive_attach_disk(&ov->c128->drive2, NULL);
                    drive_set_unit(&ov->c128->drive, ov->cfg->drive_unit);
                    drive_set_unit(&ov->c128->drive2, ov->cfg->drive2_unit);
                    apply_display(ov);
                    ov->section = OV_GENERAL; /* Tinker is now off. */
                    ov->row = 0;
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

    if (ov->about_visible) {
        if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_ESCAPE)
            ov->about_visible = false;
        return true;
    }
    if (ov->keyboard_map_visible) {
        if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_ESCAPE)
            ov->keyboard_map_visible = false;
        return true;
    }

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
        case SDL_SCANCODE_DELETE:
            clear_media_entry(ov);
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
    remember_dialog_directory(ov, kind, ov->dialog_path);

    if (kind == OV_DIALOG_DISK || kind == OV_DIALOG_DISK2) {
        replace_disk_image(ov, kind == OV_DIALOG_DISK2 ? 2 : 1,
                           ov->dialog_path);
        return;
    }
    if (kind == OV_DIALOG_CART) {
        overlay_set_cartridge(ov, ov->dialog_path);
        return;
    }
    if (kind == OV_DIALOG_U36) {
        overlay_set_u36(ov, ov->dialog_path);
        return;
    }

    char *dest = NULL;
    if (kind == OV_DIALOG_TAPE)      dest = ov->cfg->tape_path;
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
        SDL_FRect hl = { 10, y, (float)lw - 4, OV_LINE_H - 4 };
        SDL_RenderFillRect(r, &hl);
    }
    SDL_SetRenderDrawColor(r, 0xFF, 0xFF, 0xFF, 255);
    SDL_RenderDebugText(r, 20, y, label);
    if (value) {
        char shown[96];
        size_t max_chars = (size_t)(lw - OV_VALUE_X - 18) / 8;
        if (max_chars >= sizeof(shown)) max_chars = sizeof(shown) - 1;
        size_t n = strlen(value);
        if (n > max_chars && max_chars > 3) {
            memcpy(shown, value, max_chars - 3);
            memcpy(shown + max_chars - 3, "...", 4);
        } else {
            snprintf(shown, sizeof(shown), "%s", value);
        }
        SDL_SetRenderDrawColor(r, 0xFF, highlight ? 0xFF : 0xD0,
                               highlight ? 0xFF : 0x80, 255);
        SDL_RenderDebugText(r, (float)OV_VALUE_X, y, shown);
    }
}

void overlay_render(const Overlay *ov, SDL_Renderer *r) {
    if (!ov->visible) return;

    int rw, rh;
    if (!SDL_GetRenderOutputSize(r, &rw, &rh))
        SDL_GetWindowSize(ov->c128->display.window, &rw, &rh);
    float scale = OV_SCALE;
    if ((float)rw / scale < 840.0f) scale = (float)rw / 840.0f;
    if ((float)rh / scale < 470.0f) scale = (float)rh / 470.0f;
    if (scale <= 0.0f) return;
    SDL_SetRenderScale(r, scale, scale);
    int lw = (int)(rw / scale);
    int panel_w = lw - 20 < 820 ? lw - 20 : 820;
    int rows = ov->section == OV_ADVANCED ? ADV_ROWS :
               ov->section == OV_MEDIA ? section_rows(ov, OV_MEDIA) : 11;
    int panel_h = 48 + rows * OV_LINE_H + 42;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 90);
    SDL_FRect shade = { 0, 0, (float)lw, (float)(rh / scale) };
    SDL_RenderFillRect(r, &shade);
    SDL_SetRenderDrawColor(r, 8, 10, 24, 235);
    SDL_FRect bg = { 8, 8, (float)panel_w, (float)panel_h };
    SDL_RenderFillRect(r, &bg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    /* Section tabs. */
    SDL_SetRenderDrawColor(r, 0x30, 0x40, 0x60, 255);
    SDL_FRect tabbar = { 10, 10, (float)panel_w - 4, 22 };
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

        draw_row(r, panel_w, y, "Machine", machine, false); y += OV_LINE_H;
        draw_row(r, panel_w, y, "CPU", "MOS 8502 @ 2 MHz + Z80A (CP/M)", false);
        y += OV_LINE_H;
        draw_row(r, panel_w, y, "Memory", "128 KB", false); y += OV_LINE_H;
        draw_row(r, panel_w, y, "Video", "VIC-IIe (40-col) + 8563 VDC (80-col)", false);
        y += OV_LINE_H;
        draw_row(r, panel_w, y, "Sound", "SID 8580", false); y += OV_LINE_H;
#ifdef PACKAGE_VERSION
        draw_row(r, panel_w, y, "Emulator", PACKAGE_VERSION, false); y += OV_LINE_H;
#endif
        y += OV_LINE_H;
        draw_row(r, panel_w, y, "40/80 key",
                 ov->cfg->col_mode_80 ? "80 columns (VDC)" : "40 columns (VIC)",
                 ov->row == 0); y += OV_LINE_H;
        draw_row(r, panel_w, y, "Main input",
                 ov->cfg->main_input_port == 1 ? "Joy Port 1" : "Joy Port 2",
                 ov->row == 1); y += OV_LINE_H;
        draw_row(r, panel_w, y, "Joy Port 1",
                 ov->cfg->joy_port_mode[0] == JOYPORT_MOUSE ? "Mouse (1351)" : "Joystick",
                 ov->row == 2); y += OV_LINE_H;
        draw_row(r, panel_w, y, "Joy Port 2",
                 ov->cfg->joy_port_mode[1] == JOYPORT_MOUSE ? "Mouse (1351)" : "Joystick",
                 ov->row == 3); y += OV_LINE_H;
        draw_row(r, panel_w, y, "Tinker", ov->cfg->tinker ? "On" : "Off",
                 ov->row == 4); y += OV_LINE_H;
        char rd[CONFIG_PATH_MAX];
        rom_path_display(ov, rd, sizeof(rd));
        draw_row(r, panel_w, y, "ROMS PATH", rd, ov->row == 5);
        y += OV_LINE_H;
        draw_row(r, panel_w, y, "About", "Program details", ov->row == 6);
    } else if (ov->section == OV_MEDIA) {
        for (int i = 0; i < section_rows(ov, OV_MEDIA); i++) {
            int item = media_item(ov, i);
            char vbuf[CONFIG_PATH_MAX + 8];
            if (item == MEDIA_DRIVE1 || item == MEDIA_DRIVE2) {
                int unit = item == MEDIA_DRIVE2 ? ov->cfg->drive2_unit :
                                                 ov->cfg->drive_unit;
                snprintf(vbuf, sizeof(vbuf), "#%d", unit);
            } else if (item == MEDIA_TYPE1 || item == MEDIA_TYPE2) {
                int type = item == MEDIA_TYPE2 ? ov->cfg->drive2_type :
                                                  ov->cfg->drive_type;
                snprintf(vbuf, sizeof(vbuf), "%s", type == 1571
                         ? "1571CR (pending)" : "1581 (not implemented)");
            } else {
                const char *path = media_path(ov, item);
                if (path && path[0])
                    snprintf(vbuf, sizeof(vbuf), "%s (%s)", media_extension(item), path);
                else
                    snprintf(vbuf, sizeof(vbuf), "<none> (%s)", media_extension(item));
            }
            draw_row(r, panel_w, y, media_label(item), vbuf, i == ov->row);
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

        draw_row(r, panel_w, y, "Smoothing", ov->cfg->smoothing ? "On" : "Off",
                 ov->row == ADV_SMOOTHING); y += OV_LINE_H;
        draw_row(r, panel_w, y, "Real CRT", ov->cfg->crt_enabled ? "On" : "Off",
                 ov->row == ADV_REAL_CRT); y += OV_LINE_H;
        draw_row(r, panel_w, y, "CRT scanlines", sline,
                 ov->row == ADV_CRT_SCANLINES); y += OV_LINE_H;
        draw_row(r, panel_w, y, "Unified Display", ov->cfg->one_display ? "On" : "Off",
                 ov->row == ADV_ONE_DISPLAY); y += OV_LINE_H;
        draw_row(r, panel_w, y, "Display Change reset",
                 ov->cfg->display_change_reset ? "On" : "Off",
                 ov->row == ADV_DISPLAY_CHANGE_RESET); y += OV_LINE_H;
        draw_row(r, panel_w, y, "VDC RAM",
                 ov->cfg->vdc_ram_kb == 16 ? "16K" : "64K",
                 ov->row == ADV_VDC_RAM); y += OV_LINE_H;
        draw_row(r, panel_w, y, "Real Disk Drive",
                 ov->cfg->real_disk_drive ? "On (pending)" : "Off",
                 ov->row == ADV_REAL_DISK_DRIVE); y += OV_LINE_H;
        draw_row(r, panel_w, y, "Second Drive",
                 ov->cfg->second_drive ? "On" : "Off",
                 ov->row == ADV_SECOND_DRIVE); y += OV_LINE_H;
        draw_row(r, panel_w, y, "GIF resolution", gline,
                 ov->row == ADV_GIF_WIDTH); y += OV_LINE_H;
        {
            char fps[32];
            snprintf(fps, sizeof(fps), "%d fps", ov->cfg->gif_fps);
            draw_row(r, panel_w, y, "GIF frame rate", fps,
                     ov->row == ADV_GIF_FPS);
        }
        y += OV_LINE_H;
        draw_row(r, panel_w, y, "GIF encoder",
                 ov->cfg->gif_ffmpeg ? "FFmpeg optimize" : "built-in",
                 ov->row == ADV_GIF_ENCODER); y += OV_LINE_H;
        draw_row(r, panel_w, y, "Tape Audio Monitor",
                 ov->cfg->tape_audio_monitor ? "On" : "Off",
                 ov->row == ADV_TAPE_AUDIO); y += OV_LINE_H;
        draw_row(r, panel_w, y, "Tape Video Monitor",
                 ov->cfg->tape_video_monitor ? "On" : "Off",
                 ov->row == ADV_TAPE_VIDEO); y += OV_LINE_H;
        draw_row(r, panel_w, y, "Notifications", notify_mode_name(ov->cfg->notify_mode),
                 ov->row == ADV_NOTIFICATIONS); y += OV_LINE_H;
        draw_row(r, panel_w, y, "Debug", ov->cfg->debug_overlay ? "On" : "Off",
                 ov->row == ADV_DEBUG); y += OV_LINE_H;
        draw_row(r, panel_w, y, "Joystick HIDAPI",
                 ov->cfg->joystick_hidapi ? "On" : "Off",
                 ov->row == ADV_JOY_HIDAPI); y += OV_LINE_H;
        draw_row(r, panel_w, y, "Keyboard map", "[Enter]",
                 ov->row == ADV_KEYBOARD_MAP); y += OV_LINE_H;
        draw_row(r, panel_w, y, "Reset defaults", NULL,
                 ov->row == ADV_RESET); y += OV_LINE_H;
        draw_row(r, panel_w, y, "Version", version, ov->row == ADV_VERSION);
    }

    /* Footer. */
    SDL_SetRenderDrawColor(r, 0xAA, 0xAA, 0xAA, 255);
    const char *footer = ov->section == OV_MEDIA
        ? "Left/Right section  Up/Down select  Enter choose  Del clear  F9/Esc close"
        : "Left/Right section  Up/Down select  Enter toggle/choose  F9/Esc close";
    SDL_RenderDebugText(r, 20,
                        (float)(panel_h - 20), footer);

    if (ov->about_visible) {
        int lh = (int)(rh / scale);
        int text_w = 0;
        for (int i = 0; i < ABOUT_LINE_COUNT; ++i) {
            int w = (int)strlen(about_lines[i]) * 8;
            if (w > text_w) text_w = w;
        }
        int box_w = text_w + 32;
        int box_h = 28 + ABOUT_LINE_COUNT * 16 + 28;
        float bx = (float)(lw - box_w) * 0.5f;
        float by = (float)(lh - box_h) * 0.5f;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 180);
        SDL_FRect dim = { 0, 0, (float)lw, (float)lh };
        SDL_RenderFillRect(r, &dim);
        SDL_SetRenderDrawColor(r, 0x19, 0x20, 0x34, 255);
        SDL_FRect box = { bx, by, (float)box_w, (float)box_h };
        SDL_RenderFillRect(r, &box);
        SDL_SetRenderDrawColor(r, 0x89, 0xA3, 0xCB, 255);
        SDL_RenderRect(r, &box);
        SDL_SetRenderDrawColor(r, 0xF0, 0xF0, 0xF0, 255);
        for (int i = 0; i < ABOUT_LINE_COUNT; ++i)
            SDL_RenderDebugText(r, bx + 16, by + 16 + i * 16,
                                about_lines[i]);
        SDL_SetRenderDrawColor(r, 0xFF, 0xDA, 0x79, 255);
        SDL_RenderDebugText(r, bx + (box_w - 16) * 0.5f,
                            by + box_h - 20, "OK");
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    if (ov->keyboard_map_visible) {
        int lh = (int)(rh / scale);
        int box_w = 640;
        int box_h = 24 + KEYBOARD_MAP_LINE_COUNT * 16 + 20;
        float bx = (float)(lw - box_w) * 0.5f;
        float by = (float)(lh - box_h) * 0.5f;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 180);
        SDL_FRect dim = { 0, 0, (float)lw, (float)lh };
        SDL_RenderFillRect(r, &dim);
        SDL_SetRenderDrawColor(r, 0x19, 0x20, 0x34, 255);
        SDL_FRect box = { bx, by, (float)box_w, (float)box_h };
        SDL_RenderFillRect(r, &box);
        SDL_SetRenderDrawColor(r, 0x89, 0xA3, 0xCB, 255);
        SDL_RenderRect(r, &box);
        SDL_SetRenderDrawColor(r, 0xF0, 0xF0, 0xF0, 255);
        for (int i = 0; i < KEYBOARD_MAP_LINE_COUNT; ++i)
            SDL_RenderDebugText(r, bx + 16, by + 16 + i * 16,
                                keyboard_map_lines[i]);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    SDL_SetRenderScale(r, 1.0f, 1.0f);
}
