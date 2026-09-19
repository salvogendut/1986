#include "vic.h"
#include <string.h>

/* C64/C128 colour palette (RGB). */
static const u32 VIC_COLORS[16] = {
    0x000000, 0xFFFFFF, 0x813338, 0x75CEC8, 0x8E3C97, 0x56AC4D, 0x2E2C9B, 0xEDF171,
    0x8E5029, 0x553800, 0xC46C71, 0x4A4A4A, 0x7B7B7B, 0xA9FF9F, 0x706DEB, 0xB2B2B2
};

void vic_init(Vic *v) {
    memset(v, 0, sizeof(*v));
    vic_reset(v);
}

void vic_reset(Vic *v) {
    v->border_color = 0;         /* black */
    v->bg_color[0] = 0;
    v->bg_color[1] = 0;
    v->bg_color[2] = 0;
    v->bg_color[3] = 0;
    v->bg_color_idx = 0;
    v->vmode = 0x1B;             /* 25 rows, bitmap off */
    v->raster = 0;
    v->ctrl1 = 0x80;
    v->ctrl2 = 0x08;
    v->memory = 0;
    v->screen_addr = 0x0400;     /* default 40-col screen */
    v->char_addr = 0x1000;
    v->cycles = 0;
}

void vic_write(Vic *v, u16 addr, u8 val) {
    switch (addr & 0x3F) {
        case 0x11: v->vmode = val; break;
        case 0x12: v->raster = val; break;
        case 0x16: v->ctrl1 = val; break;
        case 0x18: v->ctrl2 = val;
                   v->screen_addr = (u16)((val & 0xF0) << 6);
                   v->char_addr = (u16)((val & 0x0E) << 9);
                   break;
        case 0x20: v->border_color = val & 0x0F; break;
        case 0x21: v->bg_color[0] = val & 0x0F; break;
        case 0x22: v->bg_color[1] = val & 0x0F; break;
        case 0x23: v->bg_color[2] = val & 0x0F; break;
        case 0x24: v->bg_color[3] = val & 0x0F; break;
        default: break;
    }
}

u8 vic_read(Vic *v, u16 addr) {
    switch (addr & 0x3F) {
        case 0x11: return v->vmode;
        case 0x12: return v->raster;
        case 0x16: return v->ctrl1;
        case 0x18: return v->ctrl2;
        case 0x20: return v->border_color;
        case 0x21: return v->bg_color[0];
        default: return 0xFF;
    }
}

void vic_render(Vic *v, Display *d) {
    u32 border = VIC_COLORS[v->border_color & 0x0F];
    u32 bg = VIC_COLORS[v->bg_color[0] & 0x0F];

    /* Fill the whole frame with the border colour. */
    for (int i = 0; i < C128_SCREEN_W * C128_SCREEN_H; i++)
        d->pixels[i] = border;

    /* Interior screen area (a 320x200 display with a 4-pixel outer border). */
    int bx = 4, by = 4, bw = C128_SCREEN_W - 8, bh = C128_SCREEN_H - 8;
    for (int y = by; y < by + bh; y++) {
        for (int x = bx; x < bx + bw; x++) {
            d->pixels[y * C128_SCREEN_W + x] = bg;
        }
    }

    /* Simple colour grid: 16 columns of the palette as a sanity check. */
    int gw = bw / 16;
    for (int c = 0; c < 16; c++) {
        u32 col = VIC_COLORS[c];
        for (int y = by + 8; y < by + bh - 8; y++) {
            for (int x = bx + c * gw; x < bx + (c + 1) * gw; x++) {
                d->pixels[y * C128_SCREEN_W + x] = col;
            }
        }
    }
}
