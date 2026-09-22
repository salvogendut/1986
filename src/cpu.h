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

/* PAL frame length in 8502 cycles (312 raster lines x 63 cycles @ 1 MHz). */
#define CPU_PAL_FRAME_CYCLES 19656

typedef struct {
    u8  (*read) (void *ctx, u16 addr);
    void (*write)(void *ctx, u16 addr, u8 val);
    void *ctx;
} CpuBus;

typedef struct {
    /* Register mirror, synced from the VICE core after each step. */
    u8   a;
    u8   x;
    u8   y;
    u8   sp;
    u8   p;
    u16  pc;
    u64  cycles;   /* total cycles executed */
    bool irq_level;   /* asserted IRQ line */
    bool nmi_level;   /* asserted NMI line */
    bool fast;        /* 8502 at 2 MHz (C128 fast mode) */

    /* 8502 on-chip I/O port ($00 DDR / $01 port), driving the MMU. */
    u8   io_ddr;
    u8   io_port;

    CpuBus bus;
} Cpu8502;

void cpu_init(Cpu8502 *cpu, CpuBus bus);
void cpu_attach_mem(Cpu8502 *cpu, u8 *ram); /* set the RAM base used for the stack page */
void cpu_set_stack_page(u8 *page); /* follow $D509/A stack-page relocation */
void cpu_reset(Cpu8502 *cpu);
int  cpu_step(Cpu8502 *cpu);          /* run one frame of cycles; returns cycles */
int  cpu_step_budget(Cpu8502 *cpu, int budget); /* run up to budget cycles */
void cpu_irq(Cpu8502 *cpu, bool level);
void cpu_nmi(Cpu8502 *cpu, bool level);
void cpu_pc(Cpu8502 *cpu, u16 pc);
u64  cpu_cycles(void);                /* total cycles executed (for raster sync) */
bool cpu_rmw_active(void);            /* current instruction has an RMW bus write */
void cpu_install_serial_traps(u8 *kernal); /* patch the KERNAL ROM with IEC traps */

/* IEC (serial-bus) trap callbacks, invoked by the KERNAL's patched routines.
 * The C128 core installs a handler that forwards to the pluggable drive. */
typedef struct {
    void *ctx;
    bool force_slow_serial;                 /* command-level device has no CIA SDR */
    void (*attention)(void *ctx, u8 b);    /* LISTEN/TALK/secondary on the bus */
    void (*send)(void *ctx, u8 byte);      /* C128 sends a command byte */
    int  (*receive)(void *ctx, u8 *byte);  /* -1 = serial error, 0 = no device,
                                            * 1 = data, 2 = final (EOI) byte */
    u8   (*take_status)(void *ctx);        /* consume pending IEC status bits */
} IecCallbacks;

/* Patch the KERNAL's IEC routines and install the callbacks. Pass cb=NULL to
 * just patch the serial-ready routines (boot). */
void cpu_install_iec_traps(u8 *kernal, const IecCallbacks *cb);
