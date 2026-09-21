#include "vdc.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* VDC 8563 palette (RGB), matching VICE's vdc-colors. */
static const u32 VDC_COLORS[16] = {
    0x000000, 0x555555, 0x0000AA, 0x5555FF,
    0x00AA00, 0x55FF55, 0x00AAAA, 0x55FFFF,
    0xAA0000, 0xFF5555, 0xAA00AA, 0xFF55FF,
    0xAA5500, 0xFFFF55, 0xAAAAAA, 0xFFFFFF
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
    v->address_mask = 0xFFFF; /* C128DCR default: 64 KiB */
    v->fb_w = VDC_MAX_COLS * VDC_CHAR_WIDTH;   /* 640 */
    v->fb_h = VDC_MAX_LINES * VDC_CHAR_HEIGHT; /* 200 */
    v->fb = (u32 *)malloc((size_t)v->fb_w * v->fb_h * sizeof(u32));
    vdc_reset(v);
}

void vdc_set_ram_size_kb(Vdc *v, int kb) {
    v->address_mask = kb == 16 ? 0x3FFF : 0xFFFF;
    v->dirty = true;
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
        case 9:   /* R09 rasters per char: chargen is one byte per raster */
            v->bytes_per_char = (val & 0x1F) < 16 ? 16 : 32;
            v->dirty = true;
            break;
        case 12: v->screen_adr = (u16)((v->screen_adr & 0x00FF) | (val << 8)); v->dirty = true; break;
        case 13: v->screen_adr = (u16)((v->screen_adr & 0xFF00) | val);       v->dirty = true; break;
        case 14: v->cursor_adr = (u16)((v->cursor_adr & 0x00FF) | (val << 8)); break;
        case 15: v->cursor_adr = (u16)((v->cursor_adr & 0xFF00) | val);        break;
        case 18: v->update_adr = (u16)(((v->regs[18] << 8) | v->regs[19]) & v->address_mask); break;
        case 19: v->update_adr = (u16)(((v->regs[18] << 8) | v->regs[19]) & v->address_mask); break;
        case 20: v->attribute_adr = (u16)((v->attribute_adr & 0x00FF) | (val << 8)); v->dirty = true; break;
        case 21: v->attribute_adr = (u16)((v->attribute_adr & 0xFF00) | val);        v->dirty = true; break;
        case 28: v->chargen_adr = (u16)((val << 8) & 0xE000 & v->address_mask); v->dirty = true; break;
        case 30: { /* R30 word count -> fill or copy block */
            u16 ptr = (u16)((v->regs[18] << 8) | v->regs[19]);
            int blklen = val ? val : 256;
            if (v->regs[24] & 0x80) { /* copy */
                u16 src = (u16)((v->regs[32] << 8) | v->regs[33]);
                for (int i = 0; i < blklen; i++)
                    v->ram[(ptr + i) & v->address_mask] = v->ram[(src + i) & v->address_mask];
                src = (u16)(src + blklen);
                v->regs[31] = v->ram[(src - 1) & v->address_mask];
                v->regs[32] = (u8)(src >> 8);
                v->regs[33] = (u8)src;
            } else { /* fill */
                for (int i = 0; i < blklen; i++)
                    v->ram[(ptr + i) & v->address_mask] = v->regs[31];
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
            v->ram[ptr & v->address_mask] = val;
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
        u8 val = v->ram[ptr & v->address_mask];
        ptr = (u16)((ptr + 1) & v->address_mask);
        v->regs[18] = (u8)(ptr >> 8);
        v->regs[19] = (u8)(ptr & 0xFF);
        v->update_adr = ptr;
        return val;
    }
    /* R28 readback follows VICE: 64 KiB forces bit 4 high, while 16 KiB
     * leaves the written bit 4 intact. The low nibble reads high in both. */
    if (v->reg == 28)
        return v->regs[28] | (v->address_mask == 0xFFFF ? 0x1F : 0x0F);
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
                      float cell_w, float cell_h) {
    int x0 = (int)(cx * cell_w);
    int y0 = (int)(cy * cell_h);
    int nw = (int)cell_w;
    int nh = (int)cell_h;
    int cur_top = v->regs[10] & 0x1F;
    int cur_bot = v->regs[11] & 0x1F;
    for (int py = 0; py < nh; py++) {
        int gy = (int)((float)py * VDC_CHAR_HEIGHT / cell_h);
        u8 bits = glyph[gy];
        if (reverse) bits = (u8)~bits;
        /* The VDC cursor inverts the character cell's glyph on the raster
         * lines between R10 (start) and R11 (end) while it is visible. */
        if (v->cursor_on && gy >= cur_top && gy < cur_bot)
            bits = (u8)~bits;
        for (int px = 0; px < nw; px++) {
            int gx = (int)((float)px * VDC_CHAR_WIDTH / cell_w);
            int dx = x0 + px;
            int dy = y0 + py;
            if (dx < fbw && dy < fbh)
                pixels[dy * fbw + dx] = (bits & (0x80 >> gx)) ? fg : bg;
        }
    }
}

/* In bitmap mode, each displayed raster line consumes one byte per eight
 * pixels. The bitmap address advances by R1 + R27 on every raster, while the
 * colour-attribute address advances only after a character row (R9 + 1
 * rasters). This is distinct from text mode's screen-code/chargen lookup. */
static void render_bitmap(const Vdc *v, u32 *pixels, int fbw, int fbh,
                          int cols, int rows, bool attr_mode,
                          bool reverse_screen, u32 fg, u32 bg) {
    int rasters_per_row = (v->regs[9] & 0x1F) + 1;
    int lines = rows * rasters_per_row;
    int stride = cols + v->regs[27];
    int pixel_width = (v->regs[25] & 0x10) ? 2 : 1;
    int logical_width = cols * 8 * pixel_width;

    for (int y = 0; y < fbh; y++) {
        int raster = y * lines / fbh;
        int attr_row = (raster / rasters_per_row) * stride;
        int bitmap_row = raster * stride;
        for (int x = 0; x < fbw; x++) {
            int source_x = x * logical_width / fbw / pixel_width;
            int byte_col = source_x >> 3;
            int bit = 7 - (source_x & 7);
            u8 bits = v->ram[(v->screen_adr + bitmap_row + byte_col) & v->address_mask];
            if (reverse_screen) bits = (u8)~bits;
            u32 dot_fg = fg;
            u32 dot_bg = bg;
            if (attr_mode) {
                u8 attr = v->ram[(v->attribute_adr + attr_row + byte_col) & v->address_mask];
                dot_fg = VDC_COLORS[attr >> 4];
                dot_bg = VDC_COLORS[attr & 0x0F];
            }
            pixels[y * fbw + x] = (bits & (1u << bit)) ? dot_fg : dot_bg;
        }
    }
}

void vdc_render(Vdc *v, u32 *pixels, int fbw, int fbh) {
    if (!pixels || !v->fb) return;

    v->frame_counter++;

    int cols = (int)v->screen_text_cols;
    int rows = (int)v->screen_textlines;
    if (cols > VDC_MAX_COLS) cols = VDC_MAX_COLS;
    if (rows > VDC_MAX_LINES) rows = VDC_MAX_LINES;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    float cell_w = (float)fbw / (float)cols;
    float cell_h = (float)fbh / (float)rows;

    /* Fill with the background colour (R26 low nibble). */
    u32 bg = VDC_COLORS[v->regs[26] & 0x0F];
    for (int i = 0; i < fbw * fbh; i++) pixels[i] = bg;

    /* Mono mode: R26 high nibble = fg, low nibble = bg. */
    u32 fg = VDC_COLORS[v->regs[26] >> 4];

    bool attr_mode = (v->regs[25] & 0x40) != 0;
    bool reverse_screen = (v->regs[24] & 0x40) != 0;

    if (v->regs[25] & 0x80) {
        render_bitmap(v, pixels, fbw, fbh, cols, rows, attr_mode,
                      reverse_screen, fg, bg);
        v->cursor_on = false;
        v->dirty = false;
        return;
    }

    /* Cursor: R14/R15 is the cursor position; R10 bits 5-7 select the blink
     * rate. It is visible while the blink phase matches (VICE crsrblink). */
    static const int crsrblink[4] = { 0x01, 0x00, 0x08, 0x10 };
    int blink = ((v->frame_counter | 1) & crsrblink[(v->regs[10] >> 5) & 3]) != 0;
    u16 cursor_idx = (u16)((v->cursor_adr - v->screen_adr) & 0xFFFF);

    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            u16 idx = (u16)(row * cols + col);
            u8 c = v->ram[(v->screen_adr + idx) & v->address_mask];
            u8 attr = attr_mode ? v->ram[(v->attribute_adr + idx) & v->address_mask] : 0;
            u32 c_fg = attr_mode ? VDC_COLORS[attr & 0x0F] : fg;
            u32 c_bg = bg;
            bool rev = reverse_screen || (attr_mode && (attr & VDC_ATTR_REVERSE));
            v->cursor_on = blink && (idx == cursor_idx);
            /* In attribute mode bit 7 selects the second 4 KiB character
             * set within the VDC's 8 KiB chargen block. The C128 editor
             * uses this for the upper/lowercase Shift+C= selection. */
            u16 co = (u16)(v->chargen_adr +
                (attr_mode && (attr & VDC_ATTR_ALTCHARSET) ? 0x1000u : 0u) +
                (u16)(c * v->bytes_per_char));
            u8 glyph[VDC_CHAR_HEIGHT];
            for (int l = 0; l < VDC_CHAR_HEIGHT; l++)
                glyph[l] = v->ram[(co + l) & v->address_mask];
            put_glyph(v, pixels, fbw, fbh, col, row, glyph, c_fg, c_bg, rev,
                      cell_w, cell_h);
        }
    }
    v->cursor_on = false;
    v->dirty = false;
}
