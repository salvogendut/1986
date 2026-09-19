#include "cia.h"
#include <string.h>

/* CIA interrupt control register bits. */
#define CIA_ICR_TA   0x01   /* timer A underflow */
#define CIA_ICR_IRQ  0x80   /* IRQ output line */

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
    c->ta_latch = 0;
    c->ta_counter = 0;
    c->ta_running = false;
    c->ta_underflow = false;
}

void cia_write(Cia *c, u16 addr, u8 val) {
    switch (addr & 0x0F) {
        case 0x00: c->pra = val; break;
        case 0x01: c->prb = val; break;
        case 0x02: c->ddra = val; break;
        case 0x03: c->ddrb = val; break;
        case 0x04: c->ta_lo = val; c->ta_latch = (u16)((c->ta_latch & 0xFF00) | val); break;
        case 0x05: c->ta_hi = val; c->ta_latch = (u16)((c->ta_latch & 0x00FF) | (val << 8)); break;
        case 0x06: c->tb_lo = val; break;
        case 0x07: c->tb_hi = val; break;
        case 0x0D: c->icr = val; break;   /* write: set/clear mask; clear ICR */
        case 0x0E:
            c->imr = val;
            /* Bit 0 of CIA CR: 1 = start timer A, 0 = stop. On (re)load we
             * copy the latch into the counter. */
            if (val & 0x01) {
                c->ta_running = true;
                c->ta_counter = c->ta_latch;
            } else {
                c->ta_running = false;
            }
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
        case 0x06: return c->tb_lo;
        case 0x07: return c->tb_hi;
        case 0x0D: { u8 v = c->icr; c->icr = 0; c->ta_underflow = false; return v; }
        case 0x0E: return c->imr;
        default: return 0xFF;
    }
}

bool cia_tick(Cia *c, int cycles) {
    c->ta_underflow = false;
    if (!c->ta_running) return false;
    /* Count down; on underflow set the ICR timer-A flag and reload. */
    if ((int)c->ta_counter > cycles) {
        c->ta_counter -= (u16)cycles;
        return false;
    }
    c->ta_counter = c->ta_latch;
    /* Timer A underflow. The KERNAL's main loop polls ICR bit 3 (the
     * level-triggered IRQ source), so assert that too. */
    c->icr |= CIA_ICR_TA | CIA_ICR_IRQ | 0x08;
    c->ta_underflow = true;
    return true;
}
