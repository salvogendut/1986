#include "leds.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static bool text_is(SDL_Renderer *renderer, SDL_Surface *actual,
                    int x, int y, const char *text) {
    SDL_SetRenderDrawColor(renderer, 18, 18, 18, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 205, 205, 205, 255);
    SDL_RenderDebugText(renderer, (float)x, (float)y, text);
    SDL_Surface *expected = SDL_RenderReadPixels(renderer, NULL);
    bool ok = actual && expected && !areas_differ(actual, expected, x, y,
                    (int)strlen(text) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE,
                    SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);
    SDL_DestroySurface(expected);
    return ok;
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

    leds_set_z80_frequency(4);
    leds_render(renderer, 0, 0, 384, LED_BAR_H);
    SDL_Surface *four_mhz_z80 = SDL_RenderReadPixels(renderer, NULL);

    /* With both drives enabled, the four indicators occupy 368 pixels and
     * remain inside the 384-pixel VIC window at scale 1. CPU lamp positions
     * are therefore fixed at x=184 and x=288 for this worst-case layout. */
    bool ok = one_mhz && two_mhz && four_mhz_z80 &&
              pixel_is(two_mhz, 185, 7, 255, 255, 255) &&
              pixel_is(two_mhz, 289, 7, 80, 150, 255) &&
              areas_differ(one_mhz, two_mhz, 204, 7, 72, 8) &&
              areas_differ(two_mhz, four_mhz_z80, 308, 7, 68, 8);

    /* Model and unit rows fit beside each lamp without stealing CPU space. */
    leds_set_drive_type(LED_FDC_A, 1571);
    leds_set_drive_type(LED_FDC_B, 1581);
    leds_set_drive_unit(LED_FDC_A, 10);
    leds_set_drive_unit(LED_FDC_B, 11);
    leds_render(renderer, 0, 0, 384, LED_BAR_H);
    SDL_Surface *mixed = SDL_RenderReadPixels(renderer, NULL);
    ok = text_is(renderer, mixed, 28, 2, "D1 1571") && ok;
    ok = text_is(renderer, mixed, 116, 2, "D2 1581") && ok;
    ok = text_is(renderer, mixed, 28, 12, "#10") && ok;
    ok = text_is(renderer, mixed, 116, 12, "#11") && ok;

    leds_set_drive_type(LED_FDC_A, 1581);
    leds_set_drive_type(LED_FDC_B, 1571);
    leds_render(renderer, 0, 0, 384, LED_BAR_H);
    SDL_Surface *swapped = SDL_RenderReadPixels(renderer, NULL);
    ok = text_is(renderer, swapped, 28, 2, "D1 1581") && ok;
    ok = text_is(renderer, swapped, 116, 2, "D2 1571") && ok;

    leds_set_drive_type(LED_FDC_A, 0);
    leds_set_drive_type(LED_FDC_B, 0);
    leds_render(renderer, 0, 0, 384, LED_BAR_H);
    SDL_Surface *fast = SDL_RenderReadPixels(renderer, NULL);
    ok = text_is(renderer, fast, 28, 2, "D1 FAST") && ok;
    ok = text_is(renderer, fast, 116, 2, "D2 FAST") && ok;

    SDL_DestroySurface(mixed);
    SDL_DestroySurface(swapped);
    SDL_DestroySurface(fast);
    SDL_DestroySurface(one_mhz);
    SDL_DestroySurface(two_mhz);
    SDL_DestroySurface(four_mhz_z80);
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
