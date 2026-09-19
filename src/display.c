#include "display.h"
#include "leds.h"
#include <string.h>
#include <stdio.h>

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static unsigned adjust_component(unsigned c, int brightness, int contrast,
                                 int gain) {
    int v = 128 + (((int)c - 128) * contrast + 50) / 100;
    v = (v * brightness + 50) / 100;
    v = (v * gain + 50) / 100;
    return (unsigned)clamp_int(v, 0, 255);
}

static const u32 *display_crt_pixels(Display *d) {
    if (!d->crt_enabled ||
        (d->crt_brightness == DISPLAY_CRT_BRIGHTNESS_DEFAULT &&
         d->crt_contrast == DISPLAY_CRT_CONTRAST_DEFAULT &&
         d->crt_red == DISPLAY_CRT_RGB_DEFAULT &&
         d->crt_green == DISPLAY_CRT_RGB_DEFAULT &&
         d->crt_blue == DISPLAY_CRT_RGB_DEFAULT))
        return d->pixels;

    int n = C128_SCREEN_W * C128_SCREEN_H;
    for (int i = 0; i < n; i++) {
        u32 px = d->pixels[i];
        unsigned r = adjust_component((px >> 16) & 0xFF,
                                      d->crt_brightness, d->crt_contrast, d->crt_red);
        unsigned g = adjust_component((px >> 8) & 0xFF,
                                      d->crt_brightness, d->crt_contrast, d->crt_green);
        unsigned b = adjust_component(px & 0xFF,
                                      d->crt_brightness, d->crt_contrast, d->crt_blue);
        d->crt_pixels[i] = (px & 0xFF000000u) | (r << 16) | (g << 8) | b;
    }
    return d->crt_pixels;
}

int display_init(Display *d, const char *title, int scale) {
    memset(d, 0, sizeof(*d));

    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    int win_w = WINDOW_W * scale;
    int win_h = WINDOW_H * scale + LED_BAR_HEIGHT;

    d->window = SDL_CreateWindow(title, win_w, win_h, SDL_WINDOW_RESIZABLE);
    if (!d->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
    d->renderer = SDL_CreateRenderer(d->window, NULL);
    if (!d->renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetRenderVSync(d->renderer, 0); /* pacing is done by the 50 Hz software pacer */

    d->texture = SDL_CreateTexture(d->renderer,
        SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
        C128_SCREEN_W, C128_SCREEN_H);
    if (!d->texture) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return -1;
    }

    memset(d->pixels, 0, sizeof(d->pixels));
    d->crt_scanlines = DISPLAY_CRT_SCANLINES_DEFAULT;
    d->crt_brightness = DISPLAY_CRT_BRIGHTNESS_DEFAULT;
    d->crt_contrast = DISPLAY_CRT_CONTRAST_DEFAULT;
    d->crt_red = DISPLAY_CRT_RGB_DEFAULT;
    d->crt_green = DISPLAY_CRT_RGB_DEFAULT;
    d->crt_blue = DISPLAY_CRT_RGB_DEFAULT;
    return 0;
}

void display_set_smoothing(Display *d, bool smooth) {
    if (d->texture)
        SDL_SetTextureScaleMode(d->texture,
            smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
}

void display_set_crt(Display *d, bool enabled, int scanlines, int brightness,
                     int contrast, int red, int green, int blue) {
    d->crt_enabled = enabled;
    d->crt_scanlines = clamp_int(scanlines, 0, 95);
    d->crt_brightness = clamp_int(brightness, 50, 100);
    d->crt_contrast = clamp_int(contrast, 50, 150);
    d->crt_red = clamp_int(red, 50, 150);
    d->crt_green = clamp_int(green, 50, 150);
    d->crt_blue = clamp_int(blue, 50, 150);
}

void display_destroy(Display *d) {
    if (d->texture)  SDL_DestroyTexture(d->texture);
    if (d->renderer) SDL_DestroyRenderer(d->renderer);
    if (d->window)   SDL_DestroyWindow(d->window);
}

void display_put_pixel(Display *d, u32 rgb) {
    if (d->scan_x < C128_SCREEN_W && d->scan_y < C128_SCREEN_H) {
        int off = d->scan_y * C128_SCREEN_W + d->scan_x;
        d->pixels[off] = rgb;
        d->touched[off] = 1;
    }
    d->scan_x++;
}

void display_next_line(Display *d) {
    d->scan_x = 0;
    d->scan_y++;
}

void display_vsync(Display *d) {
    d->scan_x = 0;
    d->scan_y = 0;
}

void display_finalize_frame(Display *d, u32 blank) {
    int n = C128_SCREEN_W * C128_SCREEN_H;
    for (int i = 0; i < n; i++) {
        if (!d->touched[i]) d->pixels[i] = blank;
        d->touched[i] = 0;
    }
}

void display_upload(Display *d) {
    int ww, wh;
    SDL_GetWindowSize(d->window, &ww, &wh);

    int bar_h = LED_BAR_HEIGHT;
    if (bar_h > wh / 4) bar_h = wh / 4;
    int area_h = wh - bar_h;
    if (area_h < 1) area_h = 1;

    /* Fit the 384x272 VIC-II screen (with border) into (ww x area_h),
     * maintaining the WINDOW_W:WINDOW_H aspect ratio. */
    int dst_w = ww;
    int dst_h = area_h;
    if (dst_w * WINDOW_H > dst_h * WINDOW_W)
        dst_w = dst_h * WINDOW_W / WINDOW_H;
    else
        dst_h = dst_w * WINDOW_H / WINDOW_W;
    SDL_FRect dst = {
        (float)(ww - dst_w) / 2,
        (float)(area_h - dst_h) / 2,
        (float)dst_w,
        (float)dst_h
    };

    SDL_UpdateTexture(d->texture, NULL, display_crt_pixels(d),
                      C128_SCREEN_W * sizeof(u32));
    SDL_SetTextureColorMod(d->texture, 255, 255, 255);
    SDL_SetRenderDrawColor(d->renderer, 0, 0, 0, 255);
    SDL_RenderClear(d->renderer);
    SDL_RenderTexture(d->renderer, d->texture, NULL, &dst);
    if (d->crt_enabled && d->crt_scanlines > 0) {
        Uint8 alpha = (Uint8)((d->crt_scanlines * 255 + 50) / 100);
        SDL_SetRenderDrawBlendMode(d->renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(d->renderer, 0, 0, 0, alpha);
        int lines = (int)dst.h;
        for (int y = 1; y < lines; y += 2) {
            SDL_FRect scan = { dst.x, dst.y + (float)y, dst.w, 1.0f };
            SDL_RenderFillRect(d->renderer, &scan);
        }
    }
    leds_render(d->renderer, 0, wh - bar_h, ww, bar_h);
}

void display_flip(Display *d) {
    SDL_RenderPresent(d->renderer);
}

void display_apply_greyscale(Display *d) {
    u32 *p = d->pixels;
    int  n = C128_SCREEN_W * C128_SCREEN_H;
    for (int i = 0; i < n; i++) {
        u32 px = p[i];
        unsigned r = (px >> 16) & 0xFF;
        unsigned g = (px >>  8) & 0xFF;
        unsigned b =  px        & 0xFF;
        unsigned y = (306u * r + 601u * g + 117u * b) >> 10;
        unsigned dim = 32 + ((y * 64) >> 8);
        if (dim > 255) dim = 255;
        p[i] = (dim << 16) | (dim << 8) | dim;
    }
}

void display_draw_paused_label(Display *d) {
    int ww, wh;
    SDL_GetWindowSize(d->window, &ww, &wh);
    float big   = (float)ww / 320.0f;
    float small = (float)ww / 640.0f;
    if (big   < 2.0f) big   = 2.0f;
    if (small < 1.0f) small = 1.0f;

    const char *main_txt = "PAUSED";
    const char *hint_txt = "Press F10 to resume";

    SDL_SetRenderDrawBlendMode(d->renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(d->renderer, 0xFF, 0xFF, 0xFF, 0xFF);

    SDL_SetRenderScale(d->renderer, big, big);
    int main_w = (int)(strlen(main_txt) * 8);
    float main_x = (ww / big - main_w) / 2.0f;
    float main_y = (wh / big) / 2.0f - 12.0f;
    SDL_RenderDebugText(d->renderer, main_x, main_y, main_txt);

    SDL_SetRenderScale(d->renderer, small, small);
    int hint_w = (int)(strlen(hint_txt) * 8);
    float hint_x = (ww / small - hint_w) / 2.0f;
    float hint_y = (wh / small) / 2.0f + 12.0f * (big / small);
    SDL_RenderDebugText(d->renderer, hint_x, hint_y, hint_txt);

    SDL_SetRenderScale(d->renderer, 1.0f, 1.0f);
}

void display_save_ppm(Display *d, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", C128_SCREEN_W, C128_SCREEN_H);
    for (int i = 0; i < C128_SCREEN_W * C128_SCREEN_H; i++) {
        u32 px = d->pixels[i];
        unsigned char rgb[3] = {
            (unsigned char)((px >> 16) & 0xFF),
            (unsigned char)((px >>  8) & 0xFF),
            (unsigned char)( px        & 0xFF),
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

u32 display_hash(Display *d) {
    const u32 *px = display_crt_pixels(d);
    u32 h = 2166136261u;
    int n = C128_SCREEN_W * C128_SCREEN_H;
    for (int i = 0; i < n; i++) {
        h ^= px[i];
        h *= 16777619u;
    }
    return h;
}
