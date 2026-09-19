#include "cia.h"
#include <string.h>

#define CIA_ICR_TA    0x01   /* timer A underflow */
#define CIA_ICR_IRQ   0x80   /* IRQ output line */

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
        case 0x0D:
            /* ICR write sets/clears the interrupt MASK:
             * bit 7 = 1 -> set the mask bits; bit 7 = 0 -> clear them. */
            if (val & 0x80) c->imr |= (val & 0x7F);
            else            c->imr &= (u8)~(val & 0x7F);
            break;
        case 0x0E: c->cra = val;
                   if (val & 0x01) { c->ta_running = true; c->ta_counter = c->ta_latch; }
                   else            { c->ta_running = false; }
                   break;
        case 0x0F: c->crb = val; break;
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
    if (!c->ta_running) return false;
    if ((int)c->ta_counter > cycles) {
        c->ta_counter -= (u16)cycles;
        return false;
    }
    /* Timer A underflow: set the flag, reload from the latch. */
    c->ta_counter = c->ta_latch;
    c->icr |= CIA_ICR_TA;
    if (c->icr & c->imr & 0x7F) c->icr |= CIA_ICR_IRQ;
    c->ta_underflow = true;
    return cia_irq_line(c);
}
