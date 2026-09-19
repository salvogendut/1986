#pragma once
#include "types.h"
#include "display.h"

/*
 * MOS 8564 VIC-IIe (40-column video chip) for the C128.
 *
 * The VIC-IIe drives the 320x200 display in 40-column mode. This scaffold
 * renders a static test frame (border + background + a small colour grid)
 * until the full raster/character pipeline is wired to the character ROM and
 * the 40x25 screen RAM.
 */

#define VIC_CHARS_X  40
#define VIC_CHARS_Y  25

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
    u64  cycles;       /* raster cycle counter */
} Vic;

void vic_init(Vic *v);
void vic_reset(Vic *v);
void vic_write(Vic *v, u16 addr, u8 val);
u8   vic_read(Vic *v, u16 addr);
/* Render one full frame (raster 0..199) into the display buffer. */
void vic_render(Vic *v, Display *d);
