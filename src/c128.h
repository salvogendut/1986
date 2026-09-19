#pragma once
#include "types.h"
#include "display.h"
#include "mem.h"
#include "cpu.h"
#include "z80.h"
#include "vic.h"
#include "vdc.h"
#include "cia.h"
#include "sid.h"
#include "kbd.h"
#include "config.h"
#include <stdbool.h>

/*
 * The Commodore C128DCR machine: 8502 (6502-like) + Z80, MMU-banked memory,
 * VIC-IIe (40-col), VDC 8563 (80-col), two CIAs, SID, and the integrated
 * 1571 disk drives.
 */

#define C128_PAL_FRAME_CYCLES  19656   /* 312 raster lines x 63 cycles at 1 MHz */

typedef struct {
    Display display;
    Mem     mem;
    Cpu8502 cpu;
    Z80     z80;
    Z80Bus  z80_bus;
    Vic     vic;
    Vdc     vdc;
    Cia     cia1, cia2;
    Sid     sid;
    Kbd     kbd;
    Config *cfg;
    bool    paused;
    bool    fast;        /* 8502 at 2 MHz (C128 fast mode) */
    u64     total_cycles;
} C128;

void c128_init(C128 *c, Config *cfg);
void c128_reset(C128 *c);
int  c128_frame(C128 *c);      /* run one frame; returns CPU cycles consumed */
u64  c128_cycles_to_ns(const C128 *c, int cycles);
void c128_key_event(C128 *c, int scancode, bool down);
u8   c128_mem_read(void *ctx, u16 addr);
void c128_mem_write(void *ctx, u16 addr, u8 val);

/* Frame counter (used by the z80.c debug instrumentation and boot trace). */
extern int c128_frame_count;
