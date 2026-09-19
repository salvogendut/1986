#pragma once
#include "types.h"
#include "display.h"
#include "mem.h"

/*
 * MOS 8564 VIC-IIe (40-column video chip) for the C128.
 *
 * The VIC-IIe drives the 320x200 display in 40-column mode (40x25
 * characters, 8x8 glyphs from the character ROM, colour from the nibble RAM
 * at $D800). The raster counter is derived from the CPU cycle count so the
 * KERNAL's raster-wait loops see the scanline advance.
 */

#define VIC_CHARS_X  40
#define VIC_CHARS_Y  25
#define VIC_RASTER_LINES  312   /* PAL: 312 raster lines per frame */

typedef struct {
    u8  border_color;
    u8  bg_color[4];
    u8  bg_color_idx;
    u8  vmode;         /* video mode ($D011) */
    u8  raster;        /* low 8 bits of raster ($D012) */
    u8  ctrl1;         /* $D016 */
    u8  ctrl2;         /* $D018 */
    u8  memory;        /* $D018-derived screen/char pointers */
    u16 screen_addr;   /* current screen RAM base (bank + pointer) */
    u16 char_addr;     /* current char ROM base */
    u8  irq_status;    /* $D019 (bit 0 = raster, bit 7 = IRQ line) */
    u8  irq_mask;      /* $D01A (bit 0 = raster IRQ enable) */
    u8  raster_irq_line; /* raster line for the IRQ compare */
    u64  cycles;       /* raster cycle counter */
} Vic;

void vic_init(Vic *v);
void vic_reset(Vic *v);
void vic_write(Vic *v, u16 addr, u8 val);
u8   vic_read(Vic *v, u16 addr);
/* Advance raster/IRQ state and return true if the raster IRQ line is now
 * asserted. Called once per frame. */
bool vic_tick(Vic *v);
/* Render one full frame (raster 0..199) into the display buffer. */
void vic_render(Vic *v, Mem *m, Display *d);
