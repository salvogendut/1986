#include "vic.h"
#include "cpu.h"
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

/* Current raster line, derived from the CPU cycle counter so it advances as
 * the CPU executes (one raster line per 63 cycles). */
static unsigned vic_raster(const Vic *v) {
    (void)v;
    return (unsigned)((cpu_cycles() / 63) % VIC_RASTER_LINES);
}

u8 vic_read(Vic *v, u16 addr) {
    unsigned raster = vic_raster(v);
    switch (addr & 0x3F) {
        case 0x11: return (u8)(v->vmode | ((raster & 0x100) ? 0x80 : 0));
        case 0x12: return (u8)(raster & 0xFF);
        case 0x16: return v->ctrl1;
        case 0x18: return v->ctrl2;
        case 0x20: return v->border_color;
        case 0x21: return v->bg_color[0];
        default: return 0xFF;
    }
}

/* Render the 40x25 character screen into the display buffer. */
void vic_render(Vic *v, Mem *m, Display *d) {
    u32 border = VIC_COLORS[v->border_color & 0x0F];
    u32 bg = VIC_COLORS[v->bg_color[0] & 0x0F];

    /* Fill with border colour. */
    for (int i = 0; i < C128_SCREEN_W * C128_SCREEN_H; i++)
        d->pixels[i] = border;

    /* Inner text area (with a small border margin). */
    int bx = 4, by = 4;
    int bw = C128_SCREEN_W - 8, bh = C128_SCREEN_H - 8;
    for (int y = by; y < by + bh; y++) {
        for (int x = bx; x < bx + bw; x++) {
            d->pixels[y * C128_SCREEN_W + x] = bg;
        }
    }

    /* 40x25 characters, 8x8 pixels each, fitted into the text area. */
    unsigned screen_base = (v->screen_addr & 0x3FFF) & 0x3C00;  /* page-aligned, <=16K */
    if (screen_base < 0x400) screen_base = 0x400;
    int cell_w = bw / VIC_CHARS_X;
    int cell_h = bh / VIC_CHARS_Y;
    if (cell_w < 1) cell_w = 1;
    if (cell_h < 1) cell_h = 1;

    for (int cy = 0; cy < VIC_CHARS_Y; cy++) {
        for (int cx = 0; cx < VIC_CHARS_X; cx++) {
            u16 cell = (u16)(screen_base + cy * VIC_CHARS_X + cx);
            u8 ch = (cell < 0x1000) ? m->ram[cell] : 0;
            u8 col = m->color_ram[(cy * VIC_CHARS_X + cx) & 0x3FF] & 0x0F;
            u32 fg = VIC_COLORS[col];
            /* Glyph: 8 bytes per character from the character ROM. */
            const u8 *glyph = &m->chargen[(u16)(ch << 3)];
            for (int py = 0; py < 8; py++) {
                u8 bits = glyph[py];
                for (int px = 0; px < 8; px++) {
                    if (bits & (0x80 >> px)) {
                        int dx = bx + cx * cell_w + px * cell_w / 8;
                        int dy = by + cy * cell_h + py * cell_h / 8;
                        if (dx < bx + bw && dy < by + bh)
                            d->pixels[dy * C128_SCREEN_W + dx] = fg;
                    }
                }
            }
        }
    }
}
