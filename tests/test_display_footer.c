#include "display.h"
#include "leds.h"
#include <stdio.h>
#include <stdlib.h>

/* Give the activity strip a distinct colour so both windows can be checked. */
static int led_bar_renders;
void leds_render(SDL_Renderer *r, int x, int y, int w, int h) {
    led_bar_renders++;
    SDL_SetRenderDrawColor(r, 0x22, 0x33, 0x44, 255);
    SDL_FRect bar = { (float)x, (float)y, (float)w, (float)h };
    SDL_RenderFillRect(r, &bar);
}

static int colour_pixel(SDL_Renderer *r, int x, int y, Uint8 wanted_red,
                        Uint8 wanted_green, Uint8 wanted_blue) {
    SDL_Surface *surface = SDL_RenderReadPixels(r, NULL);
    if (!surface) return 0;
    Uint8 red, green, blue, alpha;
    bool ok = SDL_ReadSurfacePixel(surface, x, y,
                                   &red, &green, &blue, &alpha);
    SDL_DestroySurface(surface);
    return ok && red == wanted_red && green == wanted_green &&
           blue == wanted_blue;
}

static int band_pixel(SDL_Renderer *r, int x, int y) {
    return colour_pixel(r, x, y, 0x10, 0x10, 0x14);
}

static int led_pixel(SDL_Renderer *r, int x, int y) {
    return colour_pixel(r, x, y, 0x22, 0x33, 0x44);
}

int main(void) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL init: %s\n", SDL_GetError());
        return 1;
    }
    Display *d = calloc(1, sizeof(*d));
    if (!d || display_init(d, "footer test", 1) != 0) return 1;
    display_set_one_display(d, false);
    if (!d->vdc_renderer) return 1;
    int active_ok = display_active_renderer(d) == d->renderer;
    display_set_vdc_active(d, true);
    active_ok = active_ok && display_active_renderer(d) == d->vdc_renderer;
    SDL_HideWindow(d->vdc_window);
    display_focus_active(d);
    active_ok = active_ok && !(SDL_GetWindowFlags(d->vdc_window) & SDL_WINDOW_HIDDEN);
    display_set_vdc_active(d, false);
    active_ok = active_ok && display_active_renderer(d) == d->renderer;
    SDL_HideWindow(d->window);
    display_focus_active(d);
    active_ok = active_ok && !(SDL_GetWindowFlags(d->window) & SDL_WINDOW_HIDDEN);

    int mw, mh, vw, vh;
    SDL_GetWindowSize(d->window, &mw, &mh);
    SDL_GetWindowSize(d->vdc_window, &vw, &vh);
    display_upload(d);
    display_render_function_keys(d);
    int ok = active_ok && mw == WINDOW_W && mh == WINDOW_H_TOTAL &&
             vw == VDC_SCREEN_W && vh == VDC_SCREEN_H + FUNCTION_KEY_BAR_HEIGHT + LED_BAR_HEIGHT &&
             band_pixel(d->renderer, 2, mh - LED_BAR_HEIGHT - 2) &&
             band_pixel(d->vdc_renderer, 2, vh - LED_BAR_HEIGHT - 2) &&
             led_pixel(d->renderer, 2, mh - 2) &&
             led_pixel(d->vdc_renderer, 2, vh - 2) &&
             led_bar_renders == 2;

    display_set_scale(d, 2);
    SDL_GetWindowSize(d->window, &mw, &mh);
    SDL_GetWindowSize(d->vdc_window, &vw, &vh);
    ok = ok && d->scale == 2 &&
         mw == WINDOW_W * 2 &&
         mh == WINDOW_H * 2 + FUNCTION_KEY_BAR_HEIGHT + LED_BAR_HEIGHT &&
         vw == VDC_SCREEN_W * 2 &&
         vh == VDC_SCREEN_H * 2 + FUNCTION_KEY_BAR_HEIGHT + LED_BAR_HEIGHT;
    display_upload(d);
    display_render_function_keys(d);
    ok = ok && band_pixel(d->renderer, 2, mh - LED_BAR_HEIGHT - 2) &&
         band_pixel(d->vdc_renderer, 2, vh - LED_BAR_HEIGHT - 2) &&
         led_pixel(d->renderer, 2, mh - 2) &&
         led_pixel(d->vdc_renderer, 2, vh - 2) &&
         led_bar_renders == 4;

    display_set_one_display(d, true);
    display_set_vdc_active(d, true);
    ok = ok && display_active_renderer(d) == d->renderer;
    display_set_one_display(d, false);
    SDL_GetWindowSize(d->vdc_window, &vw, &vh);
    ok = ok && display_active_renderer(d) == d->vdc_renderer &&
         vw == VDC_SCREEN_W * 2 &&
         vh == VDC_SCREEN_H * 2 + FUNCTION_KEY_BAR_HEIGHT + LED_BAR_HEIGHT;
    display_set_one_display(d, true);
    display_upload(d);
    display_render_function_keys(d);
    ok = ok && !d->vdc_renderer &&
         band_pixel(d->renderer, 2, mh - LED_BAR_HEIGHT - 2);
    display_destroy(d);
    free(d);
    SDL_Quit();
    if (!ok) { fputs("test-display-footer: FAIL\n", stderr); return 1; }
    puts("test-display-footer: OK");
    return 0;
}
