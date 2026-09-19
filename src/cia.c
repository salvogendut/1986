#include "cia.h"
#include <string.h>

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
}

void cia_write(Cia *c, u16 addr, u8 val) {
    switch (addr & 0x0F) {
        case 0x00: c->pra = val; break;
        case 0x01: c->prb = val; break;
        case 0x02: c->ddra = val; break;
        case 0x03: c->ddrb = val; break;
        case 0x04: c->ta_lo = val; break;
        case 0x05: c->ta_hi = val; break;
        case 0x06: c->tb_lo = val; break;
        case 0x07: c->tb_hi = val; break;
        case 0x0D: c->icr = val; break;
        case 0x0E: c->imr = val; break;
        default: break;
    }
}

u8 cia_read(Cia *c, u16 addr) {
    switch (addr & 0x0F) {
        case 0x00: return c->pra;
        case 0x01: return c->prb;
        case 0x02: return c->ddra;
        case 0x03: return c->ddrb;
        case 0x04: return c->ta_lo;
        case 0x05: return c->ta_hi;
        case 0x06: return c->tb_lo;
        case 0x07: return c->tb_hi;
        case 0x0D: return c->icr;
        case 0x0E: return c->imr;
        default: return 0xFF;
    }
}
