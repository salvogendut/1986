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
    v->ctrl2 = 0x14;             /* screen $0400, character generator $1000 */
    v->memory = 0;
    v->screen_addr = 0x0400;     /* default 40-col screen */
    v->char_addr = 0x1000;
    v->irq_status = 0;
    v->irq_mask = 0;
    v->raster_irq_line = 0;
    v->raster_irq_fired = 0;
    memset(v->sprite_x, 0, sizeof(v->sprite_x));
    memset(v->sprite_y, 0, sizeof(v->sprite_y));
    v->sprite_x_msb = 0;
    v->sprite_enable = 0;
    v->sprite_y_expand = 0;
    v->sprite_priority = 0;
    v->sprite_multicolor = 0;
    v->sprite_x_expand = 0;
    v->sprite_sprite_collision = 0;
    v->sprite_background_collision = 0;
    v->sprite_mc[0] = v->sprite_mc[1] = 0;
    memset(v->sprite_color, 0, sizeof(v->sprite_color));
    v->bank_addr = 0;
    v->prev_raster = 0;
    v->cycles = 0;
}

/* Re-evaluate the VIC IRQ line (bit 7) from the pending masked flags. */
static void vic_irq_line_update(Vic *v) {
    if (v->irq_status & v->irq_mask & 0x0F)
        v->irq_status |= 0x80;
    else
        v->irq_status &= 0x7F;
}

void vic_write(Vic *v, u16 addr, u8 val) {
    unsigned reg = addr & 0x3F;
    if (reg <= 0x0F) {
        unsigned sprite = reg >> 1;
        if (reg & 1) v->sprite_y[sprite] = val;
        else         v->sprite_x[sprite] = val;
        return;
    }

    switch (reg) {
        case 0x10: v->sprite_x_msb = val; break;
        case 0x11: v->vmode = val; break;
        case 0x12: v->raster = val; v->raster_irq_line = (u8)(v->raster_irq_line & 0x100) | val; break;
        case 0x15: v->sprite_enable = val; break;
        case 0x16: v->ctrl1 = val; break;
        case 0x17: v->sprite_y_expand = val; break;
        case 0x18: v->ctrl2 = val;
                   v->screen_addr = (u16)((val & 0xF0) << 6);
                   v->char_addr = (u16)((val & 0x0E) << 10);
                   break;
        case 0x19: v->irq_status &= (u8)~(val & 0x1F); vic_irq_line_update(v);
                   break;
        case 0x1A: v->irq_mask = val & 0x1F; vic_irq_line_update(v); break;
        case 0x1B: v->sprite_priority = val; break;
        case 0x1C: v->sprite_multicolor = val; break;
        case 0x1D: v->sprite_x_expand = val; break;
        case 0x20: v->border_color = val & 0x0F; break;
        case 0x21: v->bg_color[0] = val & 0x0F; break;
        case 0x22: v->bg_color[1] = val & 0x0F; break;
        case 0x23: v->bg_color[2] = val & 0x0F; break;
        case 0x24: v->bg_color[3] = val & 0x0F; break;
        case 0x25: v->sprite_mc[0] = val & 0x0F; break;
        case 0x26: v->sprite_mc[1] = val & 0x0F; break;
        case 0x27: case 0x28: case 0x29: case 0x2A:
        case 0x2B: case 0x2C: case 0x2D: case 0x2E:
            v->sprite_color[reg - 0x27] = val & 0x0F;
            break;
        default: break;
    }
}

void vic_write_rmw(Vic *v, u16 addr, u8 val) {
    /* 6502 read-modify-write instructions put the unmodified byte on the
     * bus before writing the result. VICE's core reports this as one write
     * with maincpu_rmw_flag, so reproduce the first bus write here. On
     * $D019 this acknowledges pending VIC IRQ bits even when LSR's final
     * value no longer has those bits set. */
    vic_write(v, addr, vic_read(v, addr));
    vic_write(v, addr, val);
}

/* Current raster line, derived from the CPU cycle counter so it advances as
 * the CPU executes (one raster line per 63 cycles). */
static unsigned vic_raster(const Vic *v) {
    (void)v;
    return (unsigned)((cpu_cycles() / 63) % VIC_RASTER_LINES);
}

u8 vic_read(Vic *v, u16 addr) {
    unsigned raster = vic_raster(v);
    unsigned reg = addr & 0x3F;
    if (reg <= 0x0F) {
        unsigned sprite = reg >> 1;
        return (reg & 1) ? v->sprite_y[sprite] : v->sprite_x[sprite];
    }

    switch (reg) {
        case 0x10: return v->sprite_x_msb;
        case 0x11: return (u8)(v->vmode | ((raster & 0x100) ? 0x80 : 0));
        case 0x12: return (u8)(raster & 0xFF);
        case 0x15: return v->sprite_enable;
        case 0x16: return v->ctrl1;
        case 0x17: return v->sprite_y_expand;
        case 0x18: return v->ctrl2;
        case 0x19: return v->irq_status;
        case 0x1A: return v->irq_mask;
        case 0x1B: return v->sprite_priority;
        case 0x1C: return v->sprite_multicolor;
        case 0x1D: return v->sprite_x_expand;
        case 0x1E: { u8 value = v->sprite_sprite_collision;
                     v->sprite_sprite_collision = 0;
                     v->irq_status &= (u8)~0x04;
                     vic_irq_line_update(v);
                     return value; }
        case 0x1F: { u8 value = v->sprite_background_collision;
                     v->sprite_background_collision = 0;
                     v->irq_status &= (u8)~0x02;
                     vic_irq_line_update(v);
                     return value; }
        case 0x20: return v->border_color;
        case 0x21: return v->bg_color[0];
        case 0x22: return v->bg_color[1];
        case 0x23: return v->bg_color[2];
        case 0x24: return v->bg_color[3];
        case 0x25: return v->sprite_mc[0];
        case 0x26: return v->sprite_mc[1];
        case 0x27: case 0x28: case 0x29: case 0x2A:
        case 0x2B: case 0x2C: case 0x2D: case 0x2E:
            return v->sprite_color[reg - 0x27];
        default: return 0xFF;
    }
}

void vic_set_bank(Vic *v, unsigned bank) {
    v->bank_addr = (u32)(bank & 0x07) << 14;
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

/* VICE maps sprite X into its normal PAL canvas with left-border-width - 24.
 * Its first displayed PAL raster line is 16, giving the matching Y offset. */
#define VIC_SPRITE_X_OFFSET  (VIC_TEXT_X - 24)
#define VIC_FIRST_VISIBLE_LINE  16

static void vic_draw_sprites(Vic *v, Mem *m, Display *d, const u8 *foreground) {
    u8 occupied[C128_SCREEN_W * C128_SCREEN_H];
    memset(occupied, 0, sizeof(occupied));

    u8 sprite_sprite = 0;
    u8 sprite_background = 0;
    u32 screen_base = v->bank_addr + ((v->ctrl2 & 0xF0) << 6);

    /* Draw 7 first and 0 last: lower-numbered sprites have priority. */
    for (int sprite = VIC_SPRITES - 1; sprite >= 0; sprite--) {
        u8 sprite_bit = (u8)(1u << sprite);
        if (!(v->sprite_enable & sprite_bit))
            continue;

        unsigned x = v->sprite_x[sprite]
                   | ((v->sprite_x_msb & sprite_bit) ? 0x100u : 0u);
        int origin_x = (int)x + VIC_SPRITE_X_OFFSET;
        int origin_y = (int)v->sprite_y[sprite] - VIC_FIRST_VISIBLE_LINE;
        int x_scale = (v->sprite_x_expand & sprite_bit) ? 2 : 1;
        int y_scale = (v->sprite_y_expand & sprite_bit) ? 2 : 1;
        bool multicolor = (v->sprite_multicolor & sprite_bit) != 0;
        bool behind = (v->sprite_priority & sprite_bit) != 0;
        u8 pointer = m->ram[screen_base + 0x3F8 + (unsigned)sprite];
        u32 data_base = v->bank_addr + ((u32)pointer << 6);

        for (int source_y = 0; source_y < 21; source_y++) {
            u32 bits = ((u32)m->ram[data_base + source_y * 3] << 16)
                     | ((u32)m->ram[data_base + source_y * 3 + 1] << 8)
                     | m->ram[data_base + source_y * 3 + 2];

            int source_width = multicolor ? 12 : 24;
            int logical_width = multicolor ? 2 : 1;
            for (int source_x = 0; source_x < source_width; source_x++) {
                u8 code;
                u8 color;
                if (multicolor) {
                    code = (u8)((bits >> (22 - source_x * 2)) & 0x03);
                    if (code == 0) continue;
                    color = code == 1 ? v->sprite_mc[0]
                          : code == 2 ? v->sprite_color[sprite]
                                      : v->sprite_mc[1];
                } else {
                    code = (u8)((bits >> (23 - source_x)) & 0x01);
                    if (code == 0) continue;
                    color = v->sprite_color[sprite];
                }

                int pixel_width = logical_width * x_scale;
                for (int repeat_y = 0; repeat_y < y_scale; repeat_y++) {
                    int dy = origin_y + source_y * y_scale + repeat_y;
                    if (dy < VIC_TEXT_Y || dy >= VIC_TEXT_Y + VIC_TEXT_H)
                        continue;

                    for (int repeat_x = 0; repeat_x < pixel_width; repeat_x++) {
                        int dx = origin_x + source_x * pixel_width + repeat_x;
                        if (dx < VIC_TEXT_X || dx >= VIC_TEXT_X + VIC_TEXT_W)
                            continue;

                        unsigned off = (unsigned)(dy * C128_SCREEN_W + dx);
                        if (occupied[off])
                            sprite_sprite |= occupied[off] | sprite_bit;
                        occupied[off] |= sprite_bit;

                        if (foreground[off])
                            sprite_background |= sprite_bit;
                        if (!behind || !foreground[off])
                            d->pixels[off] = VIC_COLORS[color & 0x0F];
                    }
                }
            }
        }
    }

    if (sprite_sprite) {
        v->sprite_sprite_collision |= sprite_sprite;
        v->irq_status |= 0x04;
    }
    if (sprite_background) {
        v->sprite_background_collision |= sprite_background;
        v->irq_status |= 0x02;
    }
    vic_irq_line_update(v);
}

/* Render the 40x25 character screen into the display buffer, with the VIC-IIe
 * border around the text area. Characters are drawn 1:1 (8x8 pixels each),
 * then the eight hardware sprites are composited over the graphics plane. */
void vic_render(Vic *v, Mem *m, Display *d) {
    u8 foreground[C128_SCREEN_W * C128_SCREEN_H];
    memset(foreground, 0, sizeof(foreground));

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
        u32 bitmap_addr = v->bank_addr + (((v->ctrl2 & 0x0E) << 10) & 0x2000);
        u32 screen_base = v->bank_addr + ((v->ctrl2 & 0xF0) << 6);
        bool multicolor = (v->ctrl1 & 0x10) != 0;

        for (int cy = 0; cy < VIC_CHARS_Y; cy++) {
            for (int cx = 0; cx < VIC_CHARS_X; cx++) {
                u16 cell = (u16)(cy * VIC_CHARS_X + cx);
                u8 screen = m->ram[screen_base + cell];

                if (!multicolor) {
                    u32 fg = VIC_COLORS[screen >> 4];
                    u32 cell_bg = VIC_COLORS[screen & 0x0F];
                    for (int py = 0; py < 8; py++) {
                        u8 bits = m->ram[bitmap_addr + cy * 320 + cx * 8 + py];
                        int dy = VIC_TEXT_Y + cy * 8 + py;
                        for (int px = 0; px < 8; px++) {
                            int dx = VIC_TEXT_X + cx * 8 + px;
                            unsigned off = (unsigned)(dy * C128_SCREEN_W + dx);
                            bool set = (bits & (0x80 >> px)) != 0;
                            d->pixels[off] = set ? fg : cell_bg;
                            foreground[off] = set;
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
                        u8 bits = m->ram[bitmap_addr + cy * 320 + cx * 8 + py];
                        int dy = VIC_TEXT_Y + cy * 8 + py;
                        for (int px = 0; px < 4; px++) {
                            u8 code = (u8)((bits >> (6 - px * 2)) & 0x03);
                            int dx = VIC_TEXT_X + cx * 8 + px * 2;
                            unsigned off = (unsigned)(dy * C128_SCREEN_W + dx);
                            d->pixels[off] = colors[code];
                            d->pixels[off + 1] = colors[code];
                            foreground[off] = foreground[off + 1] = code != 0;
                        }
                    }
                }
            }
        }
    } else {
        /* 40x25 characters, 8x8 pixels each. */
        unsigned screen_base = (v->screen_addr & 0x3FFF) & 0x3C00;
        if (screen_base < 0x400) screen_base = 0x400;
        screen_base += v->bank_addr;

        for (int cy = 0; cy < VIC_CHARS_Y; cy++) {
            for (int cx = 0; cx < VIC_CHARS_X; cx++) {
                u32 cell = screen_base + (unsigned)(cy * VIC_CHARS_X + cx);
                u8 ch = m->ram[cell];
                unsigned cbank = (m->pla_data >> 1) & 0x01;
                u8 col = m->color_ram[cbank * 0x400 + ((cy * VIC_CHARS_X + cx) & 0x3FF)] & 0x0F;
                u32 fg = VIC_COLORS[col];
                /* Native C128 PLA: $01 bit 2 low maps the character ROM into
                 * the VIC's $1000-$1FFF window. Else glyph data comes from
                 * the selected VIC RAM bank at the $D018 character pointer.
                 * The International/US machine uses the upper 4K ROM half. */
                u16 glyph_addr = (u16)(v->char_addr + ((u16)ch << 3));
                bool rom = !(m->pla_data & 0x04) &&
                           (glyph_addr & 0x3000) == 0x1000;
                const u8 *glyph = rom
                    ? &m->chargen[0x1000 + (glyph_addr & 0x0FFF)]
                    : &m->ram[v->bank_addr + glyph_addr];
                for (int py = 0; py < 8; py++) {
                    u8 bits = glyph[py];
                    int dy = VIC_TEXT_Y + cy * 8 + py;
                    for (int px = 0; px < 8; px++) {
                        if (bits & (0x80 >> px)) {
                            int dx = VIC_TEXT_X + cx * 8 + px;
                            unsigned off = (unsigned)(dy * C128_SCREEN_W + dx);
                            d->pixels[off] = fg;
                            foreground[off] = 1;
                        }
                    }
                }
            }
        }
    }

    vic_draw_sprites(v, m, d, foreground);
}
