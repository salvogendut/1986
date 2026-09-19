#pragma once
#include "types.h"
#include <stdbool.h>

/*
 * MOS 6526 CIA (Complex Interface Adapter).
 *
 * Two CIAs live in the C128 at $DC00 (CIA1: keyboard, joystick, timer A/B,
 * serial) and $DD00 (CIA2: VIC bank, RS-232, timer A/B). This models the
 * interrupt control register (ICR) / interrupt mask register (IMR) and the
 * timer A used for the 50 Hz jiffy clock the KERNAL main loop runs on.
 *
 * Interrupt flags (ICR bits): 0 = timer A, 1 = timer B, 2 = TOD alarm,
 * 3 = serial (SDR), 4 = FLAG, 7 = IRQ line.
 */
typedef struct {
    u8  pra, prb;    /* peripheral data registers */
    u8  ddra, ddrb;  /* data direction registers */
    u8  icr;         /* interrupt control register (flags; reading clears it) */
    u8  imr;         /* interrupt mask register (set/cleared via $DC0D write) */
    u8  ta_lo, ta_hi;
    u8  tb_lo, tb_hi;
    u8  tod;         /* time-of-day */
    u8  cra;         /* timer A control register */
    u8  crb;         /* timer B control register */
    u16 ta_latch;    /* timer A reload value */
    u16 ta_counter;  /* timer A down counter */
    bool ta_running; /* timer A started */
    bool ta_underflow; /* timer A reached zero this step */
} Cia;

void cia_init(Cia *c);
void cia_reset(Cia *c);
void cia_write(Cia *c, u16 addr, u8 val);
u8   cia_read(Cia *c, u16 addr);
/* Advance timer A by `cycles`; returns true if it underflowed and the IRQ is
 * now asserted (icr & imr). */
bool cia_tick(Cia *c, int cycles);
/* True while the CIA IRQ line is asserted (a masked flag is pending). */
bool cia_irq_line(const Cia *c);
