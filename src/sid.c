#include "sid.h"
#include <math.h>
#include <string.h>

/* Envelope rate-period table adapted from VICE/reSID's envelope.cc.
 * Copyright (C) 2010 Dag Lem <resid@nimrod.no>.
 * That work is licensed under the GNU General Public License, version 2 or
 * (at your option) any later version; see the repository's LICENSE.
 * Filter and combined-waveform rendering below are deliberately simpler
 * than reSID's analog model. */
static const u16 rate_period[16] = {
    8, 31, 62, 94, 148, 219, 266, 312,
    391, 976, 1953, 3125, 3906, 11719, 19531, 31250
};

enum { ENV_ATTACK, ENV_DECAY, ENV_RELEASE };

static u8 decay_divider(u8 env) {
    if (env >= 93) return 1;
    if (env >= 54) return 2;
    if (env >= 26) return 4;
    if (env >= 14) return 8;
    if (env >= 6) return 16;
    return 30;
}

static void envelope_clock(Sid *s, int n) {
    SidVoice *v = &s->voice[n];
    int base = n * 7;
    u8 ad = s->regs[base + 5];
    u8 sr = s->regs[base + 6];
    u8 rate = v->env_state == ENV_ATTACK ? ad >> 4
        : v->env_state == ENV_DECAY ? ad & 15 : sr & 15;
    /* Keep the rate counter across gate/rate changes, as on the SID. */
    v->rate_counter = (u16)((v->rate_counter + 1) & 0x7FFF);
    if (v->rate_counter < rate_period[rate]) return;
    v->rate_counter = 0;
    if (v->env_state == ENV_ATTACK) {
        if (v->env < 255) ++v->env;
        if (v->env == 255) { v->env_state = ENV_DECAY; v->exp_counter = 0; }
        return;
    }
    if (++v->exp_counter < decay_divider(v->env)) return;
    v->exp_counter = 0;
    if (v->env_state == ENV_DECAY) {
        u8 sustain = (u8)((sr >> 4) * 17);
        if (v->env > sustain) --v->env;
    } else if (v->env > 0) {
        --v->env;
    }
}

static void noise_clock(SidVoice *v) {
    /* 23-bit SID shift register; taps 22 and 17. */
    u32 feedback = ((v->noise >> 22) ^ (v->noise >> 17)) & 1;
    v->noise = ((v->noise << 1) | feedback) & 0x7FFFFF;
}

static int waveform(const Sid *s, int n) {
    const SidVoice *v = &s->voice[n];
    u8 control = s->regs[n * 7 + 4];
    u8 selected = control & 0xF0;
    if (!selected || (control & 0x08)) return 0;
    u32 phase = v->phase;
    int result = 0xFFF;
    if (selected & 0x10) {
        u32 source = s->voice[(n + 2) % 3].phase;
        if ((control & 0x04) && (source & 0x800000)) phase ^= 0x800000;
        int tri = (int)((phase >> 11) & 0xFFF);
        result &= (phase & 0x800000) ? (tri ^ 0xFFF) : tri;
    }
    if (selected & 0x20) result &= (int)(v->phase >> 12);
    if (selected & 0x40) {
        int pw = s->regs[n * 7 + 2] | ((s->regs[n * 7 + 3] & 15) << 8);
        result &= (int)(v->phase >> 12) < pw ? 0xFFF : 0;
    }
    if (selected & 0x80) {
        u32 noise = v->noise;
        int bits = (((noise >> 20) & 1) << 7) | (((noise >> 18) & 1) << 6)
                 | (((noise >> 14) & 1) << 5) | (((noise >> 11) & 1) << 4)
                 | (((noise >> 9) & 1) << 3) | (((noise >> 5) & 1) << 2)
                 | (((noise >> 2) & 1) << 1) | (noise & 1);
        result &= bits << 4;
    }
    return result;
}

static s16 mix_sample(Sid *s) {
    double direct = 0.0, filtered = 0.0;
    for (int n = 0; n < 3; ++n) {
        int wave = waveform(s, n);
        u8 control = s->regs[n * 7 + 4];
        double voice = (control & 0xF0) && !(control & 0x08)
            ? (wave - 2048.0) / 2048.0 * (s->voice[n].env / 255.0) : 0.0;
        if (s->regs[0x17] & (1 << n)) filtered += voice;
        else if (n != 2 || !(s->regs[0x18] & 0x80)) direct += voice;
    }

    /* Stable two-integrator state-variable filter. This approximates the
     * 8580's low/band/high-pass routing, not its nonlinear analog response. */
    double g = s->filter_g;
    double k = s->filter_k;
    double a1 = 1.0 / (1.0 + g * (g + k));
    double v3 = filtered - s->filter_ic2;
    double band = a1 * (s->filter_ic1 + g * v3);
    double low = s->filter_ic2 + g * band;
    s->filter_ic1 = 2.0 * band - s->filter_ic1;
    s->filter_ic2 = 2.0 * low - s->filter_ic2;
    double high = filtered - k * band - low;
    u8 mode = s->regs[0x18];
    if (mode & 0x10) direct += low;
    if (mode & 0x20) direct += band;
    if (mode & 0x40) direct += high;
    double sample = direct * ((mode & 15) / 15.0) * 9000.0;
    if (sample > 32767.0) sample = 32767.0;
    if (sample < -32768.0) sample = -32768.0;
    return (s16)sample;
}

static void update_filter(Sid *s) {
    int cutoff = ((int)s->regs[0x16] << 3) | (s->regs[0x15] & 7);
    double hz = 30.0 + cutoff * (12000.0 / 2047.0);
    s->filter_g = tan(3.14159265358979323846 * hz / SID_SAMPLE_RATE);
    s->filter_k = 1.4 - ((s->regs[0x17] >> 4) & 15) * (1.1 / 15.0);
}

void sid_init(Sid *s) {
    memset(s, 0, sizeof(*s));
    sid_reset(s);
}

void sid_reset(Sid *s) {
    memset(s, 0, sizeof(*s));
    for (int n = 0; n < 3; ++n) {
        s->voice[n].noise = 0x7FFFF8;
        s->voice[n].env_state = ENV_RELEASE;
    }
    update_filter(s);
}

void sid_write(Sid *s, u16 addr, u8 val) {
    u16 i = (u16)(addr & 0x1F); /* SID registers mirror through $D4FF */
    if (i < 21 && i % 7 == 4) {
        SidVoice *v = &s->voice[i / 7];
        bool old_gate = (s->regs[i] & 1) != 0;
        bool new_gate = (val & 1) != 0;
        if (new_gate && !old_gate) { v->env_state = ENV_ATTACK; v->exp_counter = 0; }
        if (!new_gate && old_gate) { v->env_state = ENV_RELEASE; v->exp_counter = 0; }
        if (val & 0x08) v->phase = 0;
    }
    s->regs[i] = val;
    if (i >= 0x15 && i <= 0x17) update_filter(s);
}

u8 sid_read(Sid *s, u16 addr) {
    u16 i = (u16)(addr & 0x1F);
    if (i == 0x1B) return s->osc3;
    if (i == 0x1C) return s->env3;
    if (i == 0x19 || i == 0x1A) return 0xFF; /* no paddles attached */
    /* Open-bus decay is not yet modeled; retain stored-register readback. */
    return s->regs[i];
}

int sid_clock(Sid *s, int cycles, s16 *out, int capacity) {
    int written = 0;
    for (int clock = 0; clock < cycles; ++clock) {
        bool msb_rise[3];
        for (int n = 0; n < 3; ++n) {
            SidVoice *v = &s->voice[n];
            u8 control = s->regs[n * 7 + 4];
            u32 old = v->phase;
            if (control & 0x08) {
                v->phase = 0;
                msb_rise[n] = false;
            } else {
                u32 freq = s->regs[n * 7] | ((u32)s->regs[n * 7 + 1] << 8);
                v->phase = (old + freq) & 0xFFFFFF;
                msb_rise[n] = !(old & 0x800000) && (v->phase & 0x800000);
                if (!(old & 0x80000) && (v->phase & 0x80000)) noise_clock(v);
            }
            envelope_clock(s, n);
        }
        for (int n = 0; n < 3; ++n)
            if ((s->regs[n * 7 + 4] & 0x02) && msb_rise[(n + 2) % 3])
                s->voice[n].phase = 0;
        s->osc3 = (u8)(waveform(s, 2) >> 4);
        s->env3 = s->voice[2].env;
        s->sample_accum += SID_SAMPLE_RATE;
        if (s->sample_accum >= SID_CLOCK_HZ) {
            s->sample_accum -= SID_CLOCK_HZ;
            s16 sample = mix_sample(s);
            if (written < capacity && out) out[written++] = sample;
        }
    }
    return written;
}
