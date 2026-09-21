#pragma once
#include "types.h"
#include <stdbool.h>

/* MOS 6522 VIA subset used by the 1571CR. All register accesses and timers
 * are instruction-boundary timed; the drive will gain sub-instruction bus
 * timing alongside the physical IEC implementation. */
typedef void (*Via6522PortChange)(void *ctx, unsigned port, u8 pins);

typedef struct {
    u8 ora, orb, ddra, ddrb;
    u8 input_a, input_b;
    u8 acr, pcr, sr, ifr, ier;
    u16 t1_latch, t2_latch;
    u32 t1_count, t2_count; /* clocks until underflow, 1..65536 */
    bool t1_running, t2_running;
    bool ca1, ca2, cb1, cb2;
    bool ca2_out, cb2_out;
    u8 pb7;
    Via6522PortChange port_change;
    void *port_ctx;
} Via6522;

void via6522_init(Via6522 *via);
void via6522_reset(Via6522 *via);
void via6522_set_port_hook(Via6522 *via, Via6522PortChange hook, void *ctx);
u8 via6522_read(Via6522 *via, u16 reg);
void via6522_write(Via6522 *via, u16 reg, u8 value);
void via6522_tick(Via6522 *via, unsigned cycles);
void via6522_set_input_a(Via6522 *via, u8 pins);
void via6522_set_input_b(Via6522 *via, u8 pins);
void via6522_set_ca1(Via6522 *via, bool level);
void via6522_set_ca2(Via6522 *via, bool level);
void via6522_set_cb1(Via6522 *via, bool level);
void via6522_set_cb2(Via6522 *via, bool level);
bool via6522_irq(const Via6522 *via);
u8 via6522_output_a(const Via6522 *via);
u8 via6522_output_b(const Via6522 *via);
