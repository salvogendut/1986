#include "overlay.h"
#include "leds.h"
#include "notify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

/* Only the keyboard-driven overlay state is exercised here. */
void leds_ping(LedId id) { (void)id; }
void leds_set_drive_unit(LedId id, int unit) { (void)id; (void)unit; }
static bool second_led_enabled;
void leds_set_enabled(LedId id, bool enabled) {
    if (id == LED_FDC_B) second_led_enabled = enabled;
}
void notify_set_mode(NotifyMode mode) { (void)mode; }
void notify_post(const char *fmt, ...) { (void)fmt; }
void display_set_smoothing(Display *d, bool smooth) { (void)d; (void)smooth; }
void display_set_crt(Display *d, bool enabled, int scanlines, int brightness,
                     int contrast, int red, int green, int blue) {
    (void)d; (void)enabled; (void)scanlines; (void)brightness;
    (void)contrast; (void)red; (void)green; (void)blue;
}
void display_set_one_display(Display *d, bool one) { (void)d; (void)one; }
void vdc_set_ram_size_kb(Vdc *v, int kb) {
    v->address_mask = kb == 16 ? 0x3FFF : 0xFFFF;
}
static int display_focuses;
void display_focus_active(Display *d) { (void)d; display_focuses++; }
void c128_set_4080(C128 *c, bool col80) {
    c->col_mode_80 = col80;
    c->mem.mmu.col4080 = !col80;
    c->mem.ram[0xD7] = col80 ? 0x80 : 0;
    c->display.vdc_active = col80;
}
static int machine_resets;
void c128_reset(C128 *c) { (void)c; machine_resets++; }

static char picker_location[CONFIG_PATH_MAX];
static char picker_filter[128];
static bool picker_was_folder;
static bool cancel_next_picker;
void SDL_ShowOpenFileDialog(SDL_DialogFileCallback callback, void *userdata,
                            SDL_Window *window, const SDL_DialogFileFilter *filters,
                            int nfilters, const char *default_location,
                            bool allow_many) {
    (void)callback; (void)userdata; (void)window; (void)allow_many;
    picker_was_folder = false;
    snprintf(picker_filter, sizeof(picker_filter), "%s",
             filters && nfilters ? filters[0].pattern : "");
    snprintf(picker_location, sizeof(picker_location), "%s",
             default_location ? default_location : "");
    if (cancel_next_picker) {
        const char *cancelled[] = { NULL };
        cancel_next_picker = false;
        callback(userdata, cancelled, -1);
    }
}
void SDL_ShowOpenFolderDialog(SDL_DialogFileCallback callback, void *userdata,
                              SDL_Window *window, const char *default_location,
                              bool allow_many) {
    (void)callback; (void)userdata; (void)window; (void)allow_many;
    picker_was_folder = true;
    snprintf(picker_location, sizeof(picker_location), "%s",
             default_location ? default_location : "");
}

static void key(Overlay *ov, SDL_Scancode sc) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = sc;
    overlay_handle_event(ov, &event);
}

static bool overlay_draws_after_one_f9(const Overlay *ov) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (!SDL_Init(SDL_INIT_VIDEO)) return false;
    SDL_Window *window = SDL_CreateWindow("overlay test", 1100, 800, 0);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, NULL) : NULL;
    bool drawn = false;
    if (renderer) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        overlay_render(ov, renderer);
        SDL_Surface *pixels = SDL_RenderReadPixels(renderer, NULL);
        if (pixels) {
            Uint8 red, green, blue, alpha;
            drawn = SDL_ReadSurfacePixel(pixels, 15, 15,
                                         &red, &green, &blue, &alpha) &&
                    red == 0x30 && green == 0x40 && blue == 0x60;
            SDL_DestroySurface(pixels);
        }
        SDL_DestroyRenderer(renderer);
    }
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return drawn;
}

int main(void) {
    Config cfg;
    config_set_defaults(&cfg);
    cfg.tinker = true;

    C128 *c = calloc(1, sizeof(*c));
    CHECK(c != NULL, "allocate C128 overlay fixture");
    if (!c) return 1;
    c->cfg = &cfg;
    drive_init(&c->drive, &cfg);
    drive_init(&c->drive2, &cfg);
    drive_set_slot(&c->drive2, 1);
    drive_set_unit(&c->drive2, cfg.drive2_unit);
    Overlay ov;
    overlay_init(&ov, &cfg, c);

    /* Isolate all overlay configuration writes from the user profile. */
    char temp_home[] = "/tmp/1986-overlay-test-XXXXXX";
    CHECK(mkdtemp(temp_home) != NULL, "create temporary config home");
    char config_file[CONFIG_PATH_MAX];
    snprintf(config_file, sizeof(config_file), "%s/1986.conf", temp_home);
    setenv("C128_CONFIG_PATH", config_file, 1);

    key(&ov, SDL_SCANCODE_F9);
    CHECK(overlay_is_visible(&ov), "one F9 keypress opens the options overlay");
    CHECK(overlay_draws_after_one_f9(&ov),
          "options overlay is drawn after one F9 keypress");
    CHECK(cfg.col_mode_80, "80-column key is selected by default");
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(!cfg.col_mode_80 && !c->col_mode_80 && c->mem.mmu.col4080 &&
          !c->display.vdc_active && display_focuses == 1,
          "General 40/80 key selects VIC and focuses its window");
    Config saved;
    config_set_defaults(&saved);
    CHECK(config_load(&saved, config_file) && !saved.col_mode_80,
          "40-column key selection is persisted");
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(cfg.col_mode_80 && c->col_mode_80 && !c->mem.mmu.col4080 &&
          c->display.vdc_active && display_focuses == 2,
          "General 40/80 key selects VDC and focuses its window");
    config_set_defaults(&saved);
    CHECK(config_load(&saved, config_file) && saved.col_mode_80,
          "80-column key selection is persisted");
    key(&ov, SDL_SCANCODE_DOWN);
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(cfg.main_input_port == 1, "General selects host input port 1");
    key(&ov, SDL_SCANCODE_DOWN);
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(cfg.joy_port_mode[0] == JOYPORT_MOUSE,
          "General changes port 1 from joystick to 1351 mouse");
    key(&ov, SDL_SCANCODE_RIGHT);
    key(&ov, SDL_SCANCODE_RIGHT);
    CHECK(ov.visible && ov.section == OV_ADVANCED,
          "Tinker exposes the Advanced section");
    for (int i = 0; i < 3; ++i) key(&ov, SDL_SCANCODE_DOWN);
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(cfg.one_display && display_focuses == 3,
          "enabling Unified Display focuses the selected output");
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(!cfg.one_display && display_focuses == 4,
          "restoring separate windows focuses the selected output");
    for (int i = 0; i < 2; ++i) key(&ov, SDL_SCANCODE_DOWN);
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(cfg.vdc_ram_kb == 16 && c->vdc.address_mask == 0x3FFF,
          "Advanced VDC RAM selects live 16K address mirroring");
    CHECK(config_load(&saved, config_file) && saved.vdc_ram_kb == 16,
          "Advanced VDC RAM selection is persisted");
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(cfg.vdc_ram_kb == 64 && c->vdc.address_mask == 0xFFFF,
          "Advanced VDC RAM switches back to 64K");
    for (int i = 0; i < 2; ++i) key(&ov, SDL_SCANCODE_DOWN);
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(cfg.second_drive && second_led_enabled,
          "Second Drive toggle enables its device and LED");

    key(&ov, SDL_SCANCODE_LEFT);
    CHECK(ov.section == OV_MEDIA && ov.row == 0,
          "return to Media with Drive 1 selected");
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(cfg.drive_unit == 10 && c->drive.unit == 10,
          "Drive 1 cycles past Drive 2's reserved #9");
    key(&ov, SDL_SCANCODE_DOWN);
    key(&ov, SDL_SCANCODE_DOWN);
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(cfg.drive2_unit == 11 && c->drive2.unit == 11 &&
          cfg.drive_unit != cfg.drive2_unit,
          "Drive 2 cycles to an unused unit and updates live routing");
    for (int i = 0; i < 8; ++i) key(&ov, SDL_SCANCODE_DOWN);
    CHECK(ov.row == 6, "Tinker Media includes U36 with Drive 2 enabled");

    key(&ov, SDL_SCANCODE_RIGHT);
    for (int i = 0; i < 7; ++i) key(&ov, SDL_SCANCODE_DOWN);
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(!cfg.second_drive && !second_led_enabled,
          "Second Drive toggle disables its device and LED");
    key(&ov, SDL_SCANCODE_LEFT);
    for (int i = 0; i < 8; ++i) key(&ov, SDL_SCANCODE_DOWN);
    CHECK(ov.row == 4, "disabled Media section hides Drive 2 but retains U36");

    key(&ov, SDL_SCANCODE_LEFT);
    CHECK(ov.section == OV_GENERAL && ov.row == 0,
          "General opens with first selectable row");
    for (int i = 0; i < 6; ++i) key(&ov, SDL_SCANCODE_DOWN);
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(ov.about_visible, "General About opens program details");
    key(&ov, SDL_SCANCODE_RIGHT);
    CHECK(ov.about_visible && ov.section == OV_GENERAL,
          "About dialog consumes navigation keys");
    key(&ov, SDL_SCANCODE_ESCAPE);
    CHECK(!ov.about_visible && ov.visible,
          "Escape closes About without closing options");
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(ov.about_visible, "Enter reopens About");
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(!ov.about_visible && ov.visible, "Enter dismisses About");

    key(&ov, SDL_SCANCODE_RIGHT);
    key(&ov, SDL_SCANCODE_RIGHT);
    CHECK(ov.section == OV_ADVANCED && ov.row == 0,
          "Advanced opens at its first row for keyboard map");
    for (int i = 0; i < 16; ++i) key(&ov, SDL_SCANCODE_DOWN);
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(ov.keyboard_map_visible, "Advanced opens the keyboard map");
    key(&ov, SDL_SCANCODE_LEFT);
    CHECK(ov.keyboard_map_visible && ov.section == OV_ADVANCED,
          "keyboard map consumes navigation keys");
    key(&ov, SDL_SCANCODE_ESCAPE);
    CHECK(!ov.keyboard_map_visible && ov.visible,
          "Escape closes keyboard map without closing options");
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(ov.keyboard_map_visible, "Enter reopens keyboard map");
    key(&ov, SDL_SCANCODE_F9);
    CHECK(!ov.keyboard_map_visible && !ov.visible,
          "F9 closes options and clears keyboard map state");
    key(&ov, SDL_SCANCODE_F9);
    CHECK(ov.visible, "reopen options for subsequent media checks");

    char disk_a[CONFIG_PATH_MAX / 2], disk_b[CONFIG_PATH_MAX / 2];
    snprintf(disk_a, sizeof(disk_a), "%s/disk-a", temp_home);
    snprintf(disk_b, sizeof(disk_b), "%s/disk-b", temp_home);
    CHECK(mkdir(disk_a, 0700) == 0 && mkdir(disk_b, 0700) == 0,
          "create independent picker directories");
    snprintf(cfg.disk_path, sizeof(cfg.disk_path), "%s/old.d64", temp_home);
    ov.section = OV_MEDIA;
    ov.row = 1;
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(ov.dialog_kind == OV_DIALOG_DISK && !picker_was_folder &&
          strcmp(picker_location, temp_home) == 0,
          "legacy Drive 1 image opens in its selected file's directory");
    snprintf(ov.dialog_path, sizeof(ov.dialog_path), "%s/missing.d64", disk_a);
    ov.dialog_ready = true;
    overlay_tick(&ov);
    CHECK(strcmp(cfg.last_disk_dir, disk_a) == 0 && cfg.disk_path[0] == '\0',
          "failed disk insertion still remembers its chosen directory");
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(strcmp(picker_location, disk_a) == 0,
          "Drive 1 picker returns to its recent directory after ejection");
    cancel_next_picker = true;
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(ov.dialog_kind == OV_DIALOG_NONE &&
          strcmp(cfg.last_disk_dir, disk_a) == 0,
          "cancelling a picker preserves its last-used directory");

    cfg.second_drive = true;
    snprintf(cfg.last_disk2_dir, sizeof(cfg.last_disk2_dir), "%s", disk_b);
    ov.row = 3;
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(ov.dialog_kind == OV_DIALOG_DISK2 &&
          strcmp(picker_location, disk_b) == 0,
          "Drive 2 keeps an independent picker directory");
    ov.row = 4;
    snprintf(ov.dialog_path, sizeof(ov.dialog_path), "%s/demo.tap", disk_a);
    ov.dialog_kind = OV_DIALOG_TAPE;
    ov.dialog_ready = true;
    overlay_tick(&ov);
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(ov.dialog_kind == OV_DIALOG_TAPE &&
          strcmp(picker_location, disk_a) == 0,
          "tape picker remembers its selected directory");
    key(&ov, SDL_SCANCODE_DELETE);
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(cfg.tape_path[0] == '\0' && strcmp(picker_location, disk_a) == 0,
          "clearing tape leaves its picker history intact");

    snprintf(cfg.last_cart_dir, sizeof(cfg.last_cart_dir), "%s/missing", temp_home);
    cfg.cart_path[0] = '\0';
    ov.row = 5;
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(picker_location[0] == '\0',
          "picker uses system default when recent and selected dirs are absent");
    snprintf(cfg.cart_path, sizeof(cfg.cart_path), "%s/example.crt", disk_b);
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(ov.dialog_kind == OV_DIALOG_CART &&
          strcmp(picker_location, disk_b) == 0,
          "missing recent cartridge directory falls back to selected file");
    cfg.cart_path[0] = '\0';
    snprintf(cfg.last_u36_dir, sizeof(cfg.last_u36_dir), "%s", disk_a);
    ov.row = 6;
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(ov.dialog_kind == OV_DIALOG_U36 &&
          strcmp(picker_location, disk_a) == 0,
          "U36 picker uses its own recent directory");
    cfg.second_drive = false;
    snprintf(cfg.rom_dir, sizeof(cfg.rom_dir), "%s/missing", temp_home);
    ov.section = OV_GENERAL;
    ov.row = 5;
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(picker_was_folder && picker_location[0] == '\0',
          "missing machine-ROM directory uses system folder default");
    snprintf(cfg.rom_dir, sizeof(cfg.rom_dir), "%s", disk_b);
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(ov.dialog_kind == OV_DIALOG_ROM && picker_was_folder &&
          strcmp(picker_location, disk_b) == 0,
          "machine-ROM folder picker starts in selected directory");
    cfg.rom_dir[0] = '\0';
    cfg.last_cart_dir[0] = '\0';
    cfg.last_u36_dir[0] = '\0';
    config_set_defaults(&saved);
    CHECK(config_load(&saved, config_file) &&
          strcmp(saved.last_disk_dir, disk_a) == 0 &&
          strcmp(saved.last_tape_dir, disk_a) == 0,
          "recent picker directories survive configuration reload");

    char prg_file[CONFIG_PATH_MAX];
    snprintf(prg_file, sizeof(prg_file), "%s/overlay-test.prg", temp_home);
    FILE *prg_output = fopen(prg_file, "wb");
    const unsigned char prg_bytes[] = { 0x01, 0x1C, 0x00, 0x00, 0x00 };
    CHECK(prg_output != NULL, "create Overlay PRG fixture");
    if (prg_output) {
        CHECK(fwrite(prg_bytes, 1, sizeof(prg_bytes), prg_output) ==
              sizeof(prg_bytes), "write Overlay PRG fixture");
        fclose(prg_output);
    }
    ov.section = OV_MEDIA;
    ov.row = 1;
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(strstr(picker_filter, "prg;PRG") != NULL,
          "Drive 1 picker includes PRG files");
    snprintf(ov.dialog_path, sizeof(ov.dialog_path), "%s", prg_file);
    ov.dialog_ready = true;
    overlay_tick(&ov);
    CHECK(c->drive.disk_attached && c->drive.image.format == DISK_FORMAT_PRG &&
          strcmp(cfg.disk_path, prg_file) == 0,
          "Drive 1 Overlay selection attaches and persists standalone PRG");
    config_set_defaults(&saved);
    CHECK(config_load(&saved, config_file) &&
          strcmp(saved.disk_path, prg_file) == 0,
          "standalone PRG selection restores from saved configuration");
    key(&ov, SDL_SCANCODE_DELETE);
    CHECK(!c->drive.disk_attached && cfg.disk_path[0] == '\0',
          "Del ejects standalone PRG media");
    cfg.second_drive = true;
    ov.row = 3;
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(strstr(picker_filter, "prg;PRG") != NULL,
          "Drive 2 picker includes PRG files");
    snprintf(ov.dialog_path, sizeof(ov.dialog_path), "%s", prg_file);
    ov.dialog_ready = true;
    overlay_tick(&ov);
    CHECK(c->drive2.disk_attached && c->drive2.image.format == DISK_FORMAT_PRG &&
          strcmp(cfg.disk2_path, prg_file) == 0,
          "Drive 2 Overlay selection attaches standalone PRG independently");
    key(&ov, SDL_SCANCODE_DELETE);
    cfg.second_drive = false;

    /* Cartridge selection is a live hardware change, not just a saved path. */
    char cart_file[CONFIG_PATH_MAX];
    snprintf(cart_file, sizeof(cart_file), "%s/test-cart.bin", temp_home);
    FILE *cart_output = fopen(cart_file, "wb");
    CHECK(cart_output != NULL, "create temporary raw cartridge");
    if (cart_output) {
        unsigned char block[0x2000];
        memset(block, 0x5A, sizeof(block));
        CHECK(fwrite(block, 1, sizeof(block), cart_output) == sizeof(block),
              "write temporary raw cartridge");
        fclose(cart_output);
    }
    CHECK(overlay_set_cartridge(&ov, cart_file), "insert raw cartridge from Media");
    CHECK(c->mem.cart.attached && c->mem.cart.rom[0] == 0x5A &&
          strcmp(cfg.cart_path, cart_file) == 0 && machine_resets == 1,
          "insert maps the cartridge, persists its path, and resets");
    config_set_defaults(&saved);
    CHECK(config_load(&saved, config_file) &&
          strcmp(saved.cart_path, cart_file) == 0,
          "live cartridge path is saved to config");

    CHECK(!overlay_set_cartridge(&ov, "/tmp/1986-missing-cartridge.bin"),
          "invalid replacement reports failure");
    CHECK(!c->mem.cart.attached && cfg.cart_path[0] == '\0' &&
          machine_resets == 2,
          "failed replacement ejects old image and clears saved path");
    CHECK(overlay_set_cartridge(&ov, cart_file), "reattach cartridge");
    ov.section = OV_MEDIA;
    ov.row = 3; /* cartridge when second drive is disabled */
    key(&ov, SDL_SCANCODE_DELETE);
    CHECK(!c->mem.cart.attached && cfg.cart_path[0] == '\0' &&
          machine_resets == 4,
          "Del ejects cartridge and resets the machine");
    config_set_defaults(&saved);
    CHECK(config_load(&saved, config_file) && saved.cart_path[0] == '\0',
          "ejection is persisted");

    char u36_file[CONFIG_PATH_MAX];
    snprintf(u36_file, sizeof(u36_file), "%s/test-u36.rom", temp_home);
    FILE *u36_output = fopen(u36_file, "wb");
    CHECK(u36_output != NULL, "create U36 fixture");
    if (u36_output) {
        unsigned char block[0x2000];
        memset(block, 0x36, sizeof(block));
        CHECK(fwrite(block, 1, sizeof(block), u36_output) == sizeof(block),
              "write 8 KiB U36 fixture");
        fclose(u36_output);
    }
    CHECK(overlay_set_u36(&ov, u36_file), "insert U36 ROM from Media");
    CHECK(c->mem.u36_attached && c->mem.u36_rom[0x6000] == 0x36 &&
          strcmp(cfg.u36_path, u36_file) == 0 && machine_resets == 5,
          "U36 image mirrors, persists, and resets on insert");
    config_set_defaults(&saved);
    CHECK(config_load(&saved, config_file) &&
          strcmp(saved.u36_path, u36_file) == 0,
          "U36 path survives config reload");
    ov.section = OV_MEDIA;
    ov.row = 4; /* U36 when second drive is disabled and Tinker is on */
    key(&ov, SDL_SCANCODE_DELETE);
    CHECK(!c->mem.u36_attached && cfg.u36_path[0] == '\0' &&
          machine_resets == 6, "Del ejects U36 and resets the machine");
    CHECK(overlay_set_u36(&ov, u36_file), "reattach U36 image");
    CHECK(!overlay_set_u36(&ov, "/tmp/1986-missing-u36.rom"),
          "invalid U36 replacement reports failure");
    CHECK(!c->mem.u36_attached && cfg.u36_path[0] == '\0' &&
          machine_resets == 8,
          "failed U36 replacement ejects old image and clears path");
    cfg.tinker = false;
    ov.section = OV_MEDIA;
    ov.row = 0;
    for (int i = 0; i < 8; ++i) key(&ov, SDL_SCANCODE_DOWN);
    CHECK(ov.row == 3, "U36 Media row is hidden without Tinker");
    cfg.tinker = true;

    /* Real-drive hardware type lives in Media, separately for each unit.
     * The fast-drive layout above remains unchanged while the gate is off. */
    ov.section = OV_ADVANCED;
    ov.row = 6;
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(cfg.real_disk_drive, "Advanced enables real-drive preference");
    ov.section = OV_MEDIA;
    ov.row = 1;
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(cfg.drive_type == 1581,
          "Media selects 1581 hardware for drive 1 independently of image");
    CHECK(config_load(&saved, config_file) && saved.drive_type == 1581,
          "drive 1 hardware selection persists");
    ov.section = OV_ADVANCED;
    ov.row = 7;
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(cfg.second_drive, "enable second drive for independent type selection");
    ov.section = OV_MEDIA;
    ov.row = 4;
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(cfg.drive2_type == 1581 && cfg.drive_type == 1581,
          "Media exposes a separate hardware type for drive 2");
    ov.section = OV_ADVANCED;
    ov.row = 6;
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(!cfg.real_disk_drive, "Advanced disables real-drive preference");
    ov.section = OV_MEDIA;
    ov.row = 0;
    for (int i = 0; i < 12; i++) key(&ov, SDL_SCANCODE_DOWN);
    CHECK(ov.row == 6,
          "fast-drive Media layout hides hardware type while gate is off");

    const char *preview = getenv("C128_OVERLAY_PREVIEW");
    if (preview) {
        SDL_Window *window = NULL;
        SDL_Renderer *renderer = NULL;
        CHECK(SDL_Init(SDL_INIT_VIDEO), "initialize SDL for overlay preview");
        CHECK(SDL_CreateWindowAndRenderer("overlay preview", 1536, 1110,
                                         SDL_WINDOW_HIDDEN, &window, &renderer),
              "create overlay preview renderer");
        if (renderer) {
            c->display.window = window;
            if (getenv("C128_OVERLAY_PREVIEW_KEYBOARD")) {
                ov.section = OV_ADVANCED;
                ov.row = 16;
                ov.keyboard_map_visible = true;
            } else if (getenv("C128_OVERLAY_PREVIEW_ABOUT")) {
                ov.section = OV_GENERAL;
                ov.row = 3;
                ov.about_visible = true;
            } else {
                ov.section = OV_ADVANCED;
                ov.row = 7;
            }
            SDL_SetRenderDrawColor(renderer, 0x20, 0x40, 0x20, 255);
            SDL_RenderClear(renderer);
            overlay_render(&ov, renderer);
            SDL_Surface *surface = SDL_RenderReadPixels(renderer, NULL);
            CHECK(surface && SDL_SaveBMP(surface, preview),
                  "save overlay preview image");
            SDL_DestroySurface(surface);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
    }

    unlink(cart_file);
    unlink(prg_file);
    unlink(u36_file);
    unlink(config_file);
    rmdir(disk_a);
    rmdir(disk_b);
    rmdir(temp_home);

    free(c);
    if (failures == 0) { puts("test-overlay: OK"); return 0; }
    printf("test-overlay: %d failure(s)\n", failures);
    return 1;
}
