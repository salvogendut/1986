#pragma once
#include "types.h"
#include "display.h"

/*
 * MOS 8563 VDC (80-column video chip) for the C128.
 *
 * The VDC drives the 640x200 (text) / 640x400 (high-res) display over a
 * separate connector and has its own character set. This scaffold only
 * carries the register file; the raster pipeline is a TODO.
 */
typedef struct {
    u8  reg;           /* currently selected VDC register (index port) */
    u8  regs[64];      /* VDC register file */
    u16 addr;          /* internal address counter */
    u16 mem[0x8000];   /* 32K VDC video RAM (word-accessed) */
} Vdc;

void vdc_init(Vdc *v);
void vdc_reset(Vdc *v);
void vdc_write_index(Vdc *v, u8 val);   /* $D600 */
void vdc_write_data(Vdc *v, u8 val);    /* $D601 */
u8   vdc_read_data(Vdc *v);
