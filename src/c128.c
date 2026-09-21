#include "c128.h"
#include "notify.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Frame counter for the z80.c debug instrumentation (ONE_K_TRACE_IM1). */
int c128_frame_count = 0;

/* --- CPU bus: route CPU reads/writes through memory + I/O. --- */

static u8 io_read(C128 *c, u16 addr) {
    u8 v;
    if (addr >= 0xD000 && addr < 0xD400) v = vic_read(&c->vic, addr);
    else if (addr >= 0xD400 && addr < 0xD500) {
        unsigned reg = addr & 0x1f;
        if (reg == 0x19 || reg == 0x1a) {
            /* CIA1 PA6/PA7 select the paddle/mouse POT lines. */
            u8 select = (u8)((c->cia1.pra | ~c->cia1.ddra) >> 6) & 3;
            bool y = reg == 0x1a;
            u8 p1 = joyports_pot(&c->joyports, 0,
                                c->cfg->joy_port_mode[0] == JOYPORT_MOUSE, y);
            u8 p2 = joyports_pot(&c->joyports, 1,
                                c->cfg->joy_port_mode[1] == JOYPORT_MOUSE, y);
            v = (select & 1 ? p1 : 0xff) & (select & 2 ? p2 : 0xff);
        } else v = sid_read(&c->sid, addr);
    }
    else if (addr >= 0xD800 && addr < 0xDC00) {
        unsigned bank = c->mem.pla_data & 0x01;   /* CPU colour-RAM bank */
        v = c->mem.color_ram[bank * 0x400 + (addr & 0x3FF)];
    }
    else if (addr >= 0xD500 && addr < 0xD510 && c->mem.mmu.mmio) v = mmu_read(&c->mem.mmu, addr);
    else if (addr >= 0xDC00 && addr < 0xDD00) {
        /* CIA1 keyboard scan: the KERNAL drives port A as the row output and
         * reads port B for the columns. */
        if ((addr & 0x0F) == 0x01) {              /* port B = columns + port 1 */
            /* Row select is the driven port A value: PRA | ~DDRA (VICE's old_pa).
             * A row is scanned while its bit is low. */
            u8 rowsel = (c->cia1.pra | ~c->cia1.ddra) &
                joyports_digital(&c->joyports, 1,
                                 c->cfg->joy_port_mode[1] == JOYPORT_MOUSE);
            u8 cols = 0xFF;
            for (int row = 0; row < KBD_ROWS; row++)
                if ((rowsel & (1 << row)) == 0) cols &= kbd_matrix(&c->kbd, row);
            v = cols & (c->cia1.prb | ~c->cia1.ddrb) &
                joyports_digital(&c->joyports, 0,
                                 c->cfg->joy_port_mode[0] == JOYPORT_MOUSE);
        } else if ((addr & 0x0F) == 0x00) {      /* port A = rows + port 2 */
            u8 colsel = (c->cia1.prb | ~c->cia1.ddrb) &
                joyports_digital(&c->joyports, 0,
                                 c->cfg->joy_port_mode[0] == JOYPORT_MOUSE);
            u8 rows = 0xff;
            for (int row = 0; row < KBD_ROWS; ++row)
                if ((kbd_matrix(&c->kbd, row) & colsel) != colsel)
                    rows &= (u8)~(1u << row);
            v = rows & (c->cia1.pra | ~c->cia1.ddra) &
                joyports_digital(&c->joyports, 1,
                                 c->cfg->joy_port_mode[1] == JOYPORT_MOUSE);
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
            if (mmu_take_c64_request(&c->mem.mmu))
                notify_post("C64 MODE IS NOT SUPPORTED - USING NATIVE C128 MODE");
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
 * (data & dir) | ~dir; bits 0-1 select colour-RAM banks and bit 2 determines
 * whether the VIC sees character ROM or RAM in its $1000-$1FFF window. */
static void pla_update(C128 *c) {
    mem_set_processor_port(&c->mem, c->cpu.io_ddr, c->cpu.io_port);
}

u8 c128_mem_read(void *ctx, u16 addr) {
    C128 *c = ctx;
    /* 8502 on-chip I/O port at $0000 (DDR) and $0001 (port) drives the MMU. */
    if (addr == 0x0000) return c->cpu.io_ddr;
    if (addr == 0x0001) return c->cpu.io_port;
    if (addr >= 0xFF00 && addr <= 0xFF04) return mmu_ffxx_read(&c->mem.mmu, addr);
    if (addr >= 0xD000 && addr < 0xE000 && mem_io_visible(&c->mem))
        return io_read(c, addr);
    return mem_read(&c->mem, addr);
}

void c128_mem_write(void *ctx, u16 addr, u8 val) {
    C128 *c = ctx;
    if (addr == 0x0000) { c->cpu.io_ddr = val; pla_update(c); return; }
    if (addr == 0x0001) { c->cpu.io_port = val; pla_update(c); return; }
    if (addr >= 0xFF00 && addr <= 0xFF04) { mmu_ffxx_write(&c->mem.mmu, addr, val); return; }
    if (addr >= 0xD000 && addr < 0xE000 && mem_io_visible(&c->mem)) {
        io_write(c, addr, val);
        return;
    }
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
    joyports_reset(&c->joyports);
    config_normalize_drive_units(cfg);
    drive_init(&c->drive, cfg);
    drive_init(&c->drive2, cfg);
    drive_set_unit(&c->drive2, cfg->drive2_unit);

    /* Reset is deferred: the host loads machine ROMs after c128_init(), and
     * the reset vector must be read from the loaded KERNAL ROM. */
}

/* Expand the two packed 8-byte character sets into the VDC's 16-byte slots. */
static void load_vdc_chargen(C128 *c) {
    for (unsigned ch = 0; ch < 512; ch++)
        memcpy(&c->vdc.ram[0x2000 + ch * 16],
               &c->mem.chargen[ch * 8], 8);
}

void c128_reset(C128 *c) {
    mem_reset(&c->mem);
    cpu_reset(&c->cpu);
    vic_reset(&c->vic);
    vdc_reset(&c->vdc);
    c->vdc_chargen_loaded = false;
    load_vdc_chargen(c);
    cia_reset(&c->cia1);
    cia_reset(&c->cia2);
    sid_reset(&c->sid);
    c->audio_count = 0;
    c->sid_fast_remainder = 0;
    kbd_reset(&c->kbd);
    c->restore_down = false;
    joyports_reset(&c->joyports);
    drive_reset(&c->drive);
    drive_reset(&c->drive2);
    drive_set_unit(&c->drive2, c->cfg->drive2_unit);
    c->paused = false;
    c->frames_since_reset = 0;
    /* Preserve the 40/80 column choice across resets. */
    c->mem.mmu.col4080 = !c->col_mode_80;
    display_set_vdc_active(&c->display, c->col_mode_80);
}

int c128_frame(C128 *c) {
    /* Run the 8502 in raster-line chunks (63 cycles each), ticking the VIC
     * between chunks so the raster IRQ fires when the raster crosses the
     * compare line (VICE's alarm-based timing). */
    int frame_cycles = c->fast ? 2 * CPU_PAL_FRAME_CYCLES : CPU_PAL_FRAME_CYCLES;
    int remaining = frame_cycles;
    int total = 0;
    c->audio_count = 0;
    while (remaining > 0) {
        int chunk = (remaining > 63) ? 63 : remaining;
        total += cpu_step_budget(&c->cpu, chunk);
        remaining -= chunk;
        cia_tick(&c->cia1, chunk);
        cia_tick(&c->cia2, chunk);
        /* Fast mode doubles CPU cycles per frame, not the SID's clock. */
        int sid_cycles = chunk;
        if (c->fast) {
            sid_cycles += c->sid_fast_remainder;
            c->sid_fast_remainder = sid_cycles & 1;
            sid_cycles /= 2;
        }
        c->audio_count += sid_clock(&c->sid, sid_cycles,
            c->audio_frame + c->audio_count,
            C128_AUDIO_FRAME_CAPACITY - c->audio_count);
        bool vic_irq = vic_tick(&c->vic);
        cpu_irq(&c->cpu, cia_irq_line(&c->cia1) || vic_irq);
        cpu_nmi(&c->cpu, cia_irq_line(&c->cia2) || c->restore_down);
    }
    /* The 6526 TOD input follows the PAL 50 Hz mains signal, not the 8502
     * clock (which may run at 2 MHz). One completed PAL frame is one pulse. */
    cia_tod_tick(&c->cia1);
    cia_tod_tick(&c->cia2);
    c->total_cycles += (u64)total;
    c128_frame_count++;
    c->frames_since_reset++;

    /* The KERNAL clears the VDC chargen during its 80-col setup, so reload
     * the packed ROM glyphs into the VDC's 16-byte slots shortly after boot. */
    if (!c->vdc_chargen_loaded && c->frames_since_reset > 8) {
        load_vdc_chargen(c);
        c->vdc_chargen_loaded = true;
    }

    /* $D506 bit 6 selects the VIC's 64K RAM bank on a 128K machine; CIA2
     * port A bits 0-1 select the inverted 16K window inside it. Input pins
     * float high, matching the 6526's (PRA | ~DDRA) effective port value. */
    u8 cia2_pa = c->cia2.pra | (u8)~c->cia2.ddra;
    unsigned vic_bank = ((unsigned)(c->mem.mmu.rcr >> 6) & 0x01) << 2;
    vic_bank |= (unsigned)(~cia2_pa) & 0x03;
    vic_set_bank(&c->vic, vic_bank);

    /* Render both video devices. The latched physical 40/80 key selects the
     * visible output; $00D7 is a KERNAL software flag and can disagree with
     * it (notably when a cartridge draws to VIC while BASIC is in 80-col). */
    vic_render(&c->vic, &c->mem, &c->display);
    vdc_render(&c->vdc, c->display.vdc_pixels, VDC_SCREEN_W, VDC_SCREEN_H);
    display_set_vdc_active(&c->display, !c->mem.mmu.col4080);
    return total;
}

u64 c128_cycles_to_ns(const C128 *c, int cycles) {
    u64 hz = c->fast ? 2000000ULL : 1000000ULL;
    return ((u64)(uint64_t)cycles * 1000000000ULL) / hz;
}

void c128_key_event(C128 *c, int scancode, bool down) {
    if (scancode == SDL_SCANCODE_PAGEUP) {
        c->restore_down = down;
        return;
    }
    int row, col;
    bool shift;
    if (!kbd_map_scancode(scancode, &row, &col, &shift)) return;
    /* Up/Left are the Shifted Down/Right C128 keys: press Shift alongside so
     * the four PC arrow keys work independently. */
    if (shift) kbd_set(&c->kbd, KBD_SHIFT_ROW, KBD_SHIFT_COL, down);
    kbd_set(&c->kbd, row, col, down);
}

/* Latch the physical 40/80 key and mirror the choice into the KERNAL flag so
 * the running BASIC environment follows the host's display choice too. */
void c128_set_4080(C128 *c, bool col80) {
    c->col_mode_80 = col80;
    c->mem.mmu.col4080 = !c->col_mode_80;
    c->mem.ram[0xD7] = c->col_mode_80 ? 0x80 : 0x00;
    display_set_vdc_active(&c->display, c->col_mode_80);
}

/* The two video devices have independent memory and display registers. F10
 * selects between them without copying or resetting either screen. */
void c128_switch_4080(C128 *c) {
    c128_set_4080(c, c->mem.mmu.col4080);
}

/* --- IEC serial-bus forwarding to the pluggable drive. ------------------- */

void c128_iec_attention(void *ctx, u8 b) {
    C128 *c = ctx;
    drive_pair_attention(&c->drive, &c->drive2, c->cfg->second_drive, b);
}

void c128_iec_send(void *ctx, u8 byte) {
    C128 *c = ctx;
    drive_pair_send(&c->drive, &c->drive2, c->cfg->second_drive, byte);
}

int c128_iec_receive(void *ctx, u8 *byte) {
    C128 *c = ctx;
    return drive_pair_receive(&c->drive, &c->drive2,
                              c->cfg->second_drive, byte);
}

u8 c128_iec_take_status(void *ctx) {
    C128 *c = ctx;
    return drive_pair_take_bus_status(&c->drive, &c->drive2,
                                      c->cfg->second_drive);
}
