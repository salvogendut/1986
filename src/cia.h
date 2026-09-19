#pragma once
#include "types.h"

/*
 * MOS 6526 CIA (Complex Interface Adapter).
 *
 * Two CIAs live in the C128 at $DC00 (CIA1: keyboard, joystick, timer A/B,
 * serial) and $DD00 (CIA2: VIC bank, RS-232, timer A/B, 6522-style).
 * The scaffold carries the timer registers; interrupt and port logic is a
 * TODO.
 */
typedef struct {
    u8  pra, prb;    /* peripheral data registers */
    u8  ddra, ddrb;  /* data direction registers */
    u8  icr;         /* interrupt control register */
    u8  imr;         /* interrupt mask register */
    u8  ta_lo, ta_hi;
    u8  tb_lo, tb_hi;
    u8  tod;         /* time-of-day */
} Cia;

void cia_init(Cia *c);
void cia_reset(Cia *c);
void cia_write(Cia *c, u16 addr, u8 val);
u8   cia_read(Cia *c, u16 addr);
