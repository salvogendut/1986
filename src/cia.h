#pragma once
#include "types.h"
#include <stdbool.h>

/*
 * MOS 6526 CIA (Complex Interface Adapter).
 *
 * Two CIAs live in the C128 at $DC00 (CIA1: keyboard, joystick, timer A/B,
 * serial) and $DD00 (CIA2: VIC bank, RS-232, timer A/B). This models the
 * interrupt control register (ICR) / interrupt mask register (IMR), both
 * timers, the mains-driven time-of-day clock and alarm, and the serial and
 * FLAG pins. External pin inputs are supplied by the machine when connected;
 * the diagnostic harness is not connected by default.
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
    u8  tod[4];      /* BCD tenths, seconds, minutes, hours + AM/PM */
    u8  tod_alarm[4];
    u8  tod_latch[4];
    u8  tod_pulses;  /* 50/60 Hz pulses since the last tenth */
    bool tod_stopped;
    bool tod_latched;
    u8  sdr;         /* serial data register */
    u8  serial_shift;
    u8  serial_bits;
    bool serial_pending;
    bool serial_active;
    bool sp_input_high;
    bool cnt_input_high;
    bool flag_input_high;
    bool sp_output_high;
    bool cnt_output_high;
    u8  cra;         /* timer A control register */
    u8  crb;         /* timer B control register */
    u16 ta_latch;    /* timer A reload value */
    u16 ta_counter;  /* timer A down counter */
    u16 tb_latch;    /* timer B reload value */
    u16 tb_counter;  /* timer B down counter */
    bool ta_running; /* timer A started */
    bool tb_running; /* timer B started */
    bool ta_underflow; /* timer A reached zero this step */
    bool tb_underflow; /* timer B reached zero this step */
} Cia;

void cia_init(Cia *c);
void cia_reset(Cia *c);
void cia_write(Cia *c, u16 addr, u8 val);
u8   cia_read(Cia *c, u16 addr);
/* Advance both timers by Phi2 cycles; Timer B may instead count Timer A
 * underflows. Returns the current masked interrupt-line state. */
bool cia_tick(Cia *c, int cycles);
/* True while the CIA IRQ line is asserted (a masked flag is pending). */
bool cia_irq_line(const Cia *c);
/* One 50 Hz mains pulse (PAL frame) drives the TOD clock independently of
 * the CPU speed; CRA bit 7 selects five or six pulses per tenth. */
void cia_tod_tick(Cia *c);
/* External user-port inputs. CNT rising edges clock serial input and the
 * CNT-driven timer modes; FLAG interrupts on the falling edge. */
void cia_set_cnt(Cia *c, bool high);
void cia_set_sp(Cia *c, bool high);
void cia_set_flag(Cia *c, bool high);
