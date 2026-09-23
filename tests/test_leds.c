#include "leds.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

static bool pixel_is(SDL_Surface *surface, int x, int y,
                     Uint8 wanted_r, Uint8 wanted_g, Uint8 wanted_b) {
    Uint8 r, g, b, a;
    return SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a) &&
           r == wanted_r && g == wanted_g && b == wanted_b;
}

static bool areas_differ(SDL_Surface *a, SDL_Surface *b,
                         int x, int y, int w, int h) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            Uint8 ar, ag, ab, aa, br, bg, bb, ba;
            if (!SDL_ReadSurfacePixel(a, px, py, &ar, &ag, &ab, &aa) ||
                !SDL_ReadSurfacePixel(b, px, py, &br, &bg, &bb, &ba))
                return false;
            if (ar != br || ag != bg || ab != bb)
                return true;
        }
    }
    return false;
}

int main(void) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    if (!SDL_CreateWindowAndRenderer("LED test", 384, LED_BAR_H,
                                     SDL_WINDOW_HIDDEN,
                                     &window, &renderer)) {
        fprintf(stderr, "SDL renderer: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    leds_set_enabled(LED_FDC_A, true);
    leds_set_enabled(LED_FDC_B, true);
    leds_set_enabled(LED_CPU_8502, true);
    leds_set_enabled(LED_CPU_Z80, true);
    SDL_Delay(2); /* Ensure an activity timestamp cannot be zero. */
    leds_ping(LED_CPU_8502);
    leds_ping(LED_CPU_Z80);
    leds_set_cpu_frequency(1);
    leds_render(renderer, 0, 0, 384, LED_BAR_H);
    SDL_Surface *one_mhz = SDL_RenderReadPixels(renderer, NULL);

    leds_set_cpu_frequency(2);
    leds_render(renderer, 0, 0, 384, LED_BAR_H);
    SDL_Surface *two_mhz = SDL_RenderReadPixels(renderer, NULL);

    /* With both drives enabled, the four indicators occupy 368 pixels and
     * remain inside the 384-pixel VIC window at scale 1. CPU lamp positions
     * are therefore fixed at x=184 and x=288 for this worst-case layout. */
    bool ok = one_mhz && two_mhz &&
              pixel_is(two_mhz, 185, 7, 255, 255, 255) &&
              pixel_is(two_mhz, 289, 7, 80, 150, 255) &&
              areas_differ(one_mhz, two_mhz, 204, 7, 72, 8);

    SDL_DestroySurface(one_mhz);
    SDL_DestroySurface(two_mhz);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (!ok) {
        fputs("test-leds: FAIL\n", stderr);
        return 1;
    }
    puts("test-leds: OK");
    return 0;
}
