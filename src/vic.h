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
#define VIC_SPRITES  8

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
    u16 char_addr;     /* $D018 character-data base within the 16K VIC window */
    u8  irq_status;    /* $D019 (bit 0 = raster, bit 7 = IRQ line) */
    u8  irq_mask;      /* $D01A (bit 0 = raster IRQ enable) */
    u16 raster_irq_line; /* 9-bit raster line for the IRQ compare */
    u8  raster_irq_fired; /* current compare has already matched */
    u8  sprite_x[VIC_SPRITES];
    u8  sprite_y[VIC_SPRITES];
    u8  sprite_x_msb;       /* $D010 */
    u8  sprite_enable;      /* $D015 */
    u8  sprite_y_expand;    /* $D017 */
    u8  sprite_priority;    /* $D01B: one = behind foreground graphics */
    u8  sprite_multicolor;  /* $D01C */
    u8  sprite_x_expand;    /* $D01D */
    u8  sprite_sprite_collision;     /* $D01E, cleared by read */
    u8  sprite_background_collision; /* $D01F, cleared by read */
    u8  sprite_mc[2];       /* shared colours $D025/$D026 */
    u8  sprite_color[VIC_SPRITES];   /* individual colours $D027-$D02E */
    u32 bank_addr;          /* MMU/CIA2-selected 16K VIC RAM window */
    unsigned prev_raster; /* previous raster line (for wrap detection) */
    u64  cycles;       /* raster cycle counter */
    u8   raster_ctrl2[VIC_RASTER_LINES]; /* $D018 at each raster line */
    bool raster_ctrl2_valid;
    bool fast_mode;     /* VIC-IIe $D030 bit 0: 8502 requests 2 MHz */
} Vic;

void vic_init(Vic *v);
void vic_reset(Vic *v);
void vic_write(Vic *v, u16 addr, u8 val);
void vic_write_rmw(Vic *v, u16 addr, u8 val);
u8   vic_read(Vic *v, u16 addr);
/* Select one of the eight 16K VIC windows in the C128's 128K RAM. */
void vic_set_bank(Vic *v, unsigned bank);
/* Advance raster/IRQ state and return true if the raster IRQ line is now
 * asserted. Called once per raster-line chunk. */
bool vic_tick(Vic *v);
void vic_latch_raster(Vic *v, unsigned line);
/* Render one full frame (raster 0..199) into the display buffer. */
void vic_render(Vic *v, Mem *m, Display *d);
