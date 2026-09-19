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

/* The active CpuBus (its ctx is the C128). Set by cpu_init(). */
static CpuBus g_bus;

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
    return JAM_RESET;
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
 * patching the KERNAL ROM with TRAP_OPCODE (0x02) at the trap addresses. --- */

typedef struct {
    WORD addr;     /* address patched with TRAP_OPCODE */
    WORD resume;   /* address to resume at after the trap */
} C128Trap;

static const C128Trap g_serial_traps[] = {
    { 0xE569, 0xE572 },   /* Serial ready */
    { 0xE4F5, 0xE572 },   /* Serial ready */
    { 0xE5BC, 0xE5C3 },   /* Serial ready poll (LDA $DC0D; AND #$08; BEQ) */
};
#define N_SERIAL_TRAPS (sizeof(g_serial_traps) / sizeof(g_serial_traps[0]))

DWORD traps_handler(void) {
    unsigned pc = reg_pc;
    for (size_t i = 0; i < N_SERIAL_TRAPS; i++) {
        /* reg_pc is the address just after the fetched TRAP_OPCODE. */
        if (pc == g_serial_traps[i].addr || pc == (unsigned)(g_serial_traps[i].addr + 1)) {
            /* "Serial ready": report the IEC bus ready. */
            maincpu_set_a(1);
            maincpu_set_sign(0);
            maincpu_set_zero(0);
            maincpu_set_interrupt(0);
            maincpu_set_pc(g_serial_traps[i].resume);
            return 0;
        }
    }
    return (DWORD)-1;
}

/* Patch the KERNAL ROM so the serial traps fire. kernal is the 8K image that
 * maps at $E000-$FFFF. */
void cpu_install_serial_traps(u8 *kernal) {
    for (size_t i = 0; i < N_SERIAL_TRAPS; i++) {
        unsigned off = g_serial_traps[i].addr - 0xE000;
        if (off < 0x2000)
            kernal[off] = TRAP_OPCODE;
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
}

void cpu_attach_mem(Cpu8502 *cpu, u8 *ram) {
    (void)cpu;
    mem_ram = (BYTE *)ram;
}

void cpu_reset(Cpu8502 *cpu) {
    /* Read the reset vector through the bus and set the register file. */
    u16 rv = (u16)(g_bus.read(g_bus.ctx, 0xFFFC) | (g_bus.read(g_bus.ctx, 0xFFFD) << 8));
    memset(&maincpu_regs, 0, sizeof(maincpu_regs));
    maincpu_regs.pc = rv;
    maincpu_regs.sp = 0xFD;
    maincpu_regs.p = 0x24;
    maincpu_clk = 0;
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
    (void)cpu;
    if (maincpu_int_status)
        interrupt_set_irq(maincpu_int_status, 0, level ? 1 : 0, maincpu_clk);
}

void cpu_nmi(Cpu8502 *cpu, bool level) {
    (void)cpu;
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
