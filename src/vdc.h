#pragma once
#include "types.h"
#include "display.h"
#include <stdbool.h>

/*
 * MOS 8563 VDC (80-column video chip) for the C128.
 *
 * The VDC drives the 640x200 (text) / 640x400 (high-res) display and has its
 * own character set and video RAM. This is a frame-based emulation: the
 * register interface ($D600 index / $D601 data) and the 16K/64K video RAM are
 * modelled, and the active text screen is rendered into a framebuffer once per
 * frame. Bitmap and interlace modes are not yet implemented.
 */

#define VDC_RAM_SIZE     0x10000   /* 64K of VDC video RAM (C128DCR) */
#define VDC_MAX_TEXTCOLS 100
#define VDC_MAX_TEXTLINES 32
#define VDC_MAX_COLS     80
#define VDC_MAX_LINES    25
#define VDC_CHAR_WIDTH   8         /* pixels per character cell (horizontal) */
#define VDC_CHAR_HEIGHT  8         /* visible pixels per character cell */

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

    unsigned screen_text_cols;  /* characters per line (R1) */
    unsigned screen_textlines;  /* visible rows (R6) */
    unsigned bytes_per_char;    /* chargen bytes per character (R9) */

    u8  ram[VDC_RAM_SIZE];      /* VDC video RAM (byte-addressed model) */

    u32 *fb;                    /* rendered framebuffer (fb_w x fb_h) */
    int  fb_w, fb_h;

    bool dirty;                 /* force a re-render */
} Vdc;

void vdc_init(Vdc *v);
void vdc_reset(Vdc *v);
void vdc_write_index(Vdc *v, u8 val);   /* $D600 */
void vdc_write_data(Vdc *v, u8 val);    /* $D601 */
u8   vdc_read_data(Vdc *v);             /* $D601 */
u8   vdc_read_status(const Vdc *v);     /* $D600 */
void vdc_render(Vdc *v, u32 *pixels, int w, int h);
