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

    int mw, mh, vw, vh;
    SDL_GetWindowSize(d->window, &mw, &mh);
    SDL_GetWindowSize(d->vdc_window, &vw, &vh);
    display_upload(d);
    display_render_function_keys(d);
    int ok = mw == WINDOW_W && mh == WINDOW_H_TOTAL &&
             vw == VDC_SCREEN_W && vh == VDC_SCREEN_H + FUNCTION_KEY_BAR_HEIGHT &&
             band_pixel(d->renderer, 2, mh - LED_BAR_HEIGHT - 2) &&
             band_pixel(d->vdc_renderer, 2, vh - 2);
    display_set_one_display(d, true);
    display_set_vdc_active(d, true);
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
