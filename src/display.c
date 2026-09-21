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
    d->scale = scale;
    int win_w = WINDOW_W * scale;
    int win_h = WINDOW_H * scale + FUNCTION_KEY_BAR_HEIGHT + LED_BAR_HEIGHT;

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

    d->vdc_texture = SDL_CreateTexture(d->renderer,
        SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
        VDC_SCREEN_W, VDC_SCREEN_H);
    if (!d->vdc_texture) {
        fprintf(stderr, "SDL_CreateTexture (VDC): %s\n", SDL_GetError());
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

void display_set_scale(Display *d, int scale) {
    d->scale = clamp_int(scale, 1, 4);
    if (d->window)
        SDL_SetWindowSize(d->window, WINDOW_W * d->scale,
                          WINDOW_H * d->scale + FUNCTION_KEY_BAR_HEIGHT + LED_BAR_HEIGHT);
    if (d->vdc_window)
        SDL_SetWindowSize(d->vdc_window, VDC_SCREEN_W * d->scale,
                          VDC_SCREEN_H * d->scale + FUNCTION_KEY_BAR_HEIGHT);
}

/* Create or destroy the separate VDC (80-column) window used in two-window
 * mode. The VDC shares the main window when one_display is true. */
void display_set_one_display(Display *d, bool one) {
    d->one_display = one;
    if (one) {
        if (d->vdc_window_texture) SDL_DestroyTexture(d->vdc_window_texture);
        if (d->vdc_renderer) SDL_DestroyRenderer(d->vdc_renderer);
        if (d->vdc_window)   SDL_DestroyWindow(d->vdc_window);
        d->vdc_window_texture = NULL;
        d->vdc_renderer = NULL;
        d->vdc_window   = NULL;
        return;
    }
    if (d->vdc_window) return;   /* already open */
    d->vdc_window = SDL_CreateWindow("1986 — VDC 8563 (80-column)",
                                     VDC_SCREEN_W * d->scale,
                                     VDC_SCREEN_H * d->scale + FUNCTION_KEY_BAR_HEIGHT,
                                     SDL_WINDOW_RESIZABLE);
    if (!d->vdc_window) {
        fprintf(stderr, "SDL_CreateWindow (VDC): %s\n", SDL_GetError());
        return;
    }
    d->vdc_renderer = SDL_CreateRenderer(d->vdc_window, NULL);
    if (!d->vdc_renderer) {
        fprintf(stderr, "SDL_CreateRenderer (VDC): %s\n", SDL_GetError());
        SDL_DestroyWindow(d->vdc_window);
        d->vdc_window = NULL;
        return;
    }
    d->vdc_window_texture = SDL_CreateTexture(d->vdc_renderer,
        SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
        VDC_SCREEN_W, VDC_SCREEN_H);
    if (!d->vdc_window_texture) {
        fprintf(stderr, "SDL_CreateTexture (VDC window): %s\n", SDL_GetError());
        SDL_DestroyRenderer(d->vdc_renderer);
        SDL_DestroyWindow(d->vdc_window);
        d->vdc_renderer = NULL;
        d->vdc_window = NULL;
    }
}

void display_set_vdc_active(Display *d, bool active) {
    d->vdc_active = active;
}

void display_focus_active(Display *d) {
    SDL_Window *target = d->window;
    if (!d->one_display && d->vdc_active && d->vdc_window)
        target = d->vdc_window;
    if (target) SDL_RaiseWindow(target);
}

SDL_Renderer *display_active_renderer(const Display *d) {
    if (!d->one_display && d->vdc_active && d->vdc_renderer)
        return d->vdc_renderer;
    return d->renderer;
}

bool display_vdc_window_open(const Display *d) {
    return d->vdc_window != NULL;
}

void display_destroy(Display *d) {
    display_set_one_display(d, true);   /* destroy the VDC window if open */
    if (d->vdc_texture) SDL_DestroyTexture(d->vdc_texture);
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

/* Render a texture into an area (aw x ah), centred, preserving aspect ratio.
 * Clears to black first. */
static void blit_fit(SDL_Renderer *r, SDL_Texture *tex, int tw, int th,
                     int aw, int ah) {
    int dw = aw, dh = ah;
    if (dw * th > dh * tw) dw = dh * tw / th;
    else dh = dw * th / tw;
    SDL_FRect dst = {
        (float)(aw - dw) / 2, (float)(ah - dh) / 2,
        (float)dw, (float)dh
    };
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_RenderTexture(r, tex, NULL, &dst);
}

/* Draw the CRT scanline effect over the (dst) VIC rect. */
static void blit_scanlines(SDL_Renderer *r, const Display *d, const SDL_FRect *dst) {
    if (!d->crt_enabled || d->crt_scanlines <= 0) return;
    Uint8 alpha = (Uint8)((d->crt_scanlines * 255 + 50) / 100);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, alpha);
    int lines = (int)dst->h;
    for (int y = 1; y < lines; y += 2) {
        SDL_FRect scan = { dst->x, dst->y + (float)y, dst->w, 1.0f };
        SDL_RenderFillRect(r, &scan);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

void display_upload(Display *d) {
    int ww, wh;
    SDL_GetWindowSize(d->window, &ww, &wh);

    int bar_h = LED_BAR_HEIGHT;
    if (bar_h > wh / 4) bar_h = wh / 4;
    int footer_h = FUNCTION_KEY_BAR_HEIGHT;
    if (footer_h > (wh - bar_h) / 4) footer_h = (wh - bar_h) / 4;
    int area_h = wh - bar_h - footer_h;
    if (area_h < 1) area_h = 1;

    SDL_UpdateTexture(d->texture, NULL, display_crt_pixels(d),
                      C128_SCREEN_W * sizeof(u32));
    SDL_UpdateTexture(d->vdc_texture, NULL, d->vdc_pixels,
                      VDC_SCREEN_W * sizeof(u32));

    if (d->one_display) {
        /* One window: show whichever of VIC/VDC is active, full brightness. */
        bool show_vdc = d->vdc_active;
        SDL_Texture *tex   = show_vdc ? d->vdc_texture : d->texture;
        int          tex_w = show_vdc ? VDC_SCREEN_W : C128_SCREEN_W;
        int          tex_h = show_vdc ? VDC_SCREEN_H : C128_SCREEN_H;
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        blit_fit(d->renderer, tex, tex_w, tex_h, ww, area_h);
        if (!show_vdc) {
            SDL_FRect dst = { 0, 0, (float)ww, (float)area_h };
            blit_scanlines(d->renderer, d, &dst);
        }
        leds_render(d->renderer, 0, wh - bar_h, ww, bar_h);
        return;
    }

    /* Two windows: VIC in the main window, VDC in a second. The inactive
     * output is dimmed ("sleeping"). */
    int vmod = d->vdc_active ? 80 : 255;   /* VIC sleeps while VDC is active */
    SDL_SetTextureColorMod(d->texture, vmod, vmod, vmod);
    blit_fit(d->renderer, d->texture, C128_SCREEN_W, C128_SCREEN_H, ww, area_h);
    if (!d->vdc_active) {
        SDL_FRect dst = { 0, 0, (float)ww, (float)area_h };
        blit_scanlines(d->renderer, d, &dst);
    }
    leds_render(d->renderer, 0, wh - bar_h, ww, bar_h);

    if (d->vdc_window) {
        int vw, vh;
        SDL_GetWindowSize(d->vdc_window, &vw, &vh);
        SDL_UpdateTexture(d->vdc_window_texture, NULL, d->vdc_pixels,
                          VDC_SCREEN_W * sizeof(u32));
        int vmod2 = d->vdc_active ? 255 : 80;   /* VDC sleeps while VIC is active */
        SDL_SetTextureColorMod(d->vdc_window_texture, vmod2, vmod2, vmod2);
        blit_fit(d->vdc_renderer, d->vdc_window_texture,
                 VDC_SCREEN_W, VDC_SCREEN_H, vw,
                 vh > FUNCTION_KEY_BAR_HEIGHT ? vh - FUNCTION_KEY_BAR_HEIGHT : 1);
    }
}

/* The host controls are always visible, even while the options overlay is
 * open. Shrink only the text if a user resizes a window below its default
 * width; the emulated picture keeps its own reserved space above this bar. */
static void render_function_keys(SDL_Renderer *r, int bottom_reserved) {
    int rw, rh;
    if (!SDL_GetRenderOutputSize(r, &rw, &rh)) return;
    int y = rh - bottom_reserved - FUNCTION_KEY_BAR_HEIGHT;
    if (rw <= 0 || y < 0) return;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0x10, 0x10, 0x14, 255);
    SDL_FRect band = { 0, (float)y, (float)rw, FUNCTION_KEY_BAR_HEIGHT };
    SDL_RenderFillRect(r, &band);

    const char *model = "C128DCR";
    const char *keys = rw >= 1050
        ? "  F1=joy port  F4=screenshot  F5=reset  F6=GIF  F7=pause  F8=monitor  F9=options  F10=40/80  F11=fullscreen  F12=quit"
        : "  F1 Joy F4 Shot F5 Reset F6 GIF F7 Pause F8 Mon F9 Opt F10 40/80 F11 Full F12 Quit";
    float text_w = (float)(strlen(model) + strlen(keys)) * 8.0f;
    float scale = text_w > (float)rw - 12.0f ? ((float)rw - 12.0f) / text_w : 1.0f;
    if (scale <= 0.0f) return;
    float x = ((float)rw / scale - text_w) * 0.5f;
    float text_y = ((float)y + ((float)FUNCTION_KEY_BAR_HEIGHT - 8.0f * scale) * 0.5f) / scale;
    SDL_SetRenderScale(r, scale, scale);
    SDL_SetRenderDrawColor(r, 0xFF, 0x40, 0x40, 255);
    SDL_RenderDebugText(r, x, text_y, model);
    SDL_RenderDebugText(r, x + 1.0f, text_y, model);
    SDL_SetRenderDrawColor(r, 0xE0, 0xE0, 0xE0, 255);
    SDL_RenderDebugText(r, x + (float)strlen(model) * 8.0f, text_y, keys);
    SDL_SetRenderScale(r, 1.0f, 1.0f);
}

void display_render_function_keys(Display *d) {
    render_function_keys(d->renderer, LED_BAR_HEIGHT);
    if (d->vdc_renderer) render_function_keys(d->vdc_renderer, 0);
}

void display_flip(Display *d) {
    SDL_RenderPresent(d->renderer);
    if (d->vdc_renderer) SDL_RenderPresent(d->vdc_renderer);
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
    const char *hint_txt = "Press F7 to resume";

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

/* Save whichever output is currently active: the VIC-II (40-col) or the VDC
 * (80-col) framebuffer, matching what is on screen. */
void display_save_ppm_active(Display *d, const char *path) {
    const u32 *px;
    int w, h;
    if (d->vdc_active) {
        px = d->vdc_pixels;
        w = VDC_SCREEN_W;
        h = VDC_SCREEN_H;
    } else {
        px = d->pixels;
        w = C128_SCREEN_W;
        h = C128_SCREEN_H;
    }
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        u32 p = px[i];
        unsigned char rgb[3] = {
            (unsigned char)((p >> 16) & 0xFF),
            (unsigned char)((p >>  8) & 0xFF),
            (unsigned char)( p        & 0xFF),
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
