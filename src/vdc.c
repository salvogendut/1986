#include "vdc.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* VDC palette (RGB, CGA-style 16 colours). */
static const u32 VDC_COLORS[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

/* Unused-bit masks for returned register values (VICE regmask[38]). */
static const u8 regmask[38] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x00, 0x00,
    0xFC, 0xE0, 0x80, 0xE0, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F
};

void vdc_init(Vdc *v) {
    memset(v, 0, sizeof(*v));
    v->fb_w = VDC_MAX_COLS * VDC_CHAR_WIDTH;   /* 640 */
    v->fb_h = VDC_MAX_LINES * VDC_CHAR_HEIGHT; /* 200 */
    v->fb = (u32 *)malloc((size_t)v->fb_w * v->fb_h * sizeof(u32));
    vdc_reset(v);
}

void vdc_reset(Vdc *v) {
    /* VICE powerup fills the RAM with a 0xff00ff00.. pattern. */
    for (int i = 0; i < VDC_RAM_SIZE; i++)
        v->ram[i] = (i & 1) ? 0x00 : 0xFF;
    memset(v->regs, 0, sizeof(v->regs));
    v->reg = 0;
    v->update_adr = 0;
    v->screen_adr = 0;
    v->attribute_adr = 0;
    v->chargen_adr = 0;
    v->cursor_adr = 0;
    v->screen_text_cols = 80;
    v->screen_textlines = 25;
    v->bytes_per_char = 16;
    v->dirty = true;
}

void vdc_write_index(Vdc *v, u8 val) {
    v->reg = val & 0x3F;
}

void vdc_write_data(Vdc *v, u8 val) {
    int reg = v->reg;
    v->regs[reg] = val;

    switch (reg) {
        case 1:   /* R01 horizontal chars displayed */
            if (val >= 8 && val <= VDC_MAX_TEXTCOLS) v->screen_text_cols = val;
            v->dirty = true;
            break;
        case 6:   /* R06 vertical displayed */
            if (val <= VDC_MAX_TEXTLINES) v->screen_textlines = val;
            v->dirty = true;
            break;
        case 9:   /* R09 rasters per char */
            v->bytes_per_char = (val & 0x1F) < 16 ? 16 : 32;
            v->dirty = true;
            break;
        case 12: v->screen_adr = (u16)((v->screen_adr & 0x00FF) | (val << 8)); v->dirty = true; break;
        case 13: v->screen_adr = (u16)((v->screen_adr & 0xFF00) | val);       v->dirty = true; break;
        case 14: v->cursor_adr = (u16)((v->cursor_adr & 0x00FF) | (val << 8)); break;
        case 15: v->cursor_adr = (u16)((v->cursor_adr & 0xFF00) | val);        break;
        case 18: v->update_adr = (u16)((v->regs[18] << 8) | v->regs[19]); break;
        case 19: v->update_adr = (u16)((v->regs[18] << 8) | v->regs[19]); break;
        case 20: v->attribute_adr = (u16)((v->attribute_adr & 0x00FF) | (val << 8)); v->dirty = true; break;
        case 21: v->attribute_adr = (u16)((v->attribute_adr & 0xFF00) | val);        v->dirty = true; break;
        case 28: v->chargen_adr = (u16)(val << 8); v->dirty = true; break;
        case 30: { /* R30 word count -> fill or copy block */
            u16 ptr = (u16)((v->regs[18] << 8) | v->regs[19]);
            int blklen = val ? val : 256;
            if (v->regs[24] & 0x80) { /* copy */
                u16 src = (u16)((v->regs[32] << 8) | v->regs[33]);
                for (int i = 0; i < blklen; i++)
                    v->ram[(ptr + i) & 0xFFFF] = v->ram[(src + i) & 0xFFFF];
            } else { /* fill */
                for (int i = 0; i < blklen; i++)
                    v->ram[(ptr + i) & 0xFFFF] = v->regs[31];
            }
            ptr += blklen;
            v->regs[18] = (u8)(ptr >> 8);
            v->regs[19] = (u8)(ptr & 0xFF);
            v->update_adr = ptr;
            v->dirty = true;
            break;
        }
        case 31: { /* R31 data -> write to update address, auto-increment */
            u16 ptr = (u16)((v->regs[18] << 8) | v->regs[19]);
            v->ram[ptr & 0xFFFF] = val;
            ptr++;
            v->regs[18] = (u8)(ptr >> 8);
            v->regs[19] = (u8)(ptr & 0xFF);
            v->update_adr = ptr;
            v->dirty = true;
            break;
        }
        default:
            break;
    }
}

u8 vdc_read_data(Vdc *v) {
    if (v->reg == 31) {
        u16 ptr = (u16)((v->regs[18] << 8) | v->regs[19]);
        u8 val = v->ram[ptr & 0xFFFF];
        ptr++;
        v->regs[18] = (u8)(ptr >> 8);
        v->regs[19] = (u8)(ptr & 0xFF);
        v->update_adr = ptr;
        return val;
    }
    if (v->reg < 38) return v->regs[v->reg] | regmask[v->reg];
    return 0xFF;
}

u8 vdc_read_status(const Vdc *v) {
    /* Status: bit 7 = ready (always set), bit 0-1 = revision. */
    return 0x80 | (v->regs[8] & 0x03);
}

/* --- Text-mode rendering ------------------------------------------------ */

static void put_glyph(Vdc *v, u32 *pixels, int fbw, int fbh, int cx, int cy,
                      const u8 *glyph, u32 fg, u32 bg, int reverse,
                      int cell_w, int cell_h) {
    for (int py = 0; py < cell_h; py++) {
        int gy = (int)((long)py * VDC_CHAR_HEIGHT / cell_h);
        u8 bits = glyph[gy];
        if (reverse) bits = (u8)~bits;
        for (int px = 0; px < cell_w; px++) {
            int gx = (int)((long)px * VDC_CHAR_WIDTH / cell_w);
            int dx = cx * cell_w + px;
            int dy = cy * cell_h + py;
            if (dx < fbw && dy < fbh)
                pixels[dy * fbw + dx] = (bits & (0x80 >> gx)) ? fg : bg;
        }
    }
}

void vdc_render(Vdc *v, u32 *pixels, int fbw, int fbh) {
    if (!pixels || !v->fb) return;

    int cols = (int)v->screen_text_cols;
    int rows = (int)v->screen_textlines;
    if (cols > VDC_MAX_COLS) cols = VDC_MAX_COLS;
    if (rows > VDC_MAX_LINES) rows = VDC_MAX_LINES;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    int cell_w = (fbw / cols < 1) ? 1 : fbw / cols;
    int cell_h = (fbh / rows < 1) ? 1 : fbh / rows;

    /* Fill with the background colour (R26 low nibble). */
    u32 bg = VDC_COLORS[v->regs[26] & 0x0F];
    for (int i = 0; i < fbw * fbh; i++) pixels[i] = bg;

    /* Mono mode: R26 high nibble = fg, low nibble = bg. */
    u32 fg = VDC_COLORS[v->regs[26] >> 4];

    bool attr_mode = (v->regs[25] & 0x40) != 0;
    bool reverse_screen = (v->regs[24] & 0x40) != 0;

    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            u16 idx = (u16)(row * cols + col);
            u8 c = v->ram[(v->screen_adr + idx) & 0xFFFF];
            u8 attr = attr_mode ? v->ram[(v->attribute_adr + idx) & 0xFFFF] : 0;
            u32 c_fg = attr_mode ? VDC_COLORS[attr & 0x0F] : fg;
            u32 c_bg = bg;
            bool rev = reverse_screen || (attr_mode && (attr & VDC_ATTR_REVERSE));
            u16 co = (u16)((v->chargen_adr + (u16)(c * v->bytes_per_char)) & 0xFFFF);
            u8 glyph[VDC_CHAR_HEIGHT];
            for (int l = 0; l < VDC_CHAR_HEIGHT; l++)
                glyph[l] = v->ram[(co + l) & 0xFFFF];
            put_glyph(v, pixels, fbw, fbh, col, row, glyph, c_fg, c_bg, rev,
                      cell_w, cell_h);
        }
    }
    v->dirty = false;
}
