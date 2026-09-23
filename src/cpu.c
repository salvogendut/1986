#include "cpu.h"
#include "vice/vice.h"
#include "vice/types.h"
#include "vice/interrupt.h"
#include "vice/alarm.h"
#include "vice/clkguard.h"
#include "vice/debug.h"
#include "vice/log.h"
#include "vice/machine.h"
#include "vice/maincpu.h"
#include "vice/mem.h"
#include "vice/monitor.h"
#include "vice/traps.h"
#include "vice/mos6510.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* VICE base globals that maincpu.c does NOT define (it defines the CPU
 * clock/opcode registers and interrupt status). */

int maincpu_stretch = 0;
struct vice_debug_t debug;

/* Monitor interrupt-mask (array used by the core's DO_INTERRUPT). */
int monitor_mask[1];
/* ------------------------------------------------------------------------- */
/* Memory dispatch. The 6510 core reads/writes through these tables; we point
 * every page at the machine's bus so banking/IO stays in src/c128.c. */

static read_func_ptr_t g_read_tab[0x101];
static store_func_ptr_t g_write_tab[0x101];
read_func_ptr_t *_mem_read_tab_ptr = g_read_tab;
store_func_ptr_t *_mem_write_tab_ptr = g_write_tab;
BYTE *mem_ram = NULL;
BYTE *mem_page_one = NULL;

/* The active CpuBus (its ctx is the C128). Set by cpu_init(). */
static CpuBus g_bus;
static bool g_cpu_jammed;
static u16 g_cpu_jam_pc;

static BYTE cpu_mem_read(WORD addr) {
    return g_bus.read(g_bus.ctx, addr);
}
static void cpu_mem_write(WORD addr, BYTE value) {
    g_bus.write(g_bus.ctx, addr, value);
}

void mem_mmu_translate(unsigned int addr, BYTE **base, int *start, int *limit) {
    /* Disable the core's bank fast-path: all accesses go through the bus. */
    (void)addr; (void)base; (void)start; (void)limit;
}

int mem_rom_trap_allowed(unsigned int addr) {
    (void)addr;
    return 1;
}

void mem_powerup(void) {
    /* Cold power-up RAM/ROM defaults are handled by mem_reset(); nothing to do. */
}

/* ------------------------------------------------------------------------- */
/* Machine / monitor / traps stubs (called from the CPU core glue).          */

int machine_jam(const char *fmt, ...) {
    (void)fmt;
    g_cpu_jammed = true;
    g_cpu_jam_pc = (u16)reg_pc;
    /* Any value outside VICE's reset/monitor choices takes the bounded
     * fallback path: advance one cycle and let cpu_step_budget() return.
     * Returning JAM_RESET resets maincpu_clk to 6 inside the core; when the
     * current budget was based on a much later snapshot clock that can loop
     * forever before control reaches SDL again. */
    return -1;
}
void machine_trigger_reset(int mode) {
    (void)mode;
    interrupt_trigger_reset(maincpu_int_status, maincpu_clk);
}
void machine_reset(void) {
    interrupt_trigger_reset(maincpu_int_status, maincpu_clk);
}
void machine_autostart(void) {
}

/* --- Serial (IEC) ROM traps. VICE intercepts the KERNAL's serial-bus
 * routines so the boot doesn't wait for real hardware; we do the same by
 * patching the KERNAL ROM with TRAP_OPCODE (0x02) at the trap addresses, and
 * forward LISTEN/TALK/send/receive to the pluggable drive. --- */

typedef enum { TRAP_READY = 0, TRAP_ATTENTION, TRAP_SEND, TRAP_RECEIVE } TrapKind;

typedef struct {
    WORD addr;     /* address patched with TRAP_OPCODE */
    WORD resume;   /* address to resume at after the trap */
    int  kind;
} C128Trap;

static const C128Trap g_serial_traps[] = {
    { 0xE569, 0xE572, TRAP_READY },      /* Serial ready */
    { 0xE4F5, 0xE572, TRAP_READY },      /* Serial ready */
    { 0xE5BC, 0xE5C3, TRAP_READY },      /* Serial ready poll */
    { 0xE355, 0xE5BA, TRAP_ATTENTION },  /* SerialListen */
    { 0xE37C, 0xE5BA, TRAP_ATTENTION },  /* SerialSaListen */
    { 0xE38C, 0xE5BA, TRAP_SEND },       /* SerialSendByte */
    { 0xE43E, 0xE5BA, TRAP_RECEIVE },    /* SerialReceiveByte */
};
#define N_SERIAL_TRAPS (sizeof(g_serial_traps) / sizeof(g_serial_traps[0]))

/* VICE x128 uses the C64 KERNAL trap addresses while the same machine is in
 * compatibility mode. These drive the same IEC devices and callbacks. */
static const C128Trap g_c64_serial_traps[] = {
    { 0xEEA9, 0xEDAB, TRAP_READY },
    { 0xED24, 0xEDAB, TRAP_ATTENTION },
    { 0xED37, 0xEDAB, TRAP_ATTENTION },
    { 0xED41, 0xEDAB, TRAP_SEND },
    { 0xEE14, 0xEDAB, TRAP_RECEIVE },
};
#define N_C64_SERIAL_TRAPS \
    (sizeof(g_c64_serial_traps) / sizeof(g_c64_serial_traps[0]))

static IecCallbacks g_iec;
static TapeCallbacks g_tape;
static bool g_tape_active;
static u8 *g_tape_kernal;
static u8 g_tape_original[2][3];
static u8 *g_c64_tape_kernal;
static u8 g_c64_tape_original[2][3];
static const C128Trap g_tape_traps[] = {
    { 0xE8D3, 0xE8D6, 0 }, /* Find next T64 header. */
    { 0xEA60, 0xEE57, 1 }, /* Receive file bytes. */
};
static const C128Trap g_c64_tape_traps[] = {
    { 0xF72F, 0xF732, 0 }, /* C64 KERNAL: find next T64 header. */
    { 0xF8A1, 0xFC93, 1 }, /* C64 KERNAL: receive file bytes. */
};

static void apply_iec_status(void) {
    if (!g_iec.take_status) return;
    u8 status = g_iec.take_status(g_iec.ctx);
    if (status) cpu_mem_write(0x90, (u8)(cpu_mem_read(0x90) | status));
}

/* The command-level device has no CIA shift-register endpoint.  Keep the
 * C128 KERNAL on its normal serial routines (which are trapped below) rather
 * than letting BASIC 7 select the 1571 burst path.  Bit 6 of $0A1C is the
 * KERNAL's "fast serial available" flag, tested by DLOAD at $F3E0. */
static void select_trapped_serial(void) {
    if (g_iec.force_slow_serial &&
        !(g_iec.c64_mode && g_iec.c64_mode(g_iec.ctx)))
        cpu_mem_write(0x0A1C, (u8)(cpu_mem_read(0x0A1C) & ~0x40));
}

DWORD traps_handler(void) {
    unsigned pc = reg_pc;
    if (g_tape_active) {
        for (size_t list = 0; list < 2; ++list) {
          const C128Trap *traps = list ? g_c64_tape_traps : g_tape_traps;
          u16 workspace = list ? 0x029F : 0x0A09;
          for (size_t i = 0; i < 2; ++i) {
            const C128Trap *t = &traps[i];
            if (pc != t->addr && pc != (unsigned)(t->addr + 1)) continue;
            if (i == 0) {
                u8 header[21] = { 5 }; /* EOF if no next file. */
                if (g_tape.next_header)
                    g_tape.next_header(g_tape.ctx, header);
                u16 buffer = (u16)(cpu_mem_read(0xB2) |
                                   ((u16)cpu_mem_read(0xB3) << 8));
                for (unsigned j = 0; j < sizeof(header); ++j)
                    cpu_mem_write((u16)(buffer + j), header[j]);
                if (getenv("C128_TAPE_TRACE"))
                    fprintf(stderr, "[tape] header type=%u start=$%04x end=$%04x buffer=$%04x\n",
                            header[0], header[1] | header[2] << 8,
                            header[3] | header[4] << 8, buffer);
                cpu_mem_write(0x90, 0);
                cpu_mem_write(0x93, 0);
                cpu_mem_write(workspace, 0);
                cpu_mem_write((u16)(workspace + 1), 0);
                maincpu_set_carry(0);
                maincpu_set_zero(1);
            } else {
                u16 start = (u16)(cpu_mem_read(0xC1) |
                                   ((u16)cpu_mem_read(0xC2) << 8));
                u16 end = (u16)(cpu_mem_read(0xAE) |
                                 ((u16)cpu_mem_read(0xAF) << 8));
                bool ok = maincpu_get_x() == 0x0e && end >= start;
                for (u32 addr = start; ok && addr < end; ++addr) {
                    int byte = g_tape.read_byte ? g_tape.read_byte(g_tape.ctx) : -1;
                    if (byte < 0) ok = false;
                    else cpu_mem_write((u16)addr, (u8)byte);
                }
                cpu_mem_write(workspace, 0);
                cpu_mem_write((u16)(workspace + 1), 0);
                cpu_mem_write(0x90, ok ? 0x40 : 0x10);
                if (getenv("C128_TAPE_TRACE"))
                    fprintf(stderr, "[tape] receive X=$%02x start=$%04x end=$%04x ok=%d\n",
                            maincpu_get_x(), start, end, ok);
                maincpu_set_carry(0);
                maincpu_set_interrupt(0);
            }
            maincpu_set_pc(t->resume);
            return 0;
          }
        }
    }
    for (size_t list = 0; list < 2; ++list) {
      const C128Trap *traps = list ? g_c64_serial_traps : g_serial_traps;
      size_t trap_count = list ? N_C64_SERIAL_TRAPS : N_SERIAL_TRAPS;
      for (size_t i = 0; i < trap_count; i++) {
        /* reg_pc is the address just after the fetched TRAP_OPCODE. */
        if (pc == traps[i].addr || pc == (unsigned)(traps[i].addr + 1)) {
            const C128Trap *t = &traps[i];
            switch (t->kind) {
                case TRAP_ATTENTION:
                    /* The KERNAL passes serial-bus bytes through BSOUR ($95),
                     * not in A.  Match VICE's serial_trap_attention(). */
                    if (g_iec.attention) g_iec.attention(g_iec.ctx, cpu_mem_read(0x95));
                    apply_iec_status();
                    select_trapped_serial();
                    maincpu_set_carry(0);
                    maincpu_set_interrupt(0);
                    break;
                case TRAP_SEND:
                    /* SerialSendByte uses the same KERNAL bus buffer. */
                    if (g_iec.send) g_iec.send(g_iec.ctx, cpu_mem_read(0x95));
                    apply_iec_status();
                    select_trapped_serial();
                    maincpu_set_carry(0);
                    maincpu_set_interrupt(0);
                    break;
                case TRAP_RECEIVE: {
                    u8 b = 0;
                    int st = g_iec.receive ? g_iec.receive(g_iec.ctx, &b) : 0;
                    /* C128 serial_trap_init() uses $A4 as its input temp. */
                    cpu_mem_write(0xA4, b);
                    maincpu_set_a(b);
                    maincpu_set_sign((b & 0x80) != 0);
                    maincpu_set_zero(b == 0);
                    /* The KERNAL's serial-receive routine signals end-of-input
                     * (EOI) through the status byte at $90 (bit 0x40), which
                     * READST ($FFB7) returns. On the final byte of a stream we
                     * set that bit, like VICE's serial_trap_receive does. */
                    if (st < 0)
                        cpu_mem_write(0x90, (u8)(cpu_mem_read(0x90) | 0x02));
                    else if (st == 2)
                        cpu_mem_write(0x90, (u8)(cpu_mem_read(0x90) | 0x40));
                    else if (st == 0)
                        cpu_mem_write(0x90, (u8)(cpu_mem_read(0x90) | 0x80));
                    maincpu_set_carry(0);
                    maincpu_set_interrupt(0);
                    break;
                }
                case TRAP_READY:
                default:
                    maincpu_set_a(1);
                    maincpu_set_sign(0);
                    maincpu_set_zero(0);
                    maincpu_set_interrupt(0);
                    break;
            }
            maincpu_set_pc(t->resume);
            return 0;
        }
      }
    }
    return (DWORD)-1;
}

/* Patch the KERNAL ROM so the IEC traps fire. kernal is the 8K image that
 * maps at $E000-$FFFF. cb may be NULL (only the serial-ready routines are
 * patched, so the boot does not hang). */
void cpu_install_iec_traps(u8 *kernal, const IecCallbacks *cb) {
    if (cb) g_iec = *cb;
    for (size_t i = 0; i < N_SERIAL_TRAPS; i++) {
        unsigned off = g_serial_traps[i].addr - 0xE000;
        if (off < 0x2000)
            kernal[off] = TRAP_OPCODE;
    }
}

void cpu_install_c64_iec_traps(u8 *kernal64, const IecCallbacks *cb) {
    if (cb) g_iec = *cb;
    if (!kernal64) return;
    for (size_t i = 0; i < N_C64_SERIAL_TRAPS; ++i) {
        unsigned off = g_c64_serial_traps[i].addr - 0xE000;
        if (off < 0x2000) kernal64[off] = TRAP_OPCODE;
    }
}

void cpu_install_serial_traps(u8 *kernal) {
    cpu_install_iec_traps(kernal, NULL);
}

void cpu_set_tape_traps(u8 *kernal, const TapeCallbacks *cb) {
    if (!kernal) return;
    if (g_tape_kernal != kernal) {
        g_tape_kernal = kernal;
        for (size_t i = 0; i < 2; ++i)
            memcpy(g_tape_original[i], kernal + g_tape_traps[i].addr - 0xE000, 3);
    }
    g_tape_active = cb != NULL;
    if (cb) g_tape = *cb;
    else memset(&g_tape, 0, sizeof(g_tape));
    for (size_t i = 0; i < 2; ++i) {
        u8 *site = kernal + g_tape_traps[i].addr - 0xE000;
        if (g_tape_active) site[0] = TRAP_OPCODE;
        else memcpy(site, g_tape_original[i], 3);
    }
}

void cpu_set_c64_tape_traps(u8 *kernal64, const TapeCallbacks *cb) {
    if (!kernal64) return;
    if (g_c64_tape_kernal != kernal64) {
        g_c64_tape_kernal = kernal64;
        for (size_t i = 0; i < 2; ++i)
            memcpy(g_c64_tape_original[i],
                   kernal64 + g_c64_tape_traps[i].addr - 0xE000, 3);
    }
    g_tape_active = cb != NULL;
    if (cb) g_tape = *cb;
    else memset(&g_tape, 0, sizeof(g_tape));
    for (size_t i = 0; i < 2; ++i) {
        u8 *site = kernal64 + g_c64_tape_traps[i].addr - 0xE000;
        if (g_tape_active) site[0] = TRAP_OPCODE;
        else memcpy(site, g_c64_tape_original[i], 3);
    }
}

monitor_interface_t *maincpu_monitor_interface_get(void) {
    return NULL;
}
void monitor_startup(int space) {
    (void)space;
}
int monitor_force_import(int space) {
    (void)space;
    return 0;
}
void monitor_check_icount(unsigned int pc) {
    (void)pc;
}
void monitor_check_icount_interrupt(void) {
}
int monitor_check_breakpoints(int space, unsigned int pc) {
    (void)space; (void)pc;
    return 0;
}
void monitor_check_watchpoints(unsigned int last_addr, unsigned int pc) {
    (void)last_addr; (void)pc;
}

/* ------------------------------------------------------------------------- */
/* Alarm context stubs (no cycle-exact alarms are wired yet).                */

alarm_context_t *alarm_context_new(void) {
    return (alarm_context_t *)calloc(1, 1);
}
void alarm_context_destroy(alarm_context_t *ctx) {
    free(ctx);
}
void alarm_context_set_pending_clk(alarm_context_t *ctx, CLOCK clk) {
    (void)ctx; (void)clk;
}
CLOCK alarm_context_next_pending_clk(alarm_context_t *ctx) {
    (void)ctx;
    return CLOCK_MAX;
}
void alarm_context_dispatch(alarm_context_t *ctx, CLOCK clk) {
    (void)ctx; (void)clk;
}

clk_guard_t *clk_guard_new(void) {
    return (clk_guard_t *)calloc(1, 1);
}
void clk_guard_destroy(clk_guard_t *g) {
    free(g);
}

/* ------------------------------------------------------------------------- */
/* Public Cpu8502 interface (wraps the VICE core).                           */

void cpu_init(Cpu8502 *cpu, CpuBus bus) {
    memset(cpu, 0, sizeof(*cpu));
    g_bus = bus;
    cpu->bus = bus;

    /* Point the memory dispatch at the machine bus. */
    for (int i = 0; i <= 0x100; i++) {
        g_read_tab[i] = cpu_mem_read;
        g_write_tab[i] = cpu_mem_write;
    }

    if (!maincpu_int_status) {
        maincpu_int_status = interrupt_cpu_status_new();
        interrupt_cpu_status_init(maincpu_int_status, &last_opcode_info);
        interrupt_cpu_status_int_new(maincpu_int_status, "main");
        maincpu_alarm_context = alarm_context_new();
    }

    memset(&maincpu_regs, 0, sizeof(maincpu_regs));
    maincpu_regs.sp = 0xFD;
    maincpu_regs.p = 0x24;
    maincpu_clk = 0;
    maincpu_clk_limit = 0;
    g_cpu_jammed = false;
}

void cpu_attach_mem(Cpu8502 *cpu, u8 *ram) {
    (void)cpu;
    mem_ram = (BYTE *)ram;
    mem_page_one = mem_ram + 0x100;
}

void cpu_set_stack_page(u8 *page) {
    mem_page_one = (BYTE *)page;
}

void cpu_reset(Cpu8502 *cpu) {
    /* Read the reset vector through the bus and set the register file. */
    u16 rv = (u16)(g_bus.read(g_bus.ctx, 0xFFFC) | (g_bus.read(g_bus.ctx, 0xFFFD) << 8));
    memset(&maincpu_regs, 0, sizeof(maincpu_regs));
    maincpu_regs.pc = rv;
    maincpu_regs.sp = 0xFD;
    maincpu_regs.p = 0x24;
    maincpu_clk = 0;
    g_cpu_jammed = false;
    if (maincpu_int_status) {
        interrupt_cpu_status_reset(maincpu_int_status);
    }
    cpu->cycles = 0;
}

int cpu_step(Cpu8502 *cpu) {
    /* Run until the frame's cycle budget is consumed. */
    CLOCK budget = (cpu->fast ? 2 : 1) * CPU_PAL_FRAME_CYCLES;
    maincpu_clk_limit = maincpu_clk + budget;
    CLOCK before = maincpu_clk;
    maincpu_mainloop();
    maincpu_clk_limit = 0;

    /* Mirror the register file back into the Cpu8502 for the machine. */
    cpu->pc = (u16)maincpu_regs.pc;
    cpu->a = maincpu_regs.a;
    cpu->x = maincpu_regs.x;
    cpu->y = maincpu_regs.y;
    cpu->sp = maincpu_regs.sp;
    cpu->p = maincpu_regs.p;
    cpu->cycles += (u64)(maincpu_clk - before);
    return (int)(maincpu_clk - before);
}

/* Run up to `budget` cycles (for chunked raster stepping). */
int cpu_step_budget(Cpu8502 *cpu, int budget) {
    maincpu_clk_limit = maincpu_clk + budget;
    CLOCK before = maincpu_clk;
    maincpu_mainloop();
    maincpu_clk_limit = 0;

    /* Mirror the register file back into the Cpu8502 for the machine. */
    cpu->pc = (u16)maincpu_regs.pc;
    cpu->a = maincpu_regs.a;
    cpu->x = maincpu_regs.x;
    cpu->y = maincpu_regs.y;
    cpu->sp = maincpu_regs.sp;
    cpu->p = maincpu_regs.p;
    cpu->cycles += (u64)(maincpu_clk - before);
    return (int)(maincpu_clk - before);
}

void cpu_irq(Cpu8502 *cpu, bool level) {
    if (cpu) cpu->irq_level = level;
    if (maincpu_int_status)
        interrupt_set_irq(maincpu_int_status, 0, level ? 1 : 0, maincpu_clk);
}

void cpu_nmi(Cpu8502 *cpu, bool level) {
    if (cpu) cpu->nmi_level = level;
    if (maincpu_int_status)
        interrupt_set_nmi(maincpu_int_status, 0, level ? 1 : 0, maincpu_clk);
}

void cpu_pc(Cpu8502 *cpu, u16 pc) {
    (void)cpu;
    maincpu_regs.pc = pc;
}

u64 cpu_cycles(void) {
    return (u64)maincpu_clk;
}

bool cpu_rmw_active(void) {
    return maincpu_rmw_flag != 0;
}

bool cpu_take_jam(u16 *pc) {
    bool jammed = g_cpu_jammed;
    if (jammed && pc) *pc = g_cpu_jam_pc;
    g_cpu_jammed = false;
    return jammed;
}

void cpu_state_get(const Cpu8502 *cpu, Cpu8502State *state) {
    if (!cpu || !state) return;
    state->a = maincpu_regs.a;
    state->x = maincpu_regs.x;
    state->y = maincpu_regs.y;
    state->sp = maincpu_regs.sp;
    state->p = (u8)MOS6510_REGS_GET_STATUS(&maincpu_regs);
    state->pc = (u16)maincpu_regs.pc;
    state->clock = (u64)maincpu_clk;
    state->cycles = cpu->cycles;
    state->last_opcode_info = last_opcode_info;
    state->irq_level = cpu->irq_level;
    state->nmi_level = cpu->nmi_level;
    state->fast = cpu->fast;
    state->io_ddr = cpu->io_ddr;
    state->io_port = cpu->io_port;
}

void cpu_state_set(Cpu8502 *cpu, const Cpu8502State *state) {
    if (!cpu || !state) return;
    MOS6510_REGS_SET_A(&maincpu_regs, state->a);
    MOS6510_REGS_SET_X(&maincpu_regs, state->x);
    MOS6510_REGS_SET_Y(&maincpu_regs, state->y);
    MOS6510_REGS_SET_SP(&maincpu_regs, state->sp);
    MOS6510_REGS_SET_PC(&maincpu_regs, state->pc);
    MOS6510_REGS_SET_STATUS(&maincpu_regs, state->p);
    reg_pc = state->pc;
    maincpu_clk = (CLOCK)state->clock;
    maincpu_clk_limit = 0;
    maincpu_rmw_flag = 0;
    g_cpu_jammed = false;
    last_opcode_info = state->last_opcode_info;
    if (maincpu_int_status)
        interrupt_cpu_status_reset(maincpu_int_status);

    cpu->a = state->a;
    cpu->x = state->x;
    cpu->y = state->y;
    cpu->sp = state->sp;
    cpu->p = state->p;
    cpu->pc = state->pc;
    cpu->cycles = state->cycles;
    cpu->irq_level = false;
    cpu->nmi_level = false;
    cpu->fast = state->fast;
    cpu->io_ddr = state->io_ddr;
    cpu->io_port = state->io_port;
    cpu_irq(cpu, state->irq_level);
    cpu_nmi(cpu, state->nmi_level);
    cpu->irq_level = state->irq_level;
    cpu->nmi_level = state->nmi_level;
}
