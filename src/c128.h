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
#include "joyport.h"
#include "drive.h"
#include "drive1571cr.h"
#include "iec_bus.h"
#include "drive_monitor.h"
#include "config.h"
#include <stdbool.h>

/*
 * The Commodore C128DCR machine: 8502 (6502-like) + Z80, MMU-banked memory,
 * VIC-IIe (40-col), VDC 8563 (80-col), two CIAs, SID, and the integrated
 * 1571 disk drives.
 */

#define C128_PAL_FRAME_CYCLES  19656   /* 312 raster lines x 63 cycles at 1 MHz */
#define C128_AUDIO_FRAME_CAPACITY 1024

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
    s16     audio_frame[C128_AUDIO_FRAME_CAPACITY];
    int     audio_count;
    int     sid_fast_remainder;
    Kbd     kbd;
    JoyPorts joyports;
    Drive   drive;
    Drive   drive2;
    Drive1571Cr integrated_drive; /* independent ROM-backed 1571CR machine */
    Drive1571Cr second_real_drive; /* optional second ROM-backed 1571CR */
    IecBus  iec_bus;     /* physical slow IEC pins, separate from VirtualDrive */
    DriveMonitor drive_monitor; /* host-only LED and audio presentation */
    DriveMonitor drive2_monitor;
    unsigned drive_clock_fraction;
    unsigned drive2_clock_fraction;
    u64 drive_host_cycle_synced;
    unsigned drive_clock_denominator;
    unsigned drive_media_generation;
    unsigned drive2_media_generation;
    bool drive_raw_iec; /* opt-in diagnostic: KERNAL serial ROM is unpatched */
    bool drive2_raw_iec; /* second physical 1571 joined to the same IEC bus */
    Config *cfg;
    bool    paused;
    bool    fast;        /* 8502 at 2 MHz (C128 fast mode) */
    bool    col_mode_80; /* persistent 40/80 mode: true = 80-col (survives reset) */
    bool    restore_down; /* RESTORE is an NMI pin, not a keyboard-matrix key */
    int     frames_since_reset; /* frames elapsed since the last reset */
    int     cpu_frame_debt; /* instruction-cycle overrun carried across raster frames */
    u64     total_cycles;
} C128;

void c128_init(C128 *c, Config *cfg);
void c128_reset(C128 *c);
int  c128_frame(C128 *c);      /* run one frame; returns CPU cycles consumed */
u64  c128_cycles_to_ns(const C128 *c, int cycles);
void c128_key_event(C128 *c, int scancode, bool down);
void c128_set_4080(C128 *c, bool col80); /* set the latched 40/80 key and active display */
void c128_switch_4080(C128 *c);   /* toggle 40-column VIC <-> 80-column VDC */

/* IEC serial-bus forwarding (installed via cpu_install_iec_traps). */
void c128_iec_attention(void *ctx, u8 b);
void c128_iec_send(void *ctx, u8 byte);
int  c128_iec_receive(void *ctx, u8 *byte);
u8   c128_iec_take_status(void *ctx);
u8   c128_mem_read(void *ctx, u16 addr);
void c128_mem_write(void *ctx, u16 addr, u8 val);

/* Frame counter (used by the z80.c debug instrumentation and boot trace). */
extern int c128_frame_count;
