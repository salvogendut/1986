#include "cia.h"
#include <string.h>

#define CIA_ICR_TA    0x01   /* timer A underflow */
#define CIA_ICR_TB    0x02   /* timer B underflow */
#define CIA_ICR_TOD   0x04   /* time-of-day alarm */
#define CIA_ICR_SDR   0x08   /* serial byte complete */
#define CIA_ICR_FLAG  0x10   /* falling edge on FLAG */
#define CIA_ICR_IRQ   0x80   /* IRQ output line */

static void cia_update_irq(Cia *c) {
    if (c->icr & c->imr & 0x7F) c->icr |= CIA_ICR_IRQ;
    else c->icr &= (u8)~CIA_ICR_IRQ;
}

static void cia_check_tod_alarm(Cia *c) {
    if (memcmp(c->tod, c->tod_alarm, sizeof(c->tod)) == 0) {
        c->icr |= CIA_ICR_TOD;
        cia_update_irq(c);
    }
}

/* Increment a two-digit BCD field and report a carry past its maximum. */
static bool bcd_increment(u8 *value, u8 maximum) {
    u8 ones = (u8)((*value & 0x0F) + 1);
    u8 tens = (u8)(*value & 0xF0);
    if (ones == 10) { ones = 0; tens += 0x10; }
    *value = (u8)(tens | ones);
    if (*value <= maximum) return false;
    *value = 0;
    return true;
}

static void cia_advance_tod_tenth(Cia *c) {
    if (bcd_increment(&c->tod[0], 0x09) &&
        bcd_increment(&c->tod[1], 0x59) &&
        bcd_increment(&c->tod[2], 0x59)) {
        u8 pm = c->tod[3] & 0x80;
        u8 hour = c->tod[3] & 0x1F;
        if (hour == 0x11) { hour = 0x12; pm ^= 0x80; }
        else if (hour == 0x12 || hour == 0x00) hour = 0x01;
        else if (hour == 0x09) hour = 0x10;
        else hour++;
        c->tod[3] = (u8)(pm | hour);
    }
    cia_check_tod_alarm(c);
}

/* Output mode uses Timer A underflows as CNT half-clocks. The first
 * underflow presents bit 7; each rising CNT edge transmits a bit. */
static void cia_serial_output_underflow(Cia *c) {
    if (!c->serial_active) {
        if (!c->serial_pending) return;
        c->serial_shift = c->sdr;
        c->serial_bits = 8;
        c->serial_pending = false;
        c->serial_active = true;
        c->cnt_output_high = false;
        c->sp_output_high = (c->serial_shift & 0x80) != 0;
        return;
    }
    c->cnt_output_high = !c->cnt_output_high;
    if (!c->cnt_output_high) {
        c->sp_output_high = (c->serial_shift & 0x80) != 0;
        return;
    }
    c->serial_bits--;
    c->serial_shift <<= 1;
    if (c->serial_bits == 0) {
        c->serial_active = false;
        c->icr |= CIA_ICR_SDR;
    }
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
    memset(c->tod, 0, sizeof(c->tod));
    memset(c->tod_alarm, 0, sizeof(c->tod_alarm));
    memset(c->tod_latch, 0, sizeof(c->tod_latch));
    c->tod[3] = 0x01; /* MOS 6526 resets at 1:00:00.0, stopped */
    c->tod_pulses = 0;
    c->tod_stopped = true;
    c->tod_latched = false;
    c->sdr = c->serial_shift = c->serial_bits = 0;
    c->serial_pending = c->serial_active = false;
    c->sp_input_high = c->cnt_input_high = c->flag_input_high = true;
    c->sp_output_high = c->cnt_output_high = true;
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
        case 0x08: case 0x09: case 0x0A: case 0x0B: {
            unsigned index = addr & 0x03;
            if (index == 0) val &= 0x0F;
            else if (index == 3) {
                val &= 0x9F;
                /* On a 6526, setting clock hour 12 flips the AM/PM latch;
                 * alarm writes do not change it (matching VICE ciacore). */
                if (!(c->crb & 0x80) && (val & 0x1F) == 0x12)
                    val ^= 0x80;
            }
            else val &= 0x7F;
            if (c->crb & 0x80) {
                c->tod_alarm[index] = val;
            } else {
                if (index == 3) c->tod_stopped = true;
                c->tod[index] = val;
                if (index == 0) {
                    c->tod_pulses = 0;
                    c->tod_stopped = false;
                }
            }
            cia_check_tod_alarm(c);
            break;
        }
        case 0x0C:
            c->sdr = val;
            if (c->cra & 0x40) c->serial_pending = true;
            break;
        case 0x0D:
            /* ICR write sets/clears the interrupt MASK:
             * bit 7 = 1 -> set the mask bits; bit 7 = 0 -> clear them. */
            if (val & 0x80) c->imr |= (val & 0x7F);
            else            c->imr &= (u8)~(val & 0x7F);
            cia_update_irq(c);
            break;
        case 0x0E: {
            bool mode_changed = ((c->cra ^ val) & 0x40) != 0;
            c->cra = val & (u8)~0x10; /* force-load is a strobe */
            if (val & 0x10) c->ta_counter = c->ta_latch;
            c->ta_running = (val & 0x01) != 0;
            if (mode_changed) {
                c->serial_active = c->serial_pending = false;
                c->serial_bits = c->serial_shift = 0;
                c->sp_output_high = c->cnt_output_high = true;
            }
            break;
        }
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
        case 0x08: case 0x09: case 0x0A: case 0x0B: {
            unsigned index = addr & 0x03;
            if (index == 3 && !c->tod_latched) {
                memcpy(c->tod_latch, c->tod, sizeof(c->tod_latch));
                c->tod_latched = true;
            }
            u8 value = c->tod_latched ? c->tod_latch[index] : c->tod[index];
            if (index == 0) c->tod_latched = false;
            return value;
        }
        case 0x0C: return c->sdr;
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
        if (c->cra & 0x40)
            for (int i = 0; i < ta_underflows; i++) cia_serial_output_underflow(c);
    }

    /* CRB bits 6-5: 00 Phi2, 01 CNT, 10 Timer A underflows,
     * 11 Timer A underflows gated by the external CNT level. */
    int tb_clocks = 0;
    switch (c->crb & 0x60) {
        case 0x00: tb_clocks = cycles; break;
        case 0x40: tb_clocks = ta_underflows; break;
        case 0x60: if (c->cnt_input_high) tb_clocks = ta_underflows; break;
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

void cia_tod_tick(Cia *c) {
    if (c->tod_stopped) return;
    c->tod_pulses++;
    if (c->tod_pulses >= ((c->cra & 0x80) ? 5 : 6)) {
        c->tod_pulses = 0;
        cia_advance_tod_tenth(c);
    }
}

void cia_set_sp(Cia *c, bool high) {
    c->sp_input_high = high;
}

void cia_set_cnt(Cia *c, bool high) {
    bool rising = !c->cnt_input_high && high;
    c->cnt_input_high = high;
    if (!rising) return;

    int ta_underflows = (c->cra & 0x20)
        ? timer_advance(&c->ta_counter, c->ta_latch, &c->ta_running,
                        &c->cra, 1) : 0;
    if (ta_underflows) {
        c->ta_underflow = true;
        c->icr |= CIA_ICR_TA;
    }
    int tb_clocks = 0;
    switch (c->crb & 0x60) {
        case 0x20: tb_clocks = 1; break;
        case 0x40: tb_clocks = ta_underflows; break;
        case 0x60: tb_clocks = ta_underflows; break;
        default: break;
    }
    if (timer_advance(&c->tb_counter, c->tb_latch, &c->tb_running,
                      &c->crb, tb_clocks)) {
        c->tb_underflow = true;
        c->icr |= CIA_ICR_TB;
    }

    if (!(c->cra & 0x40)) {
        c->serial_shift = (u8)((c->serial_shift << 1) |
                               (c->sp_input_high ? 1 : 0));
        if (++c->serial_bits == 8) {
            c->sdr = c->serial_shift;
            c->serial_bits = 0;
            c->serial_shift = 0;
            c->icr |= CIA_ICR_SDR;
        }
    }
    cia_update_irq(c);
}

void cia_set_flag(Cia *c, bool high) {
    if (c->flag_input_high && !high) {
        c->icr |= CIA_ICR_FLAG;
        cia_update_irq(c);
    }
    c->flag_input_high = high;
}
