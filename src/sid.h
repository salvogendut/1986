#pragma once
#include "types.h"

/*
 * MOS 6581/8580 SID (Sound Interface Device).
 *
 * Three voices, each with a 16-bit frequency and a 16-bit pulse-width,
 * feeding a filter and volume control. The scaffold carries the register
 * file; the oscillator/filter audio render is a TODO.
 */
typedef struct {
    u8  regs[32];    /* voice/filter/volume registers at $D400-$D41F */
} Sid;

void sid_init(Sid *s);
void sid_reset(Sid *s);
void sid_write(Sid *s, u16 addr, u8 val);
u8   sid_read(Sid *s, u16 addr);
