#include "c128.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Frame counter for the z80.c debug instrumentation (ONE_K_TRACE_IM1). */
int c128_frame_count = 0;

/* --- CPU bus: route CPU reads/writes through memory + I/O. --- */

static u8 io_read(C128 *c, u16 addr) {
    u8 v;
    if (addr >= 0xD000 && addr < 0xD400) v = vic_read(&c->vic, addr);
    else if (addr >= 0xD400 && addr < 0xD800) v = sid_read(&c->sid, addr);
    else if (addr >= 0xD800 && addr < 0xDC00) {
        unsigned bank = c->mem.pla_data & 0x01;   /* CPU colour-RAM bank */
        v = c->mem.color_ram[bank * 0x400 + (addr & 0x3FF)];
    }
    else if (addr >= 0xD500 && addr < 0xD510 && c->mem.mmu.mmio) v = mmu_read(&c->mem.mmu, addr);
    else if (addr >= 0xDC00 && addr < 0xDD00) {
        /* CIA1 keyboard scan: the KERNAL drives port A as the row output and
         * reads port B for the columns. */
        if ((addr & 0x0F) == 0x01) {              /* port B = columns */
            /* Row select is the driven port A value: PRA | ~DDRA (VICE's old_pa).
             * A row is scanned while its bit is low. */
            u8 rowsel = c->cia1.pra | ~c->cia1.ddra;
            u8 cols = 0xFF;
            for (int row = 0; row < KBD_ROWS; row++)
                if ((rowsel & (1 << row)) == 0) cols &= kbd_matrix(&c->kbd, row);
            v = cols;
        } else {
            v = cia_read(&c->cia1, addr);
        }
    }
    else if (addr >= 0xDD00 && addr < 0xDE00) v = cia_read(&c->cia2, addr);
    else if (addr >= 0xD600 && addr < 0xD700)
        v = ((addr & 1) == 0) ? vdc_read_status(&c->vdc) : vdc_read_data(&c->vdc);
    else v = 0xFF;

    return v;
}

static void io_write(C128 *c, u16 addr, u8 val) {
    if (addr >= 0xD000 && addr < 0xD400) { vic_write(&c->vic, addr, val); return; }
    if (addr >= 0xD400 && addr < 0xD800) { sid_write(&c->sid, addr, val); return; }
    if (addr >= 0xD800 && addr < 0xDC00) {
        unsigned bank = c->mem.pla_data & 0x01;   /* CPU colour-RAM bank */
        c->mem.color_ram[bank * 0x400 + (addr & 0x3FF)] = val & 0x0F;
        return;
    }
    if (addr >= 0xD500 && addr < 0xD510 && c->mem.mmu.mmio) {
        mmu_write(&c->mem.mmu, addr, val);
        return;
    }
    if (addr >= 0xDC00 && addr < 0xDD00) { cia_write(&c->cia1, addr, val); return; }
    if (addr >= 0xDD00 && addr < 0xDE00) { cia_write(&c->cia2, addr, val); return; }
    if (addr >= 0xD600 && addr < 0xD700) {
        if ((addr & 1) == 0) vdc_write_index(&c->vdc, val);   /* $D600 */
        else                 vdc_write_data(&c->vdc, val);    /* $D601 */
        return;
    }
}

/* Decode the 8502 $01 port (the PLA). The effective port value is
 * (data & dir) | ~dir; its low bits select the colour-RAM banks and chargen. */
static void pla_update(C128 *c) {
    u8 data = c->cpu.io_port & c->cpu.io_ddr;
    u8 dir  = c->cpu.io_ddr;
    c->mem.pla_data = (u8)(data | ~dir);
}

u8 c128_mem_read(void *ctx, u16 addr) {
    C128 *c = ctx;
    /* 8502 on-chip I/O port at $0000 (DDR) and $0001 (port) drives the MMU. */
    if (addr == 0x0000) return c->cpu.io_ddr;
    if (addr == 0x0001) return c->cpu.io_port;
    if (addr >= 0xD000 && addr < 0xE000) return io_read(c, addr);
    return mem_read(&c->mem, addr);
}

void c128_mem_write(void *ctx, u16 addr, u8 val) {
    C128 *c = ctx;
    if (addr == 0x0000) { c->cpu.io_ddr = val; pla_update(c); return; }
    if (addr == 0x0001) { c->cpu.io_port = val; pla_update(c); return; }
    if (addr >= 0xD000 && addr < 0xE000) { io_write(c, addr, val); return; }
    mem_write(&c->mem, addr, val);
}

/* --- Z80 bus (CP/M mode). Wired but not stepped until CP/M is ported. --- */
static u8  z80_mem_read (void *ctx, u16 addr) { return c128_mem_read(ctx, addr); }
static void z80_mem_write(void *ctx, u16 addr, u8 val) { c128_mem_write(ctx, addr, val); }
static u8  z80_io_read  (void *ctx, u16 port) { (void)ctx; (void)port; return 0xFF; }
static void z80_io_write(void *ctx, u16 port, u8 val) { (void)ctx; (void)port; (void)val; }

/* --- Machine lifecycle --- */

void c128_init(C128 *c, Config *cfg) {
    memset(c, 0, sizeof(*c));
    c->cfg = cfg;
    c->fast = cfg ? cfg->fast : false;

    mem_init(&c->mem);
    cpu_init(&c->cpu, (CpuBus){ .read = c128_mem_read,
                                .write = c128_mem_write,
                                .ctx = c });
    cpu_attach_mem(&c->cpu, c->mem.ram);
    c->cpu.fast = c->fast;
    z80_init(&c->z80);
    c->z80_bus = (Z80Bus){ .mem_read = z80_mem_read,
                           .mem_write = z80_mem_write,
                           .io_read = z80_io_read,
                           .io_write = z80_io_write,
                           .tick = NULL,
                           .ticked_in_step = NULL,
                           .ctx = c };
    vic_init(&c->vic);
    vdc_init(&c->vdc);
    cia_init(&c->cia1);
    cia_init(&c->cia2);
    sid_init(&c->sid);
    kbd_init(&c->kbd);

    /* Reset is deferred: the host loads machine ROMs after c128_init(), and
     * the reset vector must be read from the loaded KERNAL ROM. */
}

void c128_reset(C128 *c) {
    mem_reset(&c->mem);
    cpu_reset(&c->cpu);
    vic_reset(&c->vic);
    vdc_reset(&c->vdc);
    cia_reset(&c->cia1);
    cia_reset(&c->cia2);
    sid_reset(&c->sid);
    kbd_reset(&c->kbd);
    c->paused = false;
}

int c128_frame(C128 *c) {
    /* Run the 8502 in raster-line chunks (63 cycles each), ticking the VIC
     * between chunks so the raster IRQ fires when the raster crosses the
     * compare line (VICE's alarm-based timing). */
    int frame_cycles = c->fast ? 2 * CPU_PAL_FRAME_CYCLES : CPU_PAL_FRAME_CYCLES;
    int remaining = frame_cycles;
    int total = 0;
    while (remaining > 0) {
        int chunk = (remaining > 63) ? 63 : remaining;
        total += cpu_step_budget(&c->cpu, chunk);
        remaining -= chunk;
        cia_tick(&c->cia1, chunk);
        bool vic_irq = vic_tick(&c->vic);
        cpu_irq(&c->cpu, cia_irq_line(&c->cia1) || vic_irq);
    }
    c->total_cycles += (u64)total;
    c128_frame_count++;

    /* Render the VIC-IIe frame (40-column). The 8563 VDC framebuffer is
     * rendered only when the KERNAL actively drives it (80-col mode); the
     * C128 boots to the 40-col VIC. */
    vic_render(&c->vic, &c->mem, &c->display);
    return total;
}

u64 c128_cycles_to_ns(const C128 *c, int cycles) {
    u64 hz = c->fast ? 2000000ULL : 1000000ULL;
    return ((u64)(uint64_t)cycles * 1000000000ULL) / hz;
}

void c128_key_event(C128 *c, int scancode, bool down) {
    int row, col;
    if (kbd_map_scancode(scancode, &row, &col))
        kbd_set(&c->kbd, row, col, down);
}
