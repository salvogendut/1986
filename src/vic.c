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
    v->irq_status = 0;
    v->irq_mask = 0;
    v->raster_irq_line = 0;
    v->raster_irq_fired = 0;
    v->prev_raster = 0;
    v->cycles = 0;
}

/* Re-evaluate the VIC IRQ line (bit 7) from the pending masked flags. */
static void vic_irq_line_update(Vic *v) {
    if (v->irq_status & v->irq_mask & 0x01)
        v->irq_status |= 0x80;
    else
        v->irq_status &= 0x7F;
}

void vic_write(Vic *v, u16 addr, u8 val) {
    switch (addr & 0x3F) {
        case 0x11: v->vmode = val; break;
        case 0x12: v->raster = val; v->raster_irq_line = (u8)(v->raster_irq_line & 0x100) | val; break;
        case 0x16: v->ctrl1 = val; break;
        case 0x18: v->ctrl2 = val;
                   v->screen_addr = (u16)((val & 0xF0) << 6);
                   v->char_addr = (u16)((val & 0x0E) << 9);
                   break;
        case 0x19: v->irq_status &= (u8)~(val & 0x1F); vic_irq_line_update(v);
                   break;
        case 0x1A: v->irq_mask = val & 0x1F; vic_irq_line_update(v); break;
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
        case 0x19: return v->irq_status;
        case 0x1A: return v->irq_mask;
        case 0x20: return v->border_color;
        case 0x21: return v->bg_color[0];
        default: return 0xFF;
    }
}

/* Advance raster/IRQ state. Called each raster-line chunk; asserts the raster
 * IRQ once when the raster crosses the compare line. Returns true if the IRQ
 * line is asserted. */
bool vic_tick(Vic *v) {
    unsigned raster = vic_raster(v);

    /* Frame wrapped (raster went past the end): clear the fired latch. */
    if (raster < v->prev_raster)
        v->raster_irq_fired = 0;

    if ((raster & 0xFF) == (v->raster_irq_line & 0xFF) && !v->raster_irq_fired) {
        v->irq_status |= 0x01;   /* raster IRQ flag */
        v->raster_irq_fired = 1;
    }
    v->prev_raster = raster;

    vic_irq_line_update(v);
    return (v->irq_status & 0x80) != 0;
}

/* Render the 40x25 character screen into the display buffer, with the VIC-IIe
 * border around the text area. Characters are drawn 1:1 (8x8 pixels each). */
void vic_render(Vic *v, Mem *m, Display *d) {
    u32 border = VIC_COLORS[v->border_color & 0x0F];
    u32 bg = VIC_COLORS[v->bg_color[0] & 0x0F];

    /* Fill the whole screen with the border colour. */
    for (int i = 0; i < C128_SCREEN_W * C128_SCREEN_H; i++)
        d->pixels[i] = border;

    /* Text area (bg colour). */
    for (int y = VIC_TEXT_Y; y < VIC_TEXT_Y + VIC_TEXT_H; y++) {
        for (int x = VIC_TEXT_X; x < VIC_TEXT_X + VIC_TEXT_W; x++)
            d->pixels[y * C128_SCREEN_W + x] = bg;
    }

    /* Bitmap (graphics) mode: $D011 bit 5. $D018 bit 3 selects the 8K
     * bitmap, while bits 4-7 select the 1K screen matrix. In hires mode each
     * screen byte supplies both colours for its 8x8 cell: the high nibble for
     * bitmap bit 1 and the low nibble for bit 0. In multicolor mode the four
     * sources for pixel values 00..11 are $D021, screen high nibble, screen
     * low nibble, and colour RAM respectively. */
    if (v->vmode & 0x20) {
        u16 bitmap_addr = (u16)(((v->ctrl2 & 0x0E) << 10) & 0xE000);
        u16 screen_base = (u16)((v->ctrl2 & 0xF0) << 6);
        bool multicolor = (v->ctrl1 & 0x10) != 0;

        for (int cy = 0; cy < VIC_CHARS_Y; cy++) {
            for (int cx = 0; cx < VIC_CHARS_X; cx++) {
                u16 cell = (u16)(cy * VIC_CHARS_X + cx);
                u8 screen = m->ram[(u16)(screen_base + cell)];

                if (!multicolor) {
                    u32 fg = VIC_COLORS[screen >> 4];
                    u32 cell_bg = VIC_COLORS[screen & 0x0F];
                    for (int py = 0; py < 8; py++) {
                        u8 bits = m->ram[(u16)(bitmap_addr + cy * 320 + cx * 8 + py)];
                        int dy = VIC_TEXT_Y + cy * 8 + py;
                        for (int px = 0; px < 8; px++) {
                            int dx = VIC_TEXT_X + cx * 8 + px;
                            d->pixels[dy * C128_SCREEN_W + dx] =
                                (bits & (0x80 >> px)) ? fg : cell_bg;
                        }
                    }
                } else {
                    unsigned cbank = (m->pla_data >> 1) & 0x01;
                    u8 cram = m->color_ram[cbank * 0x400 + (cell & 0x3FF)] & 0x0F;
                    u32 colors[4] = {
                        bg,
                        VIC_COLORS[screen >> 4],
                        VIC_COLORS[screen & 0x0F],
                        VIC_COLORS[cram]
                    };
                    for (int py = 0; py < 8; py++) {
                        u8 bits = m->ram[(u16)(bitmap_addr + cy * 320 + cx * 8 + py)];
                        int dy = VIC_TEXT_Y + cy * 8 + py;
                        for (int px = 0; px < 4; px++) {
                            u8 code = (u8)((bits >> (6 - px * 2)) & 0x03);
                            u32 col = colors[code];
                            int dx = VIC_TEXT_X + cx * 8 + px * 2;
                            d->pixels[dy * C128_SCREEN_W + dx] = col;
                            d->pixels[dy * C128_SCREEN_W + dx + 1] = col;
                        }
                    }
                }
            }
        }
        return;
    }

    /* 40x25 characters, 8x8 pixels each. */
    unsigned screen_base = (v->screen_addr & 0x3FFF) & 0x3C00;  /* page-aligned, <=16K */
    if (screen_base < 0x400) screen_base = 0x400;

    for (int cy = 0; cy < VIC_CHARS_Y; cy++) {
        for (int cx = 0; cx < VIC_CHARS_X; cx++) {
            u16 cell = (u16)(screen_base + cy * VIC_CHARS_X + cx);
            u8 ch = (cell < 0x1000) ? m->ram[cell] : 0;
            unsigned cbank = (m->pla_data >> 1) & 0x01;   /* VIC colour-RAM bank */
            u8 col = m->color_ram[cbank * 0x400 + ((cy * VIC_CHARS_X + cx) & 0x3FF)] & 0x0F;
            u32 fg = VIC_COLORS[col];
            /* Glyph: 8 bytes per character from the character ROM. */
            const u8 *glyph = &m->chargen[(u16)(ch << 3)];
            for (int py = 0; py < 8; py++) {
                u8 bits = glyph[py];
                int dy = VIC_TEXT_Y + cy * 8 + py;
                for (int px = 0; px < 8; px++) {
                    if (bits & (0x80 >> px)) {
                        int dx = VIC_TEXT_X + cx * 8 + px;
                        d->pixels[dy * C128_SCREEN_W + dx] = fg;
                    }
                }
            }
        }
    }
}
