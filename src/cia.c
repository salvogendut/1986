#include "cia.h"
#include <string.h>

#define CIA_ICR_TA    0x01   /* timer A underflow */
#define CIA_ICR_TB    0x02   /* timer B underflow */
#define CIA_ICR_IRQ   0x80   /* IRQ output line */

static void cia_update_irq(Cia *c) {
    if (c->icr & c->imr & 0x7F) c->icr |= CIA_ICR_IRQ;
    else c->icr &= (u8)~CIA_ICR_IRQ;
}

/* Advance a 16-bit timer by clock events. Zero represents a full 65536-count
 * period, not an immediate underflow. Processing the whole batch preserves
 * Timer-A cascade pulses even when a frame is stepped in 63-cycle chunks. */
static int timer_advance(u16 *counter, u16 latch, bool *running,
                         u8 *control, int clocks) {
    if (!*running || clocks <= 0) return 0;
    int underflows = 0;
    while (clocks > 0) {
        int until_underflow = *counter ? *counter : 65536;
        if (clocks < until_underflow) {
            *counter = (u16)(until_underflow - clocks);
            break;
        }
        clocks -= until_underflow;
        *counter = latch;
        underflows++;
        if (*control & 0x08) { /* one-shot: clear START after underflow */
            *running = false;
            *control &= (u8)~0x01;
            break;
        }
    }
    return underflows;
}

void cia_init(Cia *c) {
    memset(c, 0, sizeof(*c));
    cia_reset(c);
}

void cia_reset(Cia *c) {
    c->pra = c->prb = 0xFF;
    c->ddra = c->ddrb = 0x00;
    c->icr = 0x00;
    c->imr = 0x00;
    c->ta_lo = c->ta_hi = c->tb_lo = c->tb_hi = 0x00;
    c->tod = 0x00;
    c->cra = c->crb = 0x00;
    c->ta_latch = 0;
    c->ta_counter = 0;
    c->tb_latch = 0;
    c->tb_counter = 0;
    c->ta_running = false;
    c->tb_running = false;
    c->ta_underflow = false;
    c->tb_underflow = false;
}

void cia_write(Cia *c, u16 addr, u8 val) {
    switch (addr & 0x0F) {
        case 0x00: c->pra = val; break;
        case 0x01: c->prb = val; break;
        case 0x02: c->ddra = val; break;
        case 0x03: c->ddrb = val; break;
        case 0x04: c->ta_lo = val; c->ta_latch = (u16)((c->ta_latch & 0xFF00) | val); break;
        case 0x05: c->ta_hi = val; c->ta_latch = (u16)((c->ta_latch & 0x00FF) | (val << 8));
                   if (!c->ta_running) c->ta_counter = c->ta_latch;
                   break;
        case 0x06: c->tb_lo = val; c->tb_latch = (u16)((c->tb_latch & 0xFF00) | val); break;
        case 0x07: c->tb_hi = val; c->tb_latch = (u16)((c->tb_latch & 0x00FF) | (val << 8));
                   if (!c->tb_running) c->tb_counter = c->tb_latch;
                   break;
        case 0x0D:
            /* ICR write sets/clears the interrupt MASK:
             * bit 7 = 1 -> set the mask bits; bit 7 = 0 -> clear them. */
            if (val & 0x80) c->imr |= (val & 0x7F);
            else            c->imr &= (u8)~(val & 0x7F);
            cia_update_irq(c);
            break;
        case 0x0E: c->cra = val & (u8)~0x10; /* force-load is a strobe */
                   if (val & 0x10) c->ta_counter = c->ta_latch;
                   c->ta_running = (val & 0x01) != 0;
                   break;
        case 0x0F: c->crb = val & (u8)~0x10;
                   if (val & 0x10) c->tb_counter = c->tb_latch;
                   c->tb_running = (val & 0x01) != 0;
                   break;
        default: break;
    }
}

u8 cia_read(Cia *c, u16 addr) {
    switch (addr & 0x0F) {
        case 0x00: return c->pra;
        case 0x01: return c->prb;
        case 0x02: return c->ddra;
        case 0x03: return c->ddrb;
        case 0x04: return (u8)(c->ta_counter & 0xFF);
        case 0x05: return (u8)((c->ta_counter >> 8) & 0xFF);
        case 0x06: return (u8)(c->tb_counter & 0xFF);
        case 0x07: return (u8)(c->tb_counter >> 8);
        case 0x0D: { u8 v = c->icr; c->icr = 0;
                     c->ta_underflow = c->tb_underflow = false; return v; }
        case 0x0E: return c->cra;
        case 0x0F: return c->crb;
        default: return 0xFF;
    }
}

bool cia_irq_line(const Cia *c) {
    return (c->icr & c->imr & 0x7F) != 0;
}

bool cia_tick(Cia *c, int cycles) {
    c->ta_underflow = false;
    c->tb_underflow = false;
    /* CRA bit 5 selects the external CNT line, not Phi2. */
    int ta_underflows = (c->cra & 0x20) ? 0
        : timer_advance(&c->ta_counter, c->ta_latch, &c->ta_running,
                        &c->cra, cycles);
    if (ta_underflows) {
        c->ta_underflow = true;
        c->icr |= CIA_ICR_TA;
    }

    /* CRB bits 6-5: 00 Phi2, 01 CNT, 10 Timer A underflows,
     * 11 Timer A underflows gated by CNT. The CNT modes await line support. */
    int tb_clocks = 0;
    switch (c->crb & 0x60) {
        case 0x00: tb_clocks = cycles; break;
        case 0x40: tb_clocks = ta_underflows; break;
        default: break;
    }
    int tb_underflows = timer_advance(&c->tb_counter, c->tb_latch,
                                      &c->tb_running, &c->crb, tb_clocks);
    if (tb_underflows) {
        c->tb_underflow = true;
        c->icr |= CIA_ICR_TB;
    }
    cia_update_irq(c);
    return cia_irq_line(c);
}
