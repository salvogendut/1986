#include "overlay.h"
#include "leds.h"
#include "notify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

/* Only the keyboard-driven overlay state is exercised here. */
void leds_ping(LedId id) { (void)id; }
void notify_set_mode(NotifyMode mode) { (void)mode; }
void notify_post(const char *fmt, ...) { (void)fmt; }
void display_set_smoothing(Display *d, bool smooth) { (void)d; (void)smooth; }
void display_set_crt(Display *d, bool enabled, int scanlines, int brightness,
                     int contrast, int red, int green, int blue) {
    (void)d; (void)enabled; (void)scanlines; (void)brightness;
    (void)contrast; (void)red; (void)green; (void)blue;
}
void display_set_one_display(Display *d, bool one) { (void)d; (void)one; }
static int machine_resets;
void c128_reset(C128 *c) { (void)c; machine_resets++; }

static void key(Overlay *ov, SDL_Scancode sc) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = sc;
    overlay_handle_event(ov, &event);
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
    drive_set_unit(&c->drive2, cfg.drive2_unit);
    Overlay ov;
    overlay_init(&ov, &cfg, c);

    key(&ov, SDL_SCANCODE_F9);
    key(&ov, SDL_SCANCODE_RIGHT);
    key(&ov, SDL_SCANCODE_RIGHT);
    CHECK(ov.visible && ov.section == OV_ADVANCED,
          "Tinker exposes the Advanced section");
    for (int i = 0; i < 6; ++i) key(&ov, SDL_SCANCODE_DOWN);
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(cfg.second_drive, "Second Drive toggle enables the extra device");

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
    CHECK(ov.row == 5, "enabled Media section includes Drive 2 image row");

    key(&ov, SDL_SCANCODE_RIGHT);
    for (int i = 0; i < 6; ++i) key(&ov, SDL_SCANCODE_DOWN);
    key(&ov, SDL_SCANCODE_RETURN);
    CHECK(!cfg.second_drive, "Second Drive toggle disables the extra device");
    key(&ov, SDL_SCANCODE_LEFT);
    for (int i = 0; i < 8; ++i) key(&ov, SDL_SCANCODE_DOWN);
    CHECK(ov.row == 3, "disabled Media section hides both Drive 2 rows");

    key(&ov, SDL_SCANCODE_LEFT);
    CHECK(ov.section == OV_GENERAL && ov.row == 0,
          "General opens with first selectable row");
    key(&ov, SDL_SCANCODE_DOWN);
    key(&ov, SDL_SCANCODE_DOWN);
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

    /* Cartridge selection is a live hardware change, not just a saved path.
     * Isolate configuration writes from the actual user profile. */
    char temp_home[] = "/tmp/1986-overlay-test-XXXXXX";
    CHECK(mkdtemp(temp_home) != NULL, "create temporary config home");
    char config_file[CONFIG_PATH_MAX];
    snprintf(config_file, sizeof(config_file), "%s/1986.conf", temp_home);
    setenv("C128_CONFIG_PATH", config_file, 1);
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
    Config saved;
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
            if (getenv("C128_OVERLAY_PREVIEW_ABOUT")) {
                ov.section = OV_GENERAL;
                ov.row = 2;
                ov.about_visible = true;
            } else {
                ov.section = OV_ADVANCED;
                ov.row = 6;
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
    unlink(config_file);
    rmdir(temp_home);

    free(c);
    if (failures == 0) { puts("test-overlay: OK"); return 0; }
    printf("test-overlay: %d failure(s)\n", failures);
    return 1;
}
