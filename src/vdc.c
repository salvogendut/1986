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
    0x00, 0x00, 0x00, 0x00, 0x0F, 0xE0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F
};

/* R28 bit 4 chooses the VDC's addressing convention, independently of how
 * much video RAM is fitted. The DRAM address-line wiring rearranges addresses
 * when the two do not match (VICE 3.10 vdc_16k_to_64k_map / vice versa). */
static u16 vdc_ram_address(const Vdc *v, u16 addr) {
    bool mode64 = (v->regs[28] & 0x10) != 0;
    bool fitted64 = v->address_mask == 0xFFFF;
    if (mode64 && fitted64) return addr;
    if (!mode64 && !fitted64) return addr & 0x3FFF;
    if (!mode64)
        return (u16)((addr & 0x80FF) |
                     ((addr & 0x3F00) << 1) | (addr & 0x0100));
    return (u16)((addr & 0x00FF) | ((addr & 0x7E00) >> 1));
}

static u8 vdc_ram_read(const Vdc *v, u16 addr) {
    return v->ram[vdc_ram_address(v, addr)];
}

static void vdc_ram_write(Vdc *v, u16 addr, u8 value) {
    v->ram[vdc_ram_address(v, addr)] = value;
    v->dirty = true;
}

void vdc_init(Vdc *v) {
    memset(v, 0, sizeof(*v));
    v->address_mask = 0xFFFF; /* C128DCR default: 64 KiB */
    v->fb_w = VDC_MAX_COLS * VDC_CHAR_WIDTH;
    v->fb_h = VDC_MAX_LINES * VDC_CHAR_HEIGHT;
    v->fb = (u32 *)malloc((size_t)v->fb_w * v->fb_h * sizeof(u32));
    vdc_powerup(v);
}

void vdc_set_ram_size_kb(Vdc *v, int kb) {
    v->address_mask = kb == 16 ? 0x3FFF : 0xFFFF;
    v->dirty = true;
}

void vdc_set_bus_clock(Vdc *v, u64 clock, bool fast_cpu) {
    if (clock < v->bus_clock) v->ready_clock = 0;
    v->bus_clock = clock;
    v->clock_scale = fast_cpu ? 2u : 1u;
}

static void vdc_busy(Vdc *v, unsigned nominal_cycles) {
    v->ready_clock = v->bus_clock + (u64)nominal_cycles * v->clock_scale;
}

static bool vdc_display_active(const Vdc *v) {
    return v->row_counter >= 1u && v->row_counter <= v->regs[6];
}

static void vdc_busy_data(Vdc *v) {
    vdc_busy(v, vdc_display_active(v) ? 43u : 4u);
}

void vdc_reset(Vdc *v) {
    memset(v->regs, 0, sizeof(v->regs));
    v->regs[0] = 126;
    v->regs[1] = 102;
    v->regs[4] = 39;
    v->regs[6] = 25;
    v->regs[9] = 7;
    v->regs[22] = 0x78;
    v->reg = 0;
    v->update_adr = 0;
    v->screen_adr = 0;
    v->attribute_adr = 0;
    v->chargen_adr = 0;
    v->cursor_adr = 0;
    v->screen_text_cols = 80;
    v->screen_textlines = 25;
    v->bytes_per_char = 16;
    v->frame_counter = 0;
    v->raster_line = 0;
    v->row_counter = 0;
    v->raster_in_row = 0;
    v->row_advance_latched = false;
    v->bus_clock = 1;
    v->ready_clock = 0;
    v->clock_scale = 1;
    v->dirty = true;
}

void vdc_powerup(Vdc *v) {
    /* The 8563/8568 comes up with the same $ff,$00 alternating VRAM pattern
     * modelled by VICE. A chip reset intentionally leaves this RAM intact. */
    for (int i = 0; i < VDC_RAM_SIZE; ++i)
        v->ram[i] = (i & 1) ? 0x00 : 0xff;
    vdc_reset(v);
}

void vdc_write_index(Vdc *v, u8 val) {
    v->reg = val & 0x3F;
}

void vdc_write_data(Vdc *v, u8 val) {
    int reg = v->reg;
    /* The light-pen position registers are read-only. */
    if (reg == 16 || reg == 17) return;
    v->regs[reg] = val;

    switch (reg) {
        case 1:   /* R01 horizontal chars displayed */
            if (val >= 6 && val <= VDC_MAX_TEXTCOLS) v->screen_text_cols = val;
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
        case 18:
        case 19:
            v->update_adr = (u16)((v->regs[18] << 8) | v->regs[19]);
            vdc_busy_data(v);
            break;
        case 20: v->attribute_adr = (u16)((v->attribute_adr & 0x00FF) | (val << 8)); v->dirty = true; break;
        case 21: v->attribute_adr = (u16)((v->attribute_adr & 0xFF00) | val);        v->dirty = true; break;
        case 28: v->chargen_adr = (u16)((val << 8) & 0xE000); v->dirty = true; break;
        case 30: { /* R30 word count -> fill or copy block */
            u16 ptr = (u16)((v->regs[18] << 8) | v->regs[19]);
            int blklen = val ? val : 256;
            if (v->regs[24] & 0x80) { /* copy */
                u16 src = (u16)((v->regs[32] << 8) | v->regs[33]);
                for (int i = 0; i < blklen; i++)
                    vdc_ram_write(v, (u16)(ptr + i), vdc_ram_read(v, (u16)(src + i)));
                src = (u16)(src + blklen);
                v->regs[31] = vdc_ram_read(v, (u16)(src - 1));
                v->regs[32] = (u8)(src >> 8);
                v->regs[33] = (u8)src;
            } else { /* fill */
                for (int i = 0; i < blklen; i++)
                    vdc_ram_write(v, (u16)(ptr + i), v->regs[31]);
            }
            ptr += blklen;
            v->regs[18] = (u8)(ptr >> 8);
            v->regs[19] = (u8)(ptr & 0xFF);
            v->update_adr = ptr;
            vdc_busy(v, (unsigned)(blklen * ((v->regs[24] & 0x80) ? 120 : 66) / 100));
            v->dirty = true;
            break;
        }
        case 31: { /* R31 data -> write to update address, auto-increment */
            u16 ptr = (u16)((v->regs[18] << 8) | v->regs[19]);
            vdc_ram_write(v, ptr, val);
            ptr++;
            v->regs[18] = (u8)(ptr >> 8);
            v->regs[19] = (u8)(ptr & 0xFF);
            v->update_adr = ptr;
            vdc_busy_data(v);
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
        u8 val = vdc_ram_read(v, ptr);
        ptr = (u16)(ptr + 1);
        v->regs[18] = (u8)(ptr >> 8);
        v->regs[19] = (u8)(ptr & 0xFF);
        v->update_adr = ptr;
        vdc_busy_data(v);
        return val;
    }
    if (v->reg < 38) return v->regs[v->reg] | regmask[v->reg];
    return 0xFF;
}

u8 vdc_read_status(const Vdc *v) {
    /* 8568 revision 2 is independent of R8's interlace setting. */
    u8 status = 2;
    if (v->bus_clock > v->ready_clock) status |= 0x80;
    if (!vdc_display_active(v)) status |= 0x20;
    return status;
}

void vdc_set_raster_line(Vdc *v, unsigned line) {
    /* VDC vertical timing is not reset by the VIC-II's PAL frame boundary.
     * R9 may even change mid-frame, so advance its row counter line by line. */
    unsigned elapsed = line >= v->raster_line
        ? line - v->raster_line : 312u - v->raster_line + line;
    for (unsigned i = 0; i < elapsed; ++i) {
        if (v->row_advance_latched) {
            v->row_advance_latched = false;
            v->raster_in_row = 0;
            if (++v->row_counter > v->regs[4]) v->row_counter = 0;
        } else {
            v->raster_in_row = (v->raster_in_row + 1u) & 0x1Fu;
        }
        /* R9 changes in the middle of a row are compared for equality, not
         * treated as an immediate row end (VICE's row-counter latch). */
        if (v->raster_in_row == (v->regs[9] & 0x1F))
            v->row_advance_latched = true;
    }
    v->raster_line = line;
}

/* --- Text-mode rendering ------------------------------------------------ */

static void render_text(Vdc *v, u32 *pixels, int fbw, int fbh,
                        int cols, int rows, bool attr_mode,
                        bool reverse_screen, u32 fg, u32 bg) {
    /* VICE's R22 mask has a special value at 7: no foreground pixels. */
    static const u8 pixel_mask[16] = {
        0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };
    static const int crsrblink[4] = { 0x01, 0x00, 0x08, 0x10 };
    int rasters_per_row = (v->regs[9] & 0x1F) + 1;
    int total_rasters = rows * rasters_per_row;
    int stride = cols + v->regs[27];
    int blink = ((v->frame_counter | 1) &
                 crsrblink[(v->regs[10] >> 5) & 3]) != 0;
    bool attribute_blink = (v->frame_counter &
        ((v->regs[24] & 0x20) ? 16 : 8)) != 0;
    int cur_top = v->regs[10] & 0x1F;
    int cur_bot = v->regs[11] & 0x1F;
    int visible_pixels = v->regs[22] & 0x0F;

    for (int y = 0; y < fbh; y++) {
        int raster = y * total_rasters / fbh;
        int row = raster / rasters_per_row;
        int glyph_line = raster % rasters_per_row;
        for (int col = 0; col < cols; col++) {
            u16 idx = (u16)(row * stride + col);
            u16 address = (u16)(v->screen_adr + idx);
            u8 c = vdc_ram_read(v, address);
            u8 attr = attr_mode ?
                vdc_ram_read(v, (u16)(v->attribute_adr + idx)) : 0;
            u32 c_fg = attr_mode ? VDC_COLORS[attr & 0x0F] : fg;
            u16 co = (u16)(v->chargen_adr +
                (attr_mode && (attr & VDC_ATTR_ALTCHARSET) ? 0x1000u : 0u) +
                (u16)(c * v->bytes_per_char));
            u8 bits = 0;
            if (glyph_line <= (v->regs[23] & 0x1F) &&
                glyph_line < (int)v->bytes_per_char)
                bits = vdc_ram_read(v, (u16)(co + glyph_line)) &
                       pixel_mask[visible_pixels];
            if (attr_mode && (attr & VDC_ATTR_UNDERLINE) &&
                glyph_line == v->regs[29])
                bits = 0xFF;
            if (attr_mode && (attr & VDC_ATTR_FLASH) && attribute_blink)
                bits = 0;
            if ((v->regs[25] & 0x20) && visible_pixels < 8 &&
                (bits & (0x80 >> visible_pixels)))
                bits |= (u8)(0xFF >> (visible_pixels + 1));
            if (attr_mode && (attr & VDC_ATTR_REVERSE)) bits ^= 0xFF;
            if (reverse_screen) bits ^= 0xFF;
            if (blink && address == v->cursor_adr &&
                glyph_line >= cur_top && glyph_line < cur_bot)
                bits ^= 0xFF;

            int x0 = col * fbw / cols;
            int x1 = (col + 1) * fbw / cols;
            for (int x = x0; x < x1; x++) {
                int bit = (x - x0) * 8 / (x1 - x0);
                pixels[y * fbw + x] = (bits & (0x80 >> bit)) ? c_fg : bg;
            }
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
            u8 bits = vdc_ram_read(v, (u16)(v->screen_adr + bitmap_row + byte_col));
            if (reverse_screen) bits = (u8)~bits;
            u32 dot_fg = fg;
            u32 dot_bg = bg;
            if (attr_mode) {
                u8 attr = vdc_ram_read(v, (u16)(v->attribute_adr + attr_row + byte_col));
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

    bool bitmap_mode = (v->regs[25] & 0x80) != 0;
    int cols = (int)v->screen_text_cols;
    /* Bitmap mode may display one raster per character row (R9=0), with
     * more than 50 rows. Amaurote uses R6=$FE for a 254-raster picture. */
    int rows = bitmap_mode ? (int)v->regs[6] : (int)v->screen_textlines;
    if (cols > VDC_MAX_COLS) cols = VDC_MAX_COLS;
    if (!bitmap_mode && rows > VDC_MAX_LINES) rows = VDC_MAX_LINES;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    /* Fill with the background colour (R26 low nibble). */
    u32 bg = VDC_COLORS[v->regs[26] & 0x0F];
    for (int i = 0; i < fbw * fbh; i++) pixels[i] = bg;

    /* Mono mode: R26 high nibble = fg, low nibble = bg. */
    u32 fg = VDC_COLORS[v->regs[26] >> 4];

    bool attr_mode = (v->regs[25] & 0x40) != 0;
    bool reverse_screen = (v->regs[24] & 0x40) != 0;

    if (bitmap_mode) {
        render_bitmap(v, pixels, fbw, fbh, cols, rows, attr_mode,
                      reverse_screen, fg, bg);
        v->dirty = false;
        return;
    }

    render_text(v, pixels, fbw, fbh, cols, rows, attr_mode,
                reverse_screen, fg, bg);
    v->dirty = false;
}
