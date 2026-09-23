/* Host presentation hooks for the SDL-free browser build. */
#include <stdarg.h>
#include <string.h>
#include "display.h"
#include "leds.h"
#include "notify.h"

static unsigned g_activity;
static unsigned char g_decay[LED_COUNT];
int g_debug_enabled;

unsigned web_led_mask(void) { return g_activity; }
void web_led_tick(void) {
    for (int i = 0; i < LED_COUNT; ++i) {
        if (g_decay[i] && --g_decay[i] == 0) g_activity &= ~(1u << i);
    }
}

void leds_set_enabled(LedId id, bool enabled) { (void)id; (void)enabled; }
void leds_set_drive_unit(LedId id, int unit) { (void)id; (void)unit; }
void leds_set_cpu_frequency(unsigned mhz) { (void)mhz; }
void leds_set_z80_frequency(unsigned mhz) { (void)mhz; }
void leds_ping(LedId id) {
    if ((unsigned)id < LED_COUNT) {
        g_activity |= 1u << id;
        g_decay[id] = 7;
    }
}
void leds_ping_split(LedId id, bool tx) { (void)tx; leds_ping(id); }
void leds_ping_m4_disk(void) {}
void leds_ping_m4_net(void) {}
void leds_set_mouse_position(int x, int y, bool inside) {
    (void)x; (void)y; (void)inside;
}

void notify_init(void) {}
void notify_set_mode(NotifyMode mode) { (void)mode; }
void notify_post(const char *fmt, ...) { (void)fmt; }
void notify_tick(int dt_ms) { (void)dt_ms; }

int display_init(Display *d, const char *title, int scale) {
    (void)title; (void)scale; memset(d, 0, sizeof(*d)); return 0;
}
void display_destroy(Display *d) { (void)d; }
void display_put_pixel(Display *d, u32 rgb) { (void)d; (void)rgb; }
void display_next_line(Display *d) { (void)d; }
void display_vsync(Display *d) { (void)d; }
void display_finalize_frame(Display *d, u32 blank) { (void)d; (void)blank; }
void display_upload(Display *d) { (void)d; }
void display_render_function_keys(Display *d) { (void)d; }
void display_flip(Display *d) { (void)d; }
void display_save_ppm(Display *d, const char *path) { (void)d; (void)path; }
void display_save_ppm_active(Display *d, const char *path) { (void)d; (void)path; }
u32 display_hash(Display *d) { (void)d; return 0; }
void display_set_smoothing(Display *d, bool smooth) { (void)d; (void)smooth; }
void display_set_crt(Display *d, bool enabled, int scanlines, int brightness,
                     int contrast, int red, int green, int blue) {
    (void)d; (void)enabled; (void)scanlines; (void)brightness;
    (void)contrast; (void)red; (void)green; (void)blue;
}
void display_set_scale(Display *d, int scale) { (void)d; (void)scale; }
void display_set_one_display(Display *d, bool one) { d->one_display = one; }
void display_set_vdc_active(Display *d, bool active) { d->vdc_active = active; }
void display_focus_active(Display *d) { (void)d; }
SDL_Renderer *display_active_renderer(const Display *d) { (void)d; return NULL; }
bool display_vdc_window_open(const Display *d) { (void)d; return false; }
void display_apply_greyscale(Display *d) { (void)d; }
void display_draw_paused_label(Display *d) { (void)d; }
