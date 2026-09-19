#pragma once
#include "types.h"
#include <stdbool.h>

/*
 * MOS 8502 / 6502 CPU core for the Commodore C128.
 *
 * This is a compact, binary-mode interpreter covering the standard opcode
 * set (no undocumented opcodes, decimal mode is stubbed). It is the intended
 * home for the 6502-like machinery that the reference implementation takes
 * from VICE (see DEVELOPMENT.md); the bus interface below is the single seam
 * the rest of the machine (MMU, memory, I/O) plugs into.
 */

/* Processor status flag bits */
#define P_C  0x01   /* carry */
#define P_Z  0x02   /* zero */
#define P_I  0x04   /* interrupt disable */
#define P_D  0x08   /* decimal */
#define P_B  0x10   /* break */
#define P_V  0x40   /* overflow */
#define P_N  0x80   /* negative */

typedef struct {
    u8  (*read) (void *ctx, u16 addr);
    void (*write)(void *ctx, u16 addr, u8 val);
    void *ctx;
} CpuBus;

typedef struct {
    u8   a;        /* accumulator */
    u8   x;        /* index X */
    u8   y;        /* index Y */
    u8   sp;       /* stack pointer (low byte; stack page $0100) */
    u8   p;        /* status flags */
    u16  pc;       /* program counter */
    u64  cycles;   /* total cycles executed */
    bool irq_level;   /* asserted IRQ line */
    bool nmi_level;   /* asserted NMI line */
    bool nmi_pending; /* edge-triggered NMI latch */

    /* 8502 on-chip I/O port ($00 DDR / $01 port). In the C128 this drives
     * the MMU memory configuration. */
    u8   io_ddr;
    u8   io_port;

    CpuBus bus;
} Cpu8502;

void cpu_init(Cpu8502 *cpu, CpuBus bus);
void cpu_reset(Cpu8502 *cpu);
int  cpu_step(Cpu8502 *cpu);          /* execute one instruction; returns cycles */
void cpu_irq(Cpu8502 *cpu, bool level);
void cpu_nmi(Cpu8502 *cpu, bool level);
void cpu_pc(Cpu8502 *cpu, u16 pc);
