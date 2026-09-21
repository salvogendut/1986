#include "display.h"
#include "leds.h"
#include <stdio.h>
#include <stdlib.h>

/* This test only needs the reserved strip, not the activity LEDs. */
void leds_render(SDL_Renderer *r, int x, int y, int w, int h) {
    (void)r; (void)x; (void)y; (void)w; (void)h;
}

static int band_pixel(SDL_Renderer *r, int x, int y) {
    SDL_Surface *surface = SDL_RenderReadPixels(r, NULL);
    if (!surface) return 0;
    Uint8 red, green, blue, alpha;
    bool ok = SDL_ReadSurfacePixel(surface, x, y,
                                   &red, &green, &blue, &alpha);
    SDL_DestroySurface(surface);
    return ok && red == 0x10 && green == 0x10 && blue == 0x14;
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
             vw == VDC_SCREEN_W && vh == VDC_SCREEN_H + FUNCTION_KEY_BAR_HEIGHT &&
             band_pixel(d->renderer, 2, mh - LED_BAR_HEIGHT - 2) &&
             band_pixel(d->vdc_renderer, 2, vh - 2);

    display_set_scale(d, 2);
    SDL_GetWindowSize(d->window, &mw, &mh);
    SDL_GetWindowSize(d->vdc_window, &vw, &vh);
    ok = ok && d->scale == 2 &&
         mw == WINDOW_W * 2 &&
         mh == WINDOW_H * 2 + FUNCTION_KEY_BAR_HEIGHT + LED_BAR_HEIGHT &&
         vw == VDC_SCREEN_W * 2 &&
         vh == VDC_SCREEN_H * 2 + FUNCTION_KEY_BAR_HEIGHT;
    display_upload(d);
    display_render_function_keys(d);
    ok = ok && band_pixel(d->renderer, 2, mh - LED_BAR_HEIGHT - 2) &&
         band_pixel(d->vdc_renderer, 2, vh - 2);

    display_set_one_display(d, true);
    display_set_vdc_active(d, true);
    ok = ok && display_active_renderer(d) == d->renderer;
    display_set_one_display(d, false);
    SDL_GetWindowSize(d->vdc_window, &vw, &vh);
    ok = ok && display_active_renderer(d) == d->vdc_renderer &&
         vw == VDC_SCREEN_W * 2 &&
         vh == VDC_SCREEN_H * 2 + FUNCTION_KEY_BAR_HEIGHT;
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
