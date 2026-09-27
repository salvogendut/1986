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

/* VICE exposes the PAL VDC as an 856x288 raster. Preserve its full horizontal
 * resolution in the public framebuffer and expand only the vertical axis
 * for 4:3 presentation. Changing R1/R6 must reveal border rather than stretch
 * a small display mode over the whole window. */
#define VDC_RASTER_WIDTH  856
#define VDC_RASTER_HEIGHT 288

static const u8 VDC_PIXEL_MASK[16] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
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
    v->fb_w = VDC_RASTER_WIDTH;
    v->fb_h = VDC_RASTER_HEIGHT;
    v->fb = (u32 *)malloc((size_t)v->fb_w * v->fb_h * sizeof(u32));
    v->display_fb = (u32 *)malloc(
        (size_t)v->fb_w * v->fb_h * sizeof(u32));
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
    v->vertical_adjust_counter = 0;
    v->vertical_adjust_active = false;
    v->raster_screen_adr = 0;
    v->raster_attribute_adr = 0;
    v->raster_attribute_offset = 0;
    v->raster_fb_valid = false;
    v->raster_output_line = 0;
    v->vsync_counter = 0;
    v->vsync_active = false;
    v->draw_raster = 0;
    v->draw_advance_latched = false;
    v->draw_prime = true;
    v->draw_active = false;
    v->draw_finished = false;
    v->draw_screen_pending = false;
    v->draw_attribute_pending = false;
    v->bus_clock = 1;
    v->ready_clock = 0;
    v->clock_scale = 1;
    v->dirty = true;
    if (v->fb)
        memset(v->fb, 0, (size_t)v->fb_w * v->fb_h * sizeof(*v->fb));
    if (v->display_fb)
        memset(v->display_fb, 0,
               (size_t)v->fb_w * v->fb_h * sizeof(*v->display_fb));
    v->display_fb_valid = false;
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
    u8 oldval = v->regs[reg];
    /* The light-pen position registers are read-only. */
    if (reg == 16 || reg == 17) return;
    v->regs[reg] = val;

    switch (reg) {
        case 1:   /* R01 horizontal chars displayed */
            if (val >= 6 && val <= VDC_MAX_TEXTCOLS) v->screen_text_cols = val;
            v->dirty = true;
            break;
        case 2:   /* sync/total registers position the active rectangle */
        case 4:
        case 5:
        case 7:
        case 8:
            v->dirty = true;
            break;
        case 6:   /* R06 vertical displayed */
            if (val <= VDC_MAX_TEXTLINES) v->screen_textlines = val;
            v->dirty = true;
            break;
        case 9:   /* R09 rasters per char: chargen is one byte per raster */
            /* A zero-to-nonzero transition on the final displayed row moves
             * the attribute fetch by three bytes on a real 8563/8568.  VDC
             * FLI software uses this to obtain 8x1 colour cells. */
            if ((oldval & 0x1F) == 0 && (val & 0x1F) != 0 &&
                v->row_counter == v->regs[6] - 1u)
                v->raster_attribute_offset = 3;
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
        case 22:
        case 25:
            v->dirty = true;
            break;
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

static int vdc_char_width(const Vdc *v) {
    int width = (v->regs[25] & 0x10)
        ? 2 * (int)(v->regs[22] >> 4)
        : 1 + (int)(v->regs[22] >> 4);
    return width > 0 ? width : 1;
}

static int vdc_horizontal_start(const Vdc *v, int cols, int char_width) {
    int start = (v->regs[25] & 0x10)
        ? 62 * 16 - (int)v->regs[2] * char_width
        : 116 * 8 - (int)v->regs[2] * char_width;
    int width = cols * char_width;
    if (start < 0 || width >= VDC_RASTER_WIDTH) start = 0;
    else if (start + width > VDC_RASTER_WIDTH)
        start = VDC_RASTER_WIDTH - width;

    /* V1/V2 VDC horizontal smooth scrolling moves the contents inside a
     * fixed border.  The DCR's 8568 behaves as V2. */
    int dot_scale = (v->regs[25] & 0x10) ? 2 : 1;
    start += ((int)(v->regs[25] & 0x0F) -
              (int)(v->regs[22] >> 4)) * dot_scale;
    if (v->regs[25] & 0x10) start += 2;
    return start;
}

/* Capture one VDC raster while the CPU is running.  Demos can alter display
 * and attribute registers between scan lines, so rebuilding the whole image
 * from the final register values loses the very effects the VDC was designed
 * to produce. */
static void vdc_capture_raster(Vdc *v) {
    /* Sample the coarse address step (R27) and fine pixel shift (R25) at
     * the same drawing boundary, as in VICE's raster draw handler. Latching
     * the row-end decision must still happen on the preceding raster, so
     * an intervening R9 write cannot cancel an already-matched row end. */
    unsigned stride = v->screen_text_cols + v->regs[27];
    if (v->draw_screen_pending)
        v->raster_screen_adr = (u16)(v->raster_screen_adr + stride);
    if (v->draw_attribute_pending)
        v->raster_attribute_adr = (u16)(v->raster_attribute_adr + stride);
    v->draw_screen_pending = false;
    v->draw_attribute_pending = false;
    if (!v->fb) return;

    u32 bg = VDC_COLORS[v->regs[26] & 0x0F];
    /* Clear only the physical line being scanned.  The VDC's timing is
     * independent of the VIC-II/host presentation boundary, so erasing the
     * entire scanout at an internal frame wrap exposes a partial frame. */
    int y = (int)v->raster_output_line;
    if (y >= 0 && y < v->fb_h) {
        u32 *line = v->fb + (size_t)y * v->fb_w;
        for (int x = 0; x < v->fb_w; ++x) line[x] = bg;
        v->raster_fb_valid = true;
    }
    if (!vdc_display_active(v) || !v->draw_active || v->vsync_active ||
        v->draw_raster > (v->regs[9] & 0x1Fu)) return;

    int cols = (int)v->screen_text_cols;
    if (cols < 1) cols = 1;
    if (cols > VDC_MAX_COLS) cols = VDC_MAX_COLS;
    bool bitmap = (v->regs[25] & 0x80) != 0;
    if (y < 0 || y >= v->fb_h) return;
    u32 *line = v->fb + (size_t)y * v->fb_w;

    int char_width = vdc_char_width(v);
    if (char_width < 8 ||
        (v->regs[25] & 0x0F) > (v->regs[22] >> 4) ||
        ((v->regs[34] > v->regs[0]) && (v->regs[35] <= v->regs[0])) ||
        ((v->regs[34] == v->regs[35]) && (v->regs[35] <= v->regs[0])))
        return;

    int x0 = vdc_horizontal_start(v, cols, char_width);
    bool attr_mode = (v->regs[25] & 0x40) != 0;
    bool reverse = (v->regs[24] & 0x40) != 0;
    bool double_pixel = (v->regs[25] & 0x10) != 0;
    bool attribute_blink = (v->frame_counter &
        ((v->regs[24] & 0x20) ? 16 : 8)) != 0;
    u32 fg = VDC_COLORS[v->regs[26] >> 4];

    for (int col = 0; col < cols; ++col) {
        u8 bits;
        u32 dot_fg = fg, dot_bg = bg;
        u16 attr_addr = (u16)(v->raster_attribute_adr + col);
        u8 attr = attr_mode ? vdc_ram_read(v, attr_addr) : 0;

        if (bitmap) {
            u16 address = (u16)(v->raster_screen_adr + col);
            bits = vdc_ram_read(v, address);
            if (attr_mode) {
                /* Bitmap attributes have the opposite nibble order to
                 * R26: low = foreground, high = background (VICE VDC). */
                dot_fg = VDC_COLORS[attr & 0x0F];
                dot_bg = VDC_COLORS[attr >> 4];
            }
        } else {
            u16 address = (u16)(v->raster_screen_adr + col);
            u8 c = vdc_ram_read(v, address);
            u16 glyph = (u16)(v->chargen_adr +
                (attr_mode && (attr & VDC_ATTR_ALTCHARSET) ? 0x1000u : 0u) +
                (u16)(c * v->bytes_per_char) + v->draw_raster);
            bits = v->draw_raster <= (v->regs[23] & 0x1F)
                ? vdc_ram_read(v, glyph) & VDC_PIXEL_MASK[v->regs[22] & 0x0F]
                : 0;
            if (attr_mode) dot_fg = VDC_COLORS[attr & 0x0F];
            if (attr_mode && (attr & VDC_ATTR_UNDERLINE) &&
                v->draw_raster == v->regs[29]) bits = 0xFF;
            if (attr_mode && (attr & VDC_ATTR_FLASH) && attribute_blink)
                bits = 0;
            if (v->regs[25] & 0x20) {
                int visible = v->regs[22] & 0x0F;
                if (visible < 8 && (bits & (0x80 >> visible)))
                    bits |= (u8)(0xFF >> (visible + 1));
            }
            if (attr_mode && (attr & VDC_ATTR_REVERSE)) bits ^= 0xFF;
        }
        if (reverse) bits ^= 0xFF;

        for (int px = 0; px < char_width; ++px) {
            int x = x0 + col * char_width + px;
            if (x < 0 || x >= v->fb_w) continue;
            int bit = double_pixel ? px / 2 : px;
            line[x] = bit < 8 && (bits & (0x80u >> bit)) ? dot_fg : dot_bg;
        }
    }
    v->raster_fb_valid = true;
}

static void vdc_latch_addresses(Vdc *v) {
    v->raster_screen_adr = (u16)((v->regs[12] << 8) | v->regs[13]);
    v->raster_attribute_adr = (u16)(
        ((v->regs[20] << 8) | v->regs[21]) + v->raster_attribute_offset);
    v->raster_attribute_offset = 0;
    if (v->regs[4] == 0xFF)
        v->raster_attribute_adr = (u16)(
            v->raster_attribute_adr - v->regs[1]);
    v->draw_active = false;
    v->draw_finished = true;
}

/* The vertical signal counter and the drawing counter are separate on the
 * VDC.  R24 primes drawing early by up to 31 rasters; the border and status
 * timing continue to use the signal counter.  See VICE's vdc.c drawing
 * section.  In particular, retain a matched row-end latch across R9 writes:
 * re-testing the live R9 loses a colour-row advance in RFOVDC's FLI image. */
static void vdc_advance_drawing(Vdc *v) {
    unsigned last = v->regs[9] & 0x1Fu;
    if (v->draw_prime) {
        if (v->row_counter == 0 && v->raster_in_row == 0) {
            v->draw_raster = v->regs[24] & 0x1Fu;
            v->draw_advance_latched = false;
        } else if (v->draw_raster == last) {
            v->draw_raster = 0;
            v->draw_prime = false;
            v->draw_active = true;
            v->draw_finished = false;
            v->draw_advance_latched = last == 0;
        } else {
            v->draw_raster = (v->draw_raster + 1u) & 0x1Fu;
        }
    } else if (v->draw_active) {
        bool row_end = v->draw_advance_latched;
        if (row_end) {
            v->draw_raster = 0;
            v->draw_attribute_pending = true;
        } else {
            v->draw_raster = (v->draw_raster + 1u) & 0x1Fu;
        }
        if ((v->regs[25] & 0x80) || row_end)
            v->draw_screen_pending = true;
        v->draw_advance_latched = v->draw_raster == last;
    } else {
        v->draw_advance_latched = false;
    }
}

void vdc_set_raster_line(Vdc *v, unsigned line) {
    /* VDC vertical timing is not reset by the VIC-II's PAL frame boundary.
     * R9 may even change mid-frame, so advance its row counter line by line. */
    unsigned elapsed = line >= v->raster_line
        ? line - v->raster_line : 312u - v->raster_line + line;
    for (unsigned i = 0; i < elapsed; ++i) {
        vdc_capture_raster(v);
        bool row_advanced = false;
        bool frame_restarted = false;
        if (v->vertical_adjust_active) {
            unsigned adjust = v->regs[5] & 0x1Fu;
            if (++v->vertical_adjust_counter >= adjust) {
                v->vertical_adjust_counter = 0;
                v->vertical_adjust_active = false;
                v->row_counter = 0;
                v->raster_in_row = 0;
                v->row_advance_latched = false;
                row_advanced = true;
                frame_restarted = true;
            }
        } else if (v->row_advance_latched) {
            v->row_advance_latched = false;
            v->raster_in_row = 0;
            if (++v->row_counter > v->regs[4]) {
                unsigned adjust = v->regs[5] & 0x1Fu;
                if (adjust) {
                    v->vertical_adjust_active = true;
                    v->vertical_adjust_counter = 0;
                } else {
                    v->row_counter = 0;
                    frame_restarted = true;
                }
            }
            row_advanced = true;
        } else {
            v->raster_in_row = (v->raster_in_row + 1u) & 0x1Fu;
        }
        /* R9 changes in the middle of a row are compared for equality, not
         * treated as an immediate row end (VICE's row-counter latch). */
        if (v->raster_in_row == (v->regs[9] & 0x1F))
            v->row_advance_latched = true;

        if (frame_restarted) {
            if (!v->draw_finished) vdc_latch_addresses(v);
            v->draw_prime = true;
        }

        /* Display and attribute start addresses are sampled after the last
         * displayed row rather than changing in the middle of a frame. */
        if (v->row_counter == (unsigned)v->regs[6] + 1u &&
            v->raster_in_row == 1u) {
            vdc_latch_addresses(v);
        }
        vdc_advance_drawing(v);

        /* The VDC owns its video timing.  Its visible scanout does not start
         * at the VIC-II frame boundary: R7 starts vsync and the PAL monitor
         * resumes at raster zero after the 25-line sync interval.  Keeping a
         * physical output-line counter is also essential for effects which
         * alter R9 while a frame is being scanned. */
        if (row_advanced && v->row_counter == v->regs[7] &&
            !v->vsync_active) {
            v->vsync_active = true;
            v->vsync_counter = 0;
        }
        if (v->vsync_active) {
            if (++v->vsync_counter > 25u) {
                v->vsync_active = false;
                v->vsync_counter = 0;
                v->raster_output_line = 0;
                if (v->display_fb && v->raster_fb_valid) {
                    u32 *completed = v->fb;
                    v->fb = v->display_fb;
                    v->display_fb = completed;
                    v->display_fb_valid = true;
                    v->raster_fb_valid = false;
                }
            } else {
                v->raster_output_line++;
            }
        } else {
            v->raster_output_line++;
        }
    }
    v->raster_line = line;
}

/* --- Text-mode rendering ------------------------------------------------ */

static void render_text(Vdc *v, u32 *pixels, int fbw, int fbh,
                        int cols, int rows, bool attr_mode,
                        bool reverse_screen, u32 fg, u32 bg,
                        int x0, int y0, int x1, int y1,
                        int char_width) {
    (void)fbh;
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

    int draw_w = x1 - x0;
    int draw_h = y1 - y0;
    if (draw_w <= 0 || draw_h <= 0) return;

    for (int y = y0; y < y1; y++) {
        int raster = (y - y0) * total_rasters / draw_h;
        int row = raster / rasters_per_row;
        int glyph_line = raster % rasters_per_row;
        for (int x = x0; x < x1; x++) {
            int source_x = (x - x0) * (cols * char_width) / draw_w;
            int col = source_x / char_width;
            int pixel_in_char = source_x % char_width;
            int bit = (v->regs[25] & 0x10) ? pixel_in_char / 2
                                            : pixel_in_char;
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
                       VDC_PIXEL_MASK[visible_pixels];
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

            pixels[y * fbw + x] = bit < 8 && (bits & (0x80 >> bit))
                ? c_fg : bg;
        }
    }
}

/* In bitmap mode, each displayed raster line consumes one byte per eight
 * pixels. The bitmap address advances by R1 + R27 on every raster, while the
 * colour-attribute address advances only after a character row (R9 + 1
 * rasters). This is distinct from text mode's screen-code/chargen lookup. */
static void render_bitmap(const Vdc *v, u32 *pixels, int fbw, int fbh,
                          int cols, int rows, bool attr_mode,
                          bool reverse_screen, u32 fg, u32 bg,
                          int x0, int y0, int x1, int y1,
                          int char_width) {
    (void)fbh;
    int rasters_per_row = (v->regs[9] & 0x1F) + 1;
    int lines = rows * rasters_per_row;
    int stride = cols + v->regs[27];
    int draw_w = x1 - x0;
    int draw_h = y1 - y0;
    if (draw_w <= 0 || draw_h <= 0) return;

    for (int y = y0; y < y1; y++) {
        int raster = (y - y0) * lines / draw_h;
        int attr_row = (raster / rasters_per_row) * stride;
        int bitmap_row = raster * stride;
        for (int x = x0; x < x1; x++) {
            int source_x = (x - x0) * (cols * char_width) / draw_w;
            int byte_col = source_x / char_width;
            int pixel_in_char = source_x % char_width;
            int source_bit = (v->regs[25] & 0x10) ? pixel_in_char / 2
                                                  : pixel_in_char;
            u8 bits = vdc_ram_read(v, (u16)(v->screen_adr + bitmap_row + byte_col));
            if (reverse_screen) bits = (u8)~bits;
            u32 dot_fg = fg;
            u32 dot_bg = bg;
            if (attr_mode) {
                u8 attr = vdc_ram_read(v, (u16)(v->attribute_adr + attr_row + byte_col));
                dot_fg = VDC_COLORS[attr & 0x0F];
                dot_bg = VDC_COLORS[attr >> 4];
            }
            pixels[y * fbw + x] = source_bit < 8 &&
                (bits & (0x80u >> source_bit)) ? dot_fg : dot_bg;
        }
    }
}

void vdc_render(Vdc *v, u32 *pixels, int fbw, int fbh) {
    if (!pixels || !v->fb) return;

    v->frame_counter++;

    if (fbw == VDC_SCREEN_W && fbh == VDC_SCREEN_H &&
        (v->raster_fb_valid || v->display_fb_valid)) {
        const u32 *scanout = v->display_fb_valid ? v->display_fb : v->fb;
        for (int y = 0; y < fbh; ++y) {
            int sy = y * v->fb_h / fbh;
            const u32 *src = scanout + (size_t)sy * v->fb_w;
            u32 *dst = pixels + (size_t)y * fbw;
            if (fbw == v->fb_w)
                memcpy(dst, src, (size_t)fbw * sizeof(*dst));
            else
                for (int x = 0; x < fbw; ++x)
                    dst[x] = src[x * v->fb_w / fbw];
        }
        v->dirty = false;
        return;
    }

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

    /* R22 high nibble controls total character width; double-pixel mode
     * emits each source dot twice.  Match VICE's character-width rules. */
    int char_width = (v->regs[25] & 0x10)
        ? 2 * (int)(v->regs[22] >> 4)
        : 1 + (int)(v->regs[22] >> 4);
    if (char_width < 1) char_width = 1;

    /* Unit tests and callers requesting a raw-sized surface retain the useful
     * old behavior of fitting the active image to that exact surface. */
    bool fixed_canvas = fbw == VDC_SCREEN_W && fbh == VDC_SCREEN_H;
    if (!fixed_canvas)
        char_width = (v->regs[25] & 0x10) ? 16 : 8;

    int active_w = cols * char_width;
    int hsync = (v->regs[25] & 0x10)
        ? 62 * 16 - (int)v->regs[2] * char_width
        : 116 * 8 - (int)v->regs[2] * char_width;
    if (hsync < 0) hsync = 0;
    if (active_w >= VDC_RASTER_WIDTH) hsync = 0;
    else if (hsync + active_w > VDC_RASTER_WIDTH)
        hsync = VDC_RASTER_WIDTH - active_w;

    int active_h = rows * ((v->regs[9] & 0x1F) + 1);
    int top = (VDC_RASTER_HEIGHT - active_h) / 2;
    if (top < 0) top = 0;
    if (active_h > VDC_RASTER_HEIGHT) active_h = VDC_RASTER_HEIGHT;

    int x0 = fixed_canvas ? hsync * fbw / VDC_RASTER_WIDTH : 0;
    int x1 = fixed_canvas ? (hsync + active_w) * fbw / VDC_RASTER_WIDTH : fbw;
    int y0 = fixed_canvas ? top * fbh / VDC_RASTER_HEIGHT : 0;
    int y1 = fixed_canvas ? (top + active_h) * fbh / VDC_RASTER_HEIGHT : fbh;
    if (x0 < 0) x0 = 0;
    if (x1 > fbw) x1 = fbw;
    if (y0 < 0) y0 = 0;
    if (y1 > fbh) y1 = fbh;

    if (bitmap_mode) {
        render_bitmap(v, pixels, fbw, fbh, cols, rows, attr_mode,
                      reverse_screen, fg, bg, x0, y0, x1, y1, char_width);
        v->dirty = false;
        return;
    }

    render_text(v, pixels, fbw, fbh, cols, rows, attr_mode,
                reverse_screen, fg, bg, x0, y0, x1, y1, char_width);
    v->dirty = false;
}
