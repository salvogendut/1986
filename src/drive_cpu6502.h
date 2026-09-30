#pragma once
#include "types.h"
#include <stdbool.h>

typedef u8 (*DriveCpu6502Read)(void *ctx, u16 addr);
typedef void (*DriveCpu6502Write)(void *ctx, u16 addr, u8 value);

typedef struct {
    u8 a, x, y, sp, p;
    u16 pc;
    u64 cycles;
    bool irq, nmi_pending, jammed;
    DriveCpu6502Read read;
    DriveCpu6502Write write;
    void *ctx;
} DriveCpu6502;

/* Each instance has its own bus; never uses the host 8502's singleton core.
 * The owner must not move/copy a live board without rebinding ctx on reset. */
void drive_cpu6502_reset(DriveCpu6502 *cpu, DriveCpu6502Read read,
                         DriveCpu6502Write write, void *ctx);
/* Returns instruction cycles, or zero for an unsupported/jammed opcode. */
int drive_cpu6502_step(DriveCpu6502 *cpu);
