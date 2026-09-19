#pragma once
#include "types.h"
#include <SDL3/SDL.h>
#include <stdbool.h>

/*
 * SDL3 display layer.
 * The C128 VIC-IIe (40-column) screen is rendered into a 320x200 pixel
 * texture and scaled to the window. The 8563 VDC (80-column) framebuffer is
 * a separate path (see vdc.c) that may be composited here later.
 */

#define C128_SCREEN_W       384   /* VIC-IIe full screen (320 text + 32+32 border) */
#define C128_SCREEN_H       272   /* VIC-IIe full screen (PAL normal, 35+200+37 border) */
#define VIC_TEXT_W          320   /* VIC-IIe visible text area */
#define VIC_TEXT_H          200
#define VIC_TEXT_X          32    /* text area origin (left border) */
#define VIC_TEXT_Y          35    /* text area origin (top border) */
#define WINDOW_W            768   /* 2x display width */
#define WINDOW_H            544   /* 2x display height */
#define LED_BAR_HEIGHT      22    /* drive-activity LED strip below the C128 area */
#define WINDOW_H_TOTAL      (WINDOW_H + LED_BAR_HEIGHT)

#define DISPLAY_CRT_SCANLINES_DEFAULT 35
#define DISPLAY_CRT_BRIGHTNESS_DEFAULT 100
#define DISPLAY_CRT_CONTRAST_DEFAULT 100
#define DISPLAY_CRT_RGB_DEFAULT 100

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    u32           pixels[C128_SCREEN_W * C128_SCREEN_H];
    u8            touched[C128_SCREEN_W * C128_SCREEN_H];
    u32           crt_pixels[C128_SCREEN_W * C128_SCREEN_H];
    int           scan_x;
    int           scan_y;
    bool          crt_enabled;
    int           crt_scanlines;
    int           crt_brightness;
    int           crt_contrast;
    int           crt_red;
    int           crt_green;
    int           crt_blue;
} Display;

int  display_init(Display *d, const char *title, int scale);
void display_destroy(Display *d);
void display_put_pixel(Display *d, u32 rgb);
void display_next_line(Display *d);
void display_vsync(Display *d);
void display_finalize_frame(Display *d, u32 blank); /* fill pixels not scanned this frame */
void display_upload(Display *d);   /* update texture + blit to renderer (no flip) */
void display_flip(Display *d);     /* SDL_RenderPresent */
void display_save_ppm(Display *d, const char *path);
u32  display_hash(Display *d);
void display_set_smoothing(Display *d, bool smooth);
void display_set_crt(Display *d, bool enabled, int scanlines, int brightness,
                     int contrast, int red, int green, int blue);
void display_apply_greyscale(Display *d);
void display_draw_paused_label(Display *d);
