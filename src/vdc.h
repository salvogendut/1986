#pragma once
#include "types.h"
#include "display.h"
#include <stdbool.h>

/*
 * MOS 8563 VDC (80-column video chip) for the C128.
 *
 * The VDC drives the 640x200 (text) / 640x400 (high-res) display and has its
 * own character set and video RAM. This is a frame-based emulation: the
 * register interface ($D600 index / $D601 data), selectable 16K/64K fitted
 * video RAM, and R28 memory addressing are modelled. Text and standard bitmap
 * modes are rendered once per frame; ready/VBLANK timing is approximate.
 * Interlace and cycle-accurate raster timing are not yet implemented.
 */

#define VDC_RAM_SIZE     0x10000   /* 64K of VDC video RAM (C128DCR) */
#define VDC_MAX_TEXTCOLS 100
#define VDC_MAX_TEXTLINES 50
#define VDC_MAX_COLS     100
#define VDC_MAX_LINES    50
#define VDC_CHAR_WIDTH   8         /* pixels per character cell (horizontal) */
#define VDC_CHAR_HEIGHT  8         /* default rasters per character cell */

#define VDC_ATTR_FLASH     0x10
#define VDC_ATTR_UNDERLINE 0x20
#define VDC_ATTR_REVERSE   0x40
#define VDC_ATTR_ALTCHARSET 0x80

typedef struct {
    u8  reg;           /* currently selected VDC register (index port) */
    u8  regs[64];      /* VDC register file */
    u16 update_adr;    /* R18/19 update (data) address */
    u16 screen_adr;    /* R12/13 display start address */
    u16 attribute_adr; /* R20/21 attribute start address */
    u16 chargen_adr;   /* R28 character-generator address */
    u16 cursor_adr;    /* R14/15 cursor location */

    int  frame_counter;  /* incremented each frame (cursor/attribute blink) */
    unsigned raster_line; /* current PAL scan line for the status register */
    u64 bus_clock;       /* 8502 clock at the latest VDC port access */
    u64 ready_clock;     /* approximate end of the current VDC operation */
    unsigned clock_scale; /* 8502 clocks per nominal VDC bus clock */

    unsigned screen_text_cols;  /* characters per line (R1) */
    unsigned screen_textlines;  /* visible rows (R6) */
    unsigned bytes_per_char;    /* chargen bytes per character (R9) */
    u16 address_mask;           /* fitted RAM: $3FFF (16K) or $FFFF (64K) */

    u8  ram[VDC_RAM_SIZE];      /* VDC video RAM (byte-addressed model) */

    u32 *fb;                    /* rendered framebuffer (fb_w x fb_h) */
    int  fb_w, fb_h;

    bool dirty;                 /* force a re-render */
} Vdc;

void vdc_init(Vdc *v);
void vdc_reset(Vdc *v);
void vdc_set_ram_size_kb(Vdc *v, int kb);
void vdc_write_index(Vdc *v, u8 val);   /* $D600 */
void vdc_write_data(Vdc *v, u8 val);    /* $D601 */
u8   vdc_read_data(Vdc *v);             /* $D601 */
u8   vdc_read_status(const Vdc *v);     /* $D600 */
void vdc_set_raster_line(Vdc *v, unsigned line);
void vdc_set_bus_clock(Vdc *v, u64 clock, bool fast_cpu);
void vdc_render(Vdc *v, u32 *pixels, int w, int h);
