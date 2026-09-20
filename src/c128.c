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
    else if (addr >= 0xD400 && addr < 0xD500) v = sid_read(&c->sid, addr);
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
    if (addr >= 0xD400 && addr < 0xD500) { sid_write(&c->sid, addr, val); return; }
    if (addr >= 0xD800 && addr < 0xDC00) {
        unsigned bank = c->mem.pla_data & 0x01;   /* CPU colour-RAM bank */
        c->mem.color_ram[bank * 0x400 + (addr & 0x3FF)] = val & 0x0F;
        return;
    }
    if (addr >= 0xD500 && addr < 0xD510) {
        if (c->mem.mmu.mmio) {
            mmu_write(&c->mem.mmu, addr, val);
            return;
        }
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
    if (addr >= 0xFF00 && addr <= 0xFF04) return mmu_ffxx_read(&c->mem.mmu, addr);
    if (addr >= 0xD000 && addr < 0xE000) return io_read(c, addr);
    return mem_read(&c->mem, addr);
}

void c128_mem_write(void *ctx, u16 addr, u8 val) {
    C128 *c = ctx;
    if (addr == 0x0000) { c->cpu.io_ddr = val; pla_update(c); return; }
    if (addr == 0x0001) { c->cpu.io_port = val; pla_update(c); return; }
    if (addr >= 0xFF00 && addr <= 0xFF04) { mmu_ffxx_write(&c->mem.mmu, addr, val); return; }
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
    drive_init(&c->drive, cfg);

    /* Reset is deferred: the host loads machine ROMs after c128_init(), and
     * the reset vector must be read from the loaded KERNAL ROM. */
}

void c128_reset(C128 *c) {
    mem_reset(&c->mem);
    cpu_reset(&c->cpu);
    vic_reset(&c->vic);
    vdc_reset(&c->vdc);
    c->vdc_chargen_loaded = false;
    /* Load the 80-column VDC character generator (block 0x000 of the 4K
     * chargen ROM, read 8 bytes per char) into VDC RAM at the chargen
     * address, so the KERNAL's later boot clears it to 0xFF and the reload
     * in c128_frame() restores the glyphs. */
    memcpy(&c->vdc.ram[0x2000], &c->mem.chargen[0x000], 0x800);
    cia_reset(&c->cia1);
    cia_reset(&c->cia2);
    sid_reset(&c->sid);
    kbd_reset(&c->kbd);
    drive_reset(&c->drive);
    c->paused = false;
    c->frames_since_reset = 0;
    /* Preserve the 40/80 column choice across resets. */
    c->mem.mmu.col4080 = !c->col_mode_80;
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
    c->frames_since_reset++;

    /* The KERNAL clears the VDC chargen (writes 0xFF) during its 80-col setup
     * but does not copy the glyphs, so load the character generator (the same
     * one VICE's KERNAL copies to the VDC, i.e. the chargen at offset 0)
     * ourselves shortly after boot (and after each reset). */
    if (!c->vdc_chargen_loaded && c->frames_since_reset > 8) {
        memcpy(&c->vdc.ram[0x2000], &c->mem.chargen[0x000], 0x800);
        c->vdc_chargen_loaded = true;
    }

    /* Render the VIC-IIe frame (40-column) and the VDC 8563 (80-column)
     * framebuffer every frame. The active display is selected from the
     * KERNAL's 40/80 mode flag ($00D7: 0 = 40-col, non-zero = 80-col). */
    vic_render(&c->vic, &c->mem, &c->display);
    vdc_render(&c->vdc, c->display.vdc_pixels, VDC_SCREEN_W, VDC_SCREEN_H);
    c->display.vdc_active = (c->mem.ram[0x00D7] != 0);
    return total;
}

u64 c128_cycles_to_ns(const C128 *c, int cycles) {
    u64 hz = c->fast ? 2000000ULL : 1000000ULL;
    return ((u64)(uint64_t)cycles * 1000000000ULL) / hz;
}

void c128_key_event(C128 *c, int scancode, bool down) {
    int row, col;
    bool shift;
    if (!kbd_map_scancode(scancode, &row, &col, &shift)) return;
    /* Up/Left are the Shifted Down/Right C128 keys: press Shift alongside so
     * the four PC arrow keys work independently. */
    if (shift) kbd_set(&c->kbd, KBD_SHIFT_ROW, KBD_SHIFT_COL, down);
    kbd_set(&c->kbd, row, col, down);
}

/* Copy the current text screen between the VIC-II (40x25) and the VDC (80x25)
 * so the READY prompt "migrates" to whichever display is now selected. */
static void migrate_vic_to_vdc(C128 *c) {
    c->vdc.screen_text_cols = 80;
    c->vdc.screen_textlines = 25;
    c->vdc.bytes_per_char   = 8;
    c->vdc.screen_adr  = 0x0000;
    c->vdc.chargen_adr = 0x2000;
    unsigned sa = (c->vic.screen_addr & 0x3FFF) & 0x3C00;
    if (sa < 0x400) sa = 0x400;
    for (int row = 0; row < 25; row++)
        for (int col = 0; col < 80; col++) {
            int vcol = col - 20;   /* centre the 40-col content in 80 cols */
            u8 ch = (vcol >= 0 && vcol < 40) ? c->mem.ram[sa + row * 40 + vcol] : 0x20;
            c->vdc.ram[(c->vdc.screen_adr + row * 80 + col) & 0xFFFF] = ch;
        }
    c->vdc.dirty = true;
}

static void migrate_vdc_to_vic(C128 *c) {
    unsigned sa = (c->vic.screen_addr & 0x3FFF) & 0x3C00;
    if (sa < 0x400) sa = 0x400;
    int cols = c->vdc.screen_text_cols;
    if (cols > 80) cols = 80;
    for (int row = 0; row < 25; row++)
        for (int col = 0; col < 40; col++) {
            int vcol = col + 20;   /* take the centred 40 cols of the 80-col row */
            u8 ch = (vcol >= 0 && vcol < cols)
                  ? c->vdc.ram[(c->vdc.screen_adr + row * 80 + vcol) & 0xFFFF] : 0x20;
            c->mem.ram[sa + row * 40 + col] = ch;
        }
}

/* Toggle the 40/80 column mode: flip the MMU sense key, the KERNAL mode flag
 * ($00D7) and migrate the text screen to the newly-selected display. */
void c128_switch_4080(C128 *c) {
    c->col_mode_80 = !c->col_mode_80;
    c->mem.mmu.col4080 = !c->col_mode_80;
    c->mem.ram[0xD7] = c->col_mode_80 ? 0x80 : 0x00;
    if (c->col_mode_80)
        migrate_vic_to_vdc(c);   /* switched to 80-col VDC */
    else
        migrate_vdc_to_vic(c);   /* switched to 40-col VIC */
}
