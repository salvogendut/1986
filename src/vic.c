#include "vic.h"
#include <stddef.h>
#include <string.h>

/* C64/C128 colour palette (RGB). */
static const u32 VIC_COLORS[16] = {
    0x000000, 0xFFFFFF, 0x813338, 0x75CEC8, 0x8E3C97, 0x56AC4D, 0x2E2C9B, 0xEDF171,
    0x8E5029, 0x553800, 0xC46C71, 0x4A4A4A, 0x7B7B7B, 0xA9FF9F, 0x706DEB, 0xB2B2B2
};

static void vic_capture_state(const Vic *v, const Mem *m,
                              VicRasterState *state) {
    state->d011 = v->vmode;
    state->d016 = v->ctrl1;
    state->d018 = v->ctrl2;
    state->border_color = v->border_color;
    memcpy(state->bg_color, v->bg_color, sizeof(state->bg_color));
    memcpy(state->sprite_x, v->sprite_x, sizeof(state->sprite_x));
    memcpy(state->sprite_y, v->sprite_y, sizeof(state->sprite_y));
    state->sprite_x_msb = v->sprite_x_msb;
    state->sprite_enable = v->sprite_enable;
    state->sprite_y_expand = v->sprite_y_expand;
    state->sprite_priority = v->sprite_priority;
    state->sprite_multicolor = v->sprite_multicolor;
    state->sprite_x_expand = v->sprite_x_expand;
    memcpy(state->sprite_mc, v->sprite_mc, sizeof(state->sprite_mc));
    memcpy(state->sprite_color, v->sprite_color, sizeof(state->sprite_color));
    state->bank_addr = v->bank_addr;
    if (m) {
        u32 screen_base = state->bank_addr + ((state->d018 & 0xF0) << 6);
        for (unsigned sprite = 0; sprite < VIC_SPRITES; sprite++)
            state->sprite_pointer[sprite] =
                m->ram[screen_base + 0x3F8 + sprite];
    } else {
        memset(state->sprite_pointer, 0, sizeof(state->sprite_pointer));
    }
    state->matrix_valid = false;
    state->matrix_row = -1;
    state->matrix_line = 0;
    memset(state->matrix_data, 0, sizeof(state->matrix_data));
    memset(state->color_data, 0, sizeof(state->color_data));
    memset(state->graphics_data, 0, sizeof(state->graphics_data));
}

static const VicRasterState *vic_state_for_line(const Vic *v, const Mem *m,
                                                 unsigned line,
                                                 VicRasterState *fallback) {
    if (line < VIC_RASTER_LINES && v->raster_state_valid[line])
        return &v->raster_state[line];
    vic_capture_state(v, m, fallback);
    return fallback;
}

void vic_init(Vic *v) {
    memset(v, 0, sizeof(*v));
    vic_reset(v);
}

void vic_reset(Vic *v) {
    memset((u8 *)v + offsetof(Vic, beam_half_clock), 0,
           sizeof(*v) - offsetof(Vic, beam_half_clock));
    v->vertical_border = true;
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
    v->keyboard_select = 0xFF;
    v->bank_addr = 0;
    v->prev_raster = 0;
    v->current_raster = 0;
    v->cycles = 0;
    memset(v->raster_state, 0, sizeof(v->raster_state));
    memset(v->raster_state_valid, 0, sizeof(v->raster_state_valid));
    v->fetch_den_latched = false;
    v->fetch_display_state = false;
    v->fetch_matrix_row = -1;
    v->fetch_row_counter = 0;
    v->fetch_matrix_valid = false;
    memset(v->fetch_matrix_data, 0, sizeof(v->fetch_matrix_data));
    memset(v->fetch_color_data, 0, sizeof(v->fetch_color_data));
    v->fast_mode = false;
}

/* Re-evaluate the VIC IRQ line (bit 7) from the pending masked flags. */
static void vic_irq_line_update(Vic *v) {
    if (v->irq_status & v->irq_mask & 0x0F)
        v->irq_status |= 0x80;
    else
        v->irq_status &= 0x7F;
}

static void vic_set_raster_irq_line(Vic *v, u16 line) {
    if (line != v->raster_irq_line)
        v->raster_irq_fired = 0;
    v->raster_irq_line = line;
}

static void vic_badline_write(Vic *v, u8 val) {
    if (!v->clocked) return;
    unsigned line = v->current_raster;
    if (line == 48 && (val & 0x10)) v->fetch_den_latched = true;
    if (!v->fetch_den_latched || line < 48 || line > 247) return;
    bool old = (v->vmode & 7) == (line & 7);
    bool bad = (val & 7) == (line & 7);
    if (old == bad) return;
    unsigned cycle = v->cycle;
    if (bad) {
        if (cycle <= 57) {
            v->bad_line = true;
            if (cycle <= 13) v->fetch_row_counter = 0;
            if (cycle >= 11 && cycle < 54) {
                v->late_fetch = (u8)(cycle < 14 ? 14 : cycle + 3);
                v->vc = (v->vcbase + (v->fetch_display_state && cycle > 14
                                      ? cycle - 14 : 0)) & 0x3ff;
            }
            v->fetch_display_state = true;
        } else v->force_display = true;
        v->rc_checked = true;
    } else {
        if (cycle < 11) v->bad_line = false;
        if (cycle > 0) {
            v->fetch_display_state = true;
            if (cycle > 13 && !v->rc_checked) v->fetch_row_counter = 0;
            v->rc_checked = true;
        }
    }
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
        case 0x11:
            vic_badline_write(v, val);
            v->vmode = val;
            vic_set_raster_irq_line(v,
                (u16)((v->raster_irq_line & 0xFF) | ((val & 0x80) << 1)));
            break;
        case 0x12:
            v->raster = val;
            vic_set_raster_irq_line(v,
                (u16)((v->raster_irq_line & 0x100) | val));
            break;
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
        case 0x2F: v->keyboard_select = val; break;
        case 0x30: v->fast_mode = (val & 0x01) != 0; break;
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

/* The video-clock raster is advanced by the machine scheduler. It must not
 * be derived from CPU cycles because D030 can switch the 8502 to 2 MHz. */
static unsigned vic_raster(const Vic *v) {
    return v->current_raster % VIC_RASTER_LINES;
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
        case 0x11: return (u8)((v->vmode & 0x7F) | ((raster & 0x100) ? 0x80 : 0));
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
        case 0x2F: return v->keyboard_select;
        case 0x30: return (u8)(0xFC | (v->fast_mode ? 1 : 0));
        default: return 0xFF;
    }
}

void vic_set_bank(Vic *v, unsigned bank) {
    v->bank_addr = (u32)(bank & 0x07) << 14;
}

void vic_set_raster_line(Vic *v, unsigned line) {
    v->current_raster = line % VIC_RASTER_LINES;
}

/* Advance raster/IRQ state. Called each raster-line chunk; asserts the raster
 * IRQ once when the raster crosses the compare line. Returns true if the IRQ
 * line is asserted. */
bool vic_tick(Vic *v) {
    unsigned raster = vic_raster(v);

    /* Frame wrapped (raster went past the end): clear the fired latch. */
    if (raster < v->prev_raster)
        v->raster_irq_fired = 0;

    if (raster == v->raster_irq_line && !v->raster_irq_fired) {
        v->irq_status |= 0x01;   /* raster IRQ flag */
        v->raster_irq_fired = 1;
    }
    v->prev_raster = raster;

    vic_irq_line_update(v);
    return (v->irq_status & 0x80) != 0;
}

static u8 vic_fetch_memory_byte(const VicRasterState *state, const Mem *m,
                                u32 physical_addr) {
    u16 vic_addr = (u16)(physical_addr - state->bank_addr);
    bool rom = mem_c64_mode(m)
        ? (physical_addr & 0x7000) == 0x1000
        : (!(m->pla_data & 0x04) && (vic_addr & 0x3000) == 0x1000);
    return rom
        ? m->chargen[(mem_c64_mode(m) ? 0 : 0x1000) +
                     (vic_addr & 0x0FFF)]
        : m->ram[physical_addr];
}

static u8 vic_fetch_text_byte(const VicRasterState *state, const Mem *m,
                              u8 ch, u8 row) {
    if ((state->d011 & 0x60) == 0x40)
        ch &= 0x3F;
    u16 char_addr = (u16)((state->d018 & 0x0E) << 10);
    u16 glyph_addr = (u16)(char_addr + ((u16)ch << 3) + row);
    return vic_fetch_memory_byte(state, m, state->bank_addr + glyph_addr);
}

static void vic_latch_matrix_fetch(Vic *v, const Mem *m, unsigned line,
                                   VicRasterState *state) {
    if (line == 48)
        v->fetch_den_latched = (state->d011 & 0x10) != 0;

    bool badline = v->fetch_den_latched && line >= 48 && line <= 247 &&
                   (line & 0x07) == (state->d011 & 0x07);
    if (badline) {
        if (v->fetch_matrix_row < 0) v->fetch_matrix_row = 0;
        v->fetch_row_counter = 0;
        v->fetch_display_state = true;
        v->fetch_matrix_valid = v->fetch_matrix_row >= 0 &&
                                v->fetch_matrix_row < VIC_CHARS_Y;
        if (v->fetch_matrix_valid) {
            u32 screen_base = state->bank_addr +
                              ((state->d018 & 0xF0) << 6);
            unsigned cell = (unsigned)v->fetch_matrix_row * VIC_CHARS_X;
            unsigned cbank = mem_c64_mode(m) ? 0 :
                             (m->pla_data >> 1) & 0x01;
            for (unsigned cx = 0; cx < VIC_CHARS_X; cx++) {
                v->fetch_matrix_data[cx] = vic_fetch_memory_byte(
                    state, m, screen_base + cell + cx);
                v->fetch_color_data[cx] =
                    m->color_ram[cbank * 0x400 + cell + cx] & 0x0F;
            }
        }
    }

    if (v->fetch_display_state && v->fetch_matrix_valid) {
        state->matrix_valid = true;
        state->matrix_row = v->fetch_matrix_row;
        state->matrix_line = v->fetch_row_counter;
        memcpy(state->matrix_data, v->fetch_matrix_data,
               sizeof(state->matrix_data));
        memcpy(state->color_data, v->fetch_color_data,
               sizeof(state->color_data));
        bool bitmap = (state->d011 & 0x20) != 0;
        for (unsigned cx = 0; cx < VIC_CHARS_X; cx++) {
            if (bitmap) {
                u32 bitmap_addr = state->bank_addr +
                    (((state->d018 & 0x0E) << 10) & 0x2000);
                state->graphics_data[cx] = m->ram[bitmap_addr +
                    (unsigned)v->fetch_matrix_row * 320 + cx * 8 +
                    v->fetch_row_counter];
            } else {
                state->graphics_data[cx] = vic_fetch_text_byte(
                    state, m, v->fetch_matrix_data[cx],
                    v->fetch_row_counter);
            }
        }
    }

    if (v->fetch_display_state) {
        if (v->fetch_row_counter == 7) {
            v->fetch_display_state = false;
            v->fetch_matrix_row++;
        } else
            v->fetch_row_counter++;
    }
}

void vic_begin_frame(Vic *v, const Mem *m) {
    memset(v->raster_state_valid, 0, sizeof(v->raster_state_valid));
    v->fetch_den_latched = false;
    v->fetch_display_state = false;
    v->fetch_matrix_row = -1;
    v->fetch_row_counter = 0;
    v->fetch_matrix_valid = false;
    vic_latch_raster(v, m, 0);
}

void vic_latch_raster(Vic *v, const Mem *m, unsigned line) {
    if (line < VIC_RASTER_LINES) {
        /* At 2 MHz the CPU loop visits each physical raster twice. Refresh
         * its register sample on the second visit, but do not advance the
         * VIC's badline/row-counter state or discard data fetched earlier
         * on that raster. */
        bool already_latched = v->raster_state_valid[line];
        VicRasterState fetched;
        if (already_latched)
            fetched = v->raster_state[line];
        vic_capture_state(v, m, &v->raster_state[line]);
        if (!already_latched) {
            vic_latch_matrix_fetch(v, m, line, &v->raster_state[line]);
        } else {
            v->raster_state[line].matrix_valid = fetched.matrix_valid;
            v->raster_state[line].matrix_row = fetched.matrix_row;
            v->raster_state[line].matrix_line = fetched.matrix_line;
            memcpy(v->raster_state[line].matrix_data, fetched.matrix_data,
                   sizeof(fetched.matrix_data));
            memcpy(v->raster_state[line].color_data, fetched.color_data,
                   sizeof(fetched.color_data));
            memcpy(v->raster_state[line].graphics_data,
                   fetched.graphics_data, sizeof(fetched.graphics_data));
        }
        v->raster_state_valid[line] = true;
    }
}

/* VICE maps sprite X into its normal PAL canvas with left-border-width - 24.
 * Its first displayed PAL raster line is 16, giving the matching Y offset. */
#define VIC_SPRITE_X_OFFSET  (VIC_TEXT_X - 24)
#define VIC_FIRST_VISIBLE_LINE  16

/* PAL cycles are numbered 0..62, as in VICE. Matrix DMA starts with BA at
 * cycle 11, C accesses at 14, and G accesses at 15. VC is reloaded from
 * VCBASE each line; only RC reaching seven commits the next matrix row.
 * In particular FLI can fetch a matrix on every line WITHOUT advancing it
 * by forty characters each time. */
static bool vic_badline_condition(const Vic *v) {
    return v->fetch_den_latched && v->current_raster >= 48 &&
        v->current_raster <= 247 &&
        (v->current_raster & 7) == (v->vmode & 7);
}

bool vic_cpu_ba_low(const Vic *v) {
    if (v->fast_mode) return false;
    if (v->bad_line && v->cycle >= 11 && v->cycle < 54) return true;
    for (unsigned s = 0; s < VIC_SPRITES; s++) {
        /* Three-cycle BA warning followed by the two PHI2 data accesses.
         * Adjacent active sprites naturally join into a single DMA burst. */
        unsigned start = (54 + s * 2) % 63;
        if ((v->sprite_dma & (1u << s)) &&
            (v->cycle + 63 - start) % 63 < 5) return true;
    }
    return false;
}

static void vic_beam_graphics(Vic *v, const Mem *m, unsigned col) {
    unsigned line = v->current_raster;
    u8 screen = v->fetch_matrix_data[col];
    u8 color = v->fetch_color_data[col] & 15;
    unsigned mode = (v->vmode >> 5) & 3;
    VicRasterState s = {.bank_addr = v->bank_addr, .d011 = v->vmode,
                        .d018 = v->ctrl2};
    u8 bits = 0;
    if (v->fetch_display_state) {
        u16 address;
        if (mode & 1) {
            address = ((v->ctrl2 & 8) << 10) |
                      ((v->vc & 0x3ff) << 3) | v->fetch_row_counter;
            bits = vic_fetch_memory_byte(&s, m, v->bank_addr + address);
        } else {
            bits = vic_fetch_text_byte(&s, m, screen, v->fetch_row_counter);
        }
        v->vc = (v->vc + 1) & 0x3ff;
    } else {
        /* Idle isn't a blank bitmap: the VIC repeatedly fetches $3fff
         * ($39ff with ECM) and uses black foreground. RFO's 2 MHz opener
         * deliberately avoids badlines and exposes this idle pattern. */
        bits = vic_fetch_memory_byte(&s, m,
                    v->bank_addr + ((mode & 2) ? 0x39ff : 0x3fff));
        screen = color = 0;
    }
    bool multicolor = (v->ctrl1 & 0x10) &&
        ((mode & 1) || ((color & 8) && mode == 0));
    u8 colors[4] = {v->bg_color[0], v->bg_color[1], v->bg_color[2], color};
    if (mode & 1) {
        colors[1] = screen >> 4;
        colors[2] = screen & 15;
    } else if (multicolor) colors[3] &= 7;
    else if (mode == 2) colors[0] = v->bg_color[screen >> 6];
    unsigned left = VIC_TEXT_X + ((v->ctrl1 & 8) ? 0 : 7);
    unsigned right = VIC_TEXT_X + VIC_TEXT_W - ((v->ctrl1 & 8) ? 0 : 9);
    for (unsigned p = 0; p < 8; p++) {
        unsigned x = VIC_TEXT_X + col * 8 + p + (v->ctrl1 & 7);
        if (x < left || x >= right || v->vertical_border) continue;
        bool fg;
        u8 ink;
        if (multicolor) {
            unsigned code = (bits >> (6 - (p & ~1u))) & 3;
            ink = colors[code]; fg = (code & 2) != 0;
        } else {
            fg = (bits & (0x80 >> p)) != 0;
            ink = (mode & 1) ? (fg ? screen >> 4 : screen & 15)
                             : (fg ? color : colors[0]);
        }
        if (mode == 3 || (mode == 2 && (v->ctrl1 & 0x10))) ink = 0;
        v->beam_pixels[line][x] = (ink & 15) | (fg ? 0x80 : 0);
    }
}

static void vic_beam_cycle(Vic *v, const Mem *m) {
    unsigned line = v->current_raster, cycle = v->cycle;
    if (cycle == 0) {
        if (line == 0) {
            v->fetch_den_latched = false;
            v->fetch_display_state = false;
            v->vcbase = v->vc = 0;
            v->fetch_row_counter = 0;
        }
        if (line == 48 && (v->vmode & 0x10)) v->fetch_den_latched = true;
        v->bad_line = false;
        v->rc_checked = false;
        v->force_display = false;
        v->late_fetch = 0;
        unsigned top = (v->vmode & 8) ? 51 : 55;
        unsigned bottom = (v->vmode & 8) ? 251 : 247;
        if (line == bottom) v->vertical_border = true;
        if (line == top && (v->vmode & 0x10)) v->vertical_border = false;
        memset(v->beam_pixels[line], 0x40 | v->border_color,
               sizeof(v->beam_pixels[line]));
        vic_capture_state(v, m, &v->raster_state[line]);
        v->raster_state_valid[line] = true;
        vic_tick(v);
    }
    if (cycle == 11) {
        v->bad_line = vic_badline_condition(v);
    }
    if (cycle == 14) {
        v->vc = v->vcbase;
        if (v->bad_line) {
            v->fetch_row_counter = 0;
            v->fetch_display_state = true;
            v->rc_checked = true;
        }
    }
    if (cycle >= 14 && cycle < 54 && v->bad_line) {
        unsigned col = cycle - 14;
        unsigned cell = (v->vcbase + col) & 0x3ff;
        VicRasterState s = {.bank_addr = v->bank_addr};
        u32 base = v->bank_addr + ((v->ctrl2 & 0xf0) << 6);
        unsigned cbank = mem_c64_mode(m) ? 0 : (m->pla_data >> 1) & 1;
        v->fetch_matrix_data[col] = cycle < v->late_fetch ? 0xff :
            vic_fetch_memory_byte(&s, m, base + cell);
        v->fetch_color_data[col] = m->color_ram[cbank * 0x400 + cell] & 15;
    }
    if (cycle >= 11 && cycle < 59) {
        unsigned x = (cycle - 11) * 8;
        for (unsigned p = 0; p < 8; p++) {
            bool border = v->vertical_border || x + p < VIC_TEXT_X ||
                x + p >= VIC_TEXT_X + VIC_TEXT_W;
            /* Don't erase pixels spilled here by horizontal fine scrolling. */
            if (v->beam_pixels[line][x + p] & 0x40)
                v->beam_pixels[line][x + p] = border
                    ? (0x40 | v->border_color) : v->bg_color[0];
        }
    }
    if (cycle >= 15 && cycle < 55) vic_beam_graphics(v, m, cycle - 15);
    if (cycle == 54) {
        vic_capture_state(v, m, &v->raster_state[line]);
        v->sprite_line_active[(line + 1) % VIC_RASTER_LINES] = 0;
        for (unsigned s = 0; s < VIC_SPRITES; s++) {
            unsigned bit = 1u << s;
            if ((v->sprite_enable & bit) && v->sprite_y[s] == (line & 255)) {
                v->sprite_dma |= bit;
                v->sprite_dma_row[s] = 0;
                v->sprite_dma_repeat &= ~bit;
            } else if (v->sprite_dma & bit) {
                if (!(v->sprite_y_expand & bit) || (v->sprite_dma_repeat & bit))
                    if (++v->sprite_dma_row[s] == 21) v->sprite_dma &= ~bit;
                v->sprite_dma_repeat ^= bit;
            }
        }
    }
    /* Sprite 0..2 fetch at the right edge; 3..7 after the line wraps.
     * Once DMA starts, changing Y schedules a subsequent appearance: it
     * must not relocate the sprite rows already being displayed. Fetch
     * the pointer and pattern here, before a later D018/RAM write. */
    for (unsigned s = 0; s < VIC_SPRITES; s++) {
        unsigned fetch = (57 + 2 * s) % 63;
        if (cycle != fetch || !(v->sprite_dma & (1u << s))) continue;
        unsigned target = cycle >= 57 ? (line + 1) % VIC_RASTER_LINES : line;
        VicRasterState state = {.bank_addr = v->bank_addr};
        unsigned screen = (v->ctrl2 & 0xf0) << 6;
        u8 pointer = vic_fetch_memory_byte(&state, m,
                          v->bank_addr + screen + 0x3f8 + s);
        unsigned address = v->bank_addr + (pointer << 6) +
                           v->sprite_dma_row[s] * 3;
        u32 bits = 0;
        for (unsigned b = 0; b < 3; b++)
            bits = (bits << 8) | vic_fetch_memory_byte(&state, m, address + b);
        v->sprite_line_data[target][s] = bits;
        v->sprite_line_active[target] |= 1u << s;
    }
    if (cycle == 57) {
        if (v->fetch_row_counter == 7) {
            v->vcbase = v->vc;
            v->fetch_display_state = false;
        }
        if (v->fetch_display_state || v->bad_line) {
            v->fetch_row_counter = (v->fetch_row_counter + 1) & 7;
            v->fetch_display_state = true;
        }
        if (v->force_display) v->fetch_display_state = true;
    }
}

void vic_clock_half(Vic *v, const Mem *m) {
    if (!v->clocked) {
        v->clocked = true;
        v->current_raster = (v->beam_half_clock / 126) % VIC_RASTER_LINES;
        v->cycle = (v->beam_half_clock / 2) % 63;
        vic_beam_cycle(v, m);
    }
    ++v->beam_half_clock;
    if (!(v->beam_half_clock & 1)) {
        v->current_raster = (v->beam_half_clock / 126) % VIC_RASTER_LINES;
        v->cycle = (v->beam_half_clock / 2) % 63;
        vic_beam_cycle(v, m);
    }
}

static void vic_draw_sprites(Vic *v, Mem *m, Display *d, const u8 *foreground) {
    u8 occupied[C128_SCREEN_W * C128_SCREEN_H];
    memset(occupied, 0, sizeof(occupied));

    u8 sprite_sprite = 0;
    u8 sprite_background = 0;
    for (int dy = VIC_TEXT_Y; dy < VIC_TEXT_Y + VIC_TEXT_H; dy++) {
        unsigned raster = (unsigned)(dy + VIC_FIRST_VISIBLE_LINE);
        VicRasterState fallback;
        const VicRasterState *state =
            vic_state_for_line(v, m, raster, &fallback);

        /* Draw 7 first and 0 last: lower-numbered sprites have priority. */
        for (int sprite = VIC_SPRITES - 1; sprite >= 0; sprite--) {
            u8 sprite_bit = (u8)(1u << sprite);
            if (!((v->clocked ? v->sprite_line_active[raster]
                             : state->sprite_enable) & sprite_bit))
                continue;

            unsigned x = state->sprite_x[sprite]
                       | ((state->sprite_x_msb & sprite_bit) ? 0x100u : 0u);
            int origin_x = (int)x + VIC_SPRITE_X_OFFSET;
            int origin_y = (int)state->sprite_y[sprite] - VIC_FIRST_VISIBLE_LINE;
            int x_scale = (state->sprite_x_expand & sprite_bit) ? 2 : 1;
            int y_scale = (state->sprite_y_expand & sprite_bit) ? 2 : 1;
            int source_y = (dy - origin_y) / y_scale;
            if (!v->clocked && (dy < origin_y || source_y < 0 || source_y >= 21))
                continue;
            bool multicolor = (state->sprite_multicolor & sprite_bit) != 0;
            bool behind = (state->sprite_priority & sprite_bit) != 0;
            u8 pointer = state->sprite_pointer[sprite];
            u32 data_base = state->bank_addr + ((u32)pointer << 6);
            u32 bits = v->clocked ? v->sprite_line_data[raster][sprite] :
                       ((u32)m->ram[data_base + source_y * 3] << 16)
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
                    color = code == 1 ? state->sprite_mc[0]
                          : code == 2 ? state->sprite_color[sprite]
                                      : state->sprite_mc[1];
                } else {
                    code = (u8)((bits >> (23 - source_x)) & 0x01);
                    if (code == 0) continue;
                    color = state->sprite_color[sprite];
                }

                int pixel_width = logical_width * x_scale;
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
    if (v->clocked) {
        u8 foreground[C128_SCREEN_W * C128_SCREEN_H];
        for (unsigned y = 0; y < C128_SCREEN_H; y++) {
            for (unsigned x = 0; x < C128_SCREEN_W; x++) {
                u8 sample = v->beam_pixels[y + VIC_FIRST_VISIBLE_LINE][x];
                unsigned off = y * C128_SCREEN_W + x;
                d->pixels[off] = VIC_COLORS[sample & 15];
                foreground[off] = (sample & 0x80) != 0;
            }
        }
        vic_draw_sprites(v, m, d, foreground);
        return;
    }
    u8 foreground[C128_SCREEN_W * C128_SCREEN_H];
    int matrix_row[VIC_RASTER_LINES];
    u8 matrix_line[VIC_RASTER_LINES];
    u32 matrix_base[VIC_RASTER_LINES];
    memset(foreground, 0, sizeof(foreground));
    for (unsigned line = 0; line < VIC_RASTER_LINES; line++)
        matrix_row[line] = -1;

    /* Follow the VIC-II's badline-driven row counter in raster order. A
     * mid-frame YSCROLL write only changes later badline comparisons; it
     * does not retroactively remap every following scanline. Screen/color
     * matrix data is fetched on a badline and remains buffered while RC runs
     * from 0 to 7, whereas character/bitmap data follows $D018 each line. */
    bool den_latched = false;
    bool display_state = false;
    int row = -1;
    u8 row_counter = 0;
    u32 screen_base = 0;
    for (unsigned line = 0; line < VIC_RASTER_LINES; line++) {
        VicRasterState fallback;
        const VicRasterState *state =
            vic_state_for_line(v, m, line, &fallback);
        if (line == 48)
            den_latched = (state->d011 & 0x10) != 0;
        bool badline = den_latched && line >= 48 && line <= 247 &&
                       (line & 0x07) == (state->d011 & 0x07);
        if (badline) {
            if (row < 0) row = 0;
            row_counter = 0;
            display_state = true;
            screen_base = state->bank_addr + ((state->d018 & 0xF0) << 6);
        }
        if (display_state && row >= 0 && row < VIC_CHARS_Y) {
            matrix_row[line] = row;
            matrix_line[line] = row_counter;
            matrix_base[line] = screen_base;
        }
        if (display_state) {
            if (row_counter == 7) {
                display_state = false;
                row++;
            } else
                row_counter++;
        }
    }

    /* Render each line from its sampled registers. Bitmap mode uses $D011
     * bit 5. $D018 bit 3 selects the 8K
     * bitmap, while bits 4-7 select the 1K screen matrix. In hires mode each
     * screen byte supplies both colours for its 8x8 cell: the high nibble for
     * bitmap bit 1 and the low nibble for bit 0. In multicolor mode the four
     * sources for pixel values 00..11 are $D021, screen high nibble, screen
     * low nibble, and colour RAM respectively. */
    for (int dy = 0; dy < C128_SCREEN_H; dy++) {
        unsigned raster = (unsigned)(dy + VIC_FIRST_VISIBLE_LINE);
        VicRasterState fallback;
        const VicRasterState *state =
            vic_state_for_line(v, m, raster, &fallback);
        u32 border = VIC_COLORS[state->border_color & 0x0F];
        for (int dx = 0; dx < C128_SCREEN_W; dx++)
            d->pixels[dy * C128_SCREEN_W + dx] = border;

        /* RSEL selects a 25-row window on raster lines 51..250 or a 24-row
         * window on 55..246. */
        unsigned display_first = (state->d011 & 0x08) ? 51u : 55u;
        unsigned display_last = (state->d011 & 0x08) ? 251u : 247u;
        if (raster < display_first || raster >= display_last ||
            !(state->d011 & 0x10))
            continue;

        u32 bg = VIC_COLORS[state->bg_color[0] & 0x0F];
        for (int dx = VIC_TEXT_X; dx < VIC_TEXT_X + VIC_TEXT_W; dx++)
            d->pixels[dy * C128_SCREEN_W + dx] = bg;
        bool fetched = v->raster_state_valid[raster] && state->matrix_valid;
        if (!fetched && matrix_row[raster] < 0)
            continue;
        int cy = fetched ? state->matrix_row : matrix_row[raster];
        int py = fetched ? state->matrix_line : matrix_line[raster];

        u32 screen_base = fetched ? 0 : matrix_base[raster];
        u32 bitmap_addr = state->bank_addr +
            (((state->d018 & 0x0E) << 10) & 0x2000);
        u16 char_addr = (u16)((state->d018 & 0x0E) << 10);
        bool bitmap = (state->d011 & 0x20) != 0;
        bool multicolor = (state->d016 & 0x10) != 0;
        bool extended = (state->d011 & 0x40) != 0;

        for (int cx = 0; cx < VIC_CHARS_X; cx++) {
            u16 cell_index = (u16)(cy * VIC_CHARS_X + cx);
            u32 cell = screen_base + cell_index;
            u8 screen = fetched ? state->matrix_data[cx] :
                vic_fetch_memory_byte(state, m, cell);
            unsigned cbank = mem_c64_mode(m) ? 0 :
                             (m->pla_data >> 1) & 0x01;
            u8 cram = fetched ? state->color_data[cx] :
                m->color_ram[cbank * 0x400 + (cell_index & 0x3FF)] & 0x0F;

            if (bitmap && !extended) {
                u8 bits = fetched ? state->graphics_data[cx] :
                    m->ram[bitmap_addr + cy * 320 + cx * 8 + py];
                if (!multicolor) {
                    u32 fg = VIC_COLORS[screen >> 4];
                    u32 cell_bg = VIC_COLORS[screen & 0x0F];
                    for (int px = 0; px < 8; px++) {
                        int dx = VIC_TEXT_X + cx * 8 + px;
                        unsigned off = (unsigned)(dy * C128_SCREEN_W + dx);
                        bool set = (bits & (0x80 >> px)) != 0;
                        d->pixels[off] = set ? fg : cell_bg;
                        foreground[off] = set;
                    }
                } else {
                    u32 colors[4] = {
                        bg, VIC_COLORS[screen >> 4],
                        VIC_COLORS[screen & 0x0F], VIC_COLORS[cram]
                    };
                    for (int px = 0; px < 4; px++) {
                        u8 code = (u8)((bits >> (6 - px * 2)) & 0x03);
                        int dx = VIC_TEXT_X + cx * 8 + px * 2;
                        unsigned off = (unsigned)(dy * C128_SCREEN_W + dx);
                        d->pixels[off] = colors[code];
                        d->pixels[off + 1] = colors[code];
                        foreground[off] = foreground[off + 1] =
                            (code & 0x02) != 0;
                    }
                }
            } else if (!bitmap) {
                u8 ch = screen;
                u8 bg_index = 0;
                if (extended) {
                    bg_index = ch >> 6;
                    ch &= 0x3F;
                }
                /* Native C128 PLA: $01 bit 2 low maps the character ROM into
                 * the VIC's $1000-$1FFF window. Else glyph data comes from
                 * the selected VIC RAM bank at the $D018 character pointer.
                 * The International/US machine uses the upper 4K ROM half. */
                u16 glyph_addr = (u16)(char_addr + ((u16)ch << 3));
                u32 physical_glyph = state->bank_addr + glyph_addr;
                bool rom = mem_c64_mode(m)
                    ? (physical_glyph & 0x7000) == 0x1000
                    : (!(m->pla_data & 0x04) &&
                       (glyph_addr & 0x3000) == 0x1000);
                const u8 *glyph = rom
                    ? &m->chargen[(mem_c64_mode(m) ? 0 : 0x1000) +
                                  (glyph_addr & 0x0FFF)]
                    : &m->ram[state->bank_addr + glyph_addr];
                u8 bits = fetched ? state->graphics_data[cx] : glyph[py];
                bool text_multicolor = multicolor && (cram & 0x08) && !extended;
                if (text_multicolor) {
                    u32 colors[4] = {
                        VIC_COLORS[state->bg_color[0] & 0x0F],
                        VIC_COLORS[state->bg_color[1] & 0x0F],
                        VIC_COLORS[state->bg_color[2] & 0x0F],
                        VIC_COLORS[cram & 0x07]
                    };
                    for (int px = 0; px < 4; px++) {
                        u8 code = (u8)((bits >> (6 - px * 2)) & 0x03);
                        int dx = VIC_TEXT_X + cx * 8 + px * 2;
                        unsigned off = (unsigned)(dy * C128_SCREEN_W + dx);
                        d->pixels[off] = colors[code];
                        d->pixels[off + 1] = colors[code];
                        foreground[off] = foreground[off + 1] =
                            (code & 0x02) != 0;
                    }
                } else {
                    u32 fg = VIC_COLORS[cram];
                    u32 cell_bg = VIC_COLORS[state->bg_color[bg_index] & 0x0F];
                    for (int px = 0; px < 8; px++) {
                        int dx = VIC_TEXT_X + cx * 8 + px;
                        unsigned off = (unsigned)(dy * C128_SCREEN_W + dx);
                        bool set = (bits & (0x80 >> px)) != 0;
                        d->pixels[off] = set ? fg : cell_bg;
                        foreground[off] = set;
                    }
                }
            }
        }
    }

    vic_draw_sprites(v, m, d, foreground);
}
