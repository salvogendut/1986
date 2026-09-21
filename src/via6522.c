#include "via6522.h"
#include <string.h>

enum {
    VIA_CB2 = 0x08, VIA_CB1 = 0x10, VIA_T2 = 0x20, VIA_T1 = 0x40,
    VIA_CA2 = 0x01, VIA_CA1 = 0x02
};

void via6522_init(Via6522 *v) {
    memset(v, 0, sizeof(*v));
    v->input_a = v->input_b = 0xff;
    v->t1_latch = v->t2_latch = 0xffff;
    v->t1_count = v->t2_count = 0x10000;
    v->ca2_out = v->cb2_out = true;
}

void via6522_reset(Via6522 *v) {
    Via6522PortChange hook = v->port_change;
    void *ctx = v->port_ctx;
    via6522_init(v);
    v->port_change = hook;
    v->port_ctx = ctx;
    if (hook) {
        hook(ctx, 0, via6522_output_a(v));
        hook(ctx, 1, via6522_output_b(v));
    }
}

void via6522_set_port_hook(Via6522 *v, Via6522PortChange hook, void *ctx) {
    v->port_change = hook;
    v->port_ctx = ctx;
    if (hook) {
        hook(ctx, 0, via6522_output_a(v));
        hook(ctx, 1, via6522_output_b(v));
    }
}

u8 via6522_output_a(const Via6522 *v) {
    return (u8)(v->ora | ~v->ddra);
}

u8 via6522_output_b(const Via6522 *v) {
    u8 result = (u8)(v->orb | ~v->ddrb);
    if (v->acr & 0x80) result = (u8)((result & 0x7f) | v->pb7);
    return result;
}

static void port_change(Via6522 *v, unsigned port) {
    if (v->port_change)
        v->port_change(v->port_ctx, port,
                       port ? via6522_output_b(v) : via6522_output_a(v));
}

void via6522_set_input_a(Via6522 *v, u8 pins) { v->input_a = pins; }

void via6522_set_input_b(Via6522 *v, u8 pins) {
    bool falling_pb6 = (v->input_b & 0x40) && !(pins & 0x40);
    v->input_b = pins;
    if (falling_pb6 && (v->acr & 0x20) && v->t2_running) {
        if (v->t2_count > 1) v->t2_count--;
        else {
            v->t2_count = 0x10000;
            v->t2_running = false;
            v->ifr |= VIA_T2;
        }
    }
}

static void edge(Via6522 *v, bool *stored, bool level, u8 flag,
                 bool active_rising) {
    if (*stored != level && level == active_rising) v->ifr |= flag;
    *stored = level;
}

void via6522_set_ca1(Via6522 *v, bool level) {
    edge(v, &v->ca1, level, VIA_CA1, (v->pcr & 1) != 0);
}

void via6522_set_ca2(Via6522 *v, bool level) {
    if (!(v->pcr & 0x08))
        edge(v, &v->ca2, level, VIA_CA2, (v->pcr & 0x04) != 0);
    else v->ca2 = level;
}

void via6522_set_cb1(Via6522 *v, bool level) {
    edge(v, &v->cb1, level, VIA_CB1, (v->pcr & 0x10) != 0);
}

void via6522_set_cb2(Via6522 *v, bool level) {
    if (!(v->pcr & 0x80))
        edge(v, &v->cb2, level, VIA_CB2, (v->pcr & 0x40) != 0);
    else v->cb2 = level;
}

bool via6522_irq(const Via6522 *v) {
    return (v->ifr & v->ier & 0x7f) != 0;
}

static void clear_port_a_irq(Via6522 *v) {
    v->ifr &= (u8)~VIA_CA1;
    if ((v->pcr & 0x0a) != 0x02) v->ifr &= (u8)~VIA_CA2;
}

static void clear_port_b_irq(Via6522 *v) {
    v->ifr &= (u8)~VIA_CB1;
    if ((v->pcr & 0xa0) != 0x20) v->ifr &= (u8)~VIA_CB2;
}

u8 via6522_read(Via6522 *v, u16 reg) {
    switch (reg & 15) {
        case 0:
            clear_port_b_irq(v);
            return (u8)(((v->input_b & ~v->ddrb) | (v->orb & v->ddrb)) &
                        ((v->acr & 0x80) ? 0x7f : 0xff)) |
                   ((v->acr & 0x80) ? v->pb7 : 0);
        case 1:
            clear_port_a_irq(v);
            return (u8)((v->input_a & ~v->ddra) | (v->ora & v->ddra));
        case 2: return v->ddrb;
        case 3: return v->ddra;
        case 4:
            v->ifr &= (u8)~VIA_T1;
            return (u8)(v->t1_count - 1);
        case 5: return (u8)((v->t1_count - 1) >> 8);
        case 6: return (u8)v->t1_latch;
        case 7: return (u8)(v->t1_latch >> 8);
        case 8:
            v->ifr &= (u8)~VIA_T2;
            return (u8)(v->t2_count - 1);
        case 9: return (u8)((v->t2_count - 1) >> 8);
        case 10:
            v->ifr &= (u8)~0x04;
            return v->sr;
        case 11: return v->acr;
        case 12: return v->pcr;
        case 13: return (u8)(v->ifr | (via6522_irq(v) ? 0x80 : 0));
        case 14: return (u8)(v->ier | 0x80);
        case 15: return (u8)((v->input_a & ~v->ddra) | (v->ora & v->ddra));
    }
    return 0xff;
}

void via6522_write(Via6522 *v, u16 reg, u8 value) {
    switch (reg & 15) {
        case 0:
            clear_port_b_irq(v);
            v->orb = value;
            port_change(v, 1);
            break;
        case 1:
            clear_port_a_irq(v);
            /* fall through */
        case 15:
            v->ora = value;
            port_change(v, 0);
            break;
        case 2:
            v->ddrb = value;
            port_change(v, 1);
            break;
        case 3:
            v->ddra = value;
            port_change(v, 0);
            break;
        case 4: case 6:
            v->t1_latch = (u16)((v->t1_latch & 0xff00) | value);
            break;
        case 5:
            v->t1_latch = (u16)((v->t1_latch & 0x00ff) | ((u16)value << 8));
            v->t1_count = (u32)v->t1_latch + 1;
            v->t1_running = true;
            v->ifr &= (u8)~VIA_T1;
            v->pb7 = 0;
            if (v->acr & 0x80) port_change(v, 1);
            break;
        case 7:
            v->t1_latch = (u16)((v->t1_latch & 0x00ff) | ((u16)value << 8));
            v->ifr &= (u8)~VIA_T1;
            break;
        case 8:
            v->t2_latch = (u16)((v->t2_latch & 0xff00) | value);
            break;
        case 9:
            v->t2_latch = (u16)((v->t2_latch & 0x00ff) | ((u16)value << 8));
            v->t2_count = (u32)v->t2_latch + 1;
            v->t2_running = true;
            v->ifr &= (u8)~VIA_T2;
            break;
        case 10:
            v->sr = value;
            v->ifr &= (u8)~0x04;
            break;
        case 11:
            if ((value & 0x80) && !(v->acr & 0x80)) v->pb7 = 0x80;
            v->acr = value;
            port_change(v, 1);
            break;
        case 12:
            v->pcr = value;
            v->ca2_out = (value & 0x0e) != 0x0c;
            v->cb2_out = (value & 0xe0) != 0xc0;
            break;
        case 13:
            v->ifr &= (u8)~(value & 0x7f);
            break;
        case 14:
            if (value & 0x80) v->ier |= value & 0x7f;
            else v->ier &= (u8)~(value & 0x7f);
            break;
    }
}

void via6522_tick(Via6522 *v, unsigned cycles) {
    if (v->t1_running && cycles) {
        if (cycles < v->t1_count) v->t1_count -= cycles;
        else {
            unsigned after = cycles - v->t1_count;
            v->ifr |= VIA_T1;
            if (v->acr & 0x40) {
                u32 period = (u32)v->t1_latch + 1;
                unsigned underflows = 1 + after / period;
                v->t1_count = period - after % period;
                if ((v->acr & 0x80) && (underflows & 1)) {
                    v->pb7 ^= 0x80;
                    port_change(v, 1);
                }
            } else {
                v->t1_running = false;
                v->t1_count = 0x10000;
                if (v->acr & 0x80) {
                    v->pb7 = 0x80;
                    port_change(v, 1);
                }
            }
        }
    }
    if (v->t2_running && !(v->acr & 0x20) && cycles) {
        if (cycles < v->t2_count) v->t2_count -= cycles;
        else {
            v->t2_count = 0x10000;
            v->t2_running = false;
            v->ifr |= VIA_T2;
        }
    }
}
