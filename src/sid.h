#pragma once
#include "types.h"

/* PAL C128DCR SID clock and mono output rate. Audio runs at 1 MHz even when
 * the 8502 uses its 2 MHz fast mode. */
#define SID_CLOCK_HZ    982800
#define SID_SAMPLE_RATE 44100

typedef struct {
    u32 phase;            /* 24-bit oscillator accumulator */
    u32 noise;            /* 23-bit noise shift register */
    u16 rate_counter;     /* 15-bit envelope rate counter */
    u8 env;               /* 8-bit envelope level */
    u8 env_state;
    u8 exp_counter;
} SidVoice;

typedef struct {
    u8 regs[32];          /* $D400-$D41F, mirrored through $D4FF */
    SidVoice voice[3];
    u32 sample_accum;
    u8 osc3, env3;        /* readable voice-three oscillator/envelope */
    double filter_g, filter_k;
    double filter_ic1, filter_ic2;
} Sid;

void sid_init(Sid *s);
void sid_reset(Sid *s);
void sid_write(Sid *s, u16 addr, u8 val);
u8 sid_read(Sid *s, u16 addr);
/* Advance SID clocks and write up to capacity signed 16-bit mono samples.
 * The oscillator/envelope state advances even when out is NULL. */
int sid_clock(Sid *s, int cycles, s16 *out, int capacity);
