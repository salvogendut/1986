#include "c128.h"
#include "notify.h"
#include "leds.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Frame counter for the z80.c debug instrumentation (ONE_K_TRACE_IM1). */
int c128_frame_count = 0;

static void c128_cpu_bus_wait(C128 *c, bool write, bool io);
static void real_drive_media(C128 *c, unsigned slot);

static bool drive_probe_active(const C128 *c) {
    return c->drive_raw_iec;
}

static bool drive_busy(const GcrDrive *g) {
    /* Motor spin-down can outlast LOAD considerably. Don't speed up the
     * newly loaded program during that idle time, or on error LED flashes
     * with the motor stopped. Writes remain busy even with the LED off. */
    return g->motor && (g->led || g->write_mode);
}

static bool real_drive_busy(const C128 *c, unsigned slot) {
    if (c->real_drive_type[slot] == 1581) {
        const Drive1581 *d = &c->real1581[slot];
        return d->fdc.motor && (d->led || (d->fdc.status & WD1770_BUSY));
    }
    return drive_busy(slot ? &c->second_real_drive.gcr : &c->integrated_drive.gcr);
}

bool c128_drive_warp_active(const C128 *c) {
    if (!c->cfg->unthrottled_drive || c->paused) return false;
    /* Follow the running backend, not the restart-pending preference. */
    return (c->drive_raw_iec && real_drive_busy(c, 0)) ||
           (c->drive2_raw_iec && real_drive_busy(c, 1));
}

/* The C128 KERNAL copies its banked FETCH, STASH and CMPARE primitives to
 * $02a2-$02cc.  Each temporarily maps KERNAL out through $ff00, performs one
 * indirect access, then restores the old configuration.  Our raster IRQ is
 * scheduled at instruction boundaries rather than at an exact VIC cycle;
 * defer a level IRQ across only these tiny MMU windows so it cannot enter the
 * common-RAM IRQ trampoline with KERNAL absent and return into overwritten
 * top shared RAM.  The device flag remains pending and is delivered as soon
 * as the restoring $ff00 write completes. */
static bool kernal_bank_access_active(const C128 *c) {
    u16 pc = c->cpu.pc;
    if ((c->mem.mmu.mcr & 0x30) == 0) return false;
    return (pc >= 0x02a8 && pc <= 0x02ab) ||
           (pc >= 0x02b6 && pc <= 0x02ba) ||
           (pc >= 0x02c5 && pc <= 0x02c9);
}

void c128_refresh_vic_bank(C128 *c) {
    /* $D506 bit 6 selects the VIC's 64K RAM bank on a 128K machine; CIA2
     * port A bits 0-1 select the inverted 16K window inside it. Input pins
     * float high, matching the 6526's (PRA | ~DDRA) effective port value. */
    u8 cia2_pa = c->cia2.pra | (u8)~c->cia2.ddra;
    unsigned vic_bank = mem_c64_mode(&c->mem)
        ? (unsigned)c->mem.mmu.c64_ram_bank << 2
        : ((unsigned)(c->mem.mmu.rcr >> 6) & 0x01) << 2;
    vic_bank |= (unsigned)(~cia2_pa) & 0x03;
    vic_set_bank(&c->vic, vic_bank);
}

/* The 8502 core calls I/O handlers during an instruction, at the bus cycle
 * of the access. Synchronize the drives before sampling or changing IEC lines:
 * advancing only after the instruction can miss GEOS fast-serial edges. */
static void drive_sync_to_cpu(C128 *c) {
    if (!c->drive_clock_denominator) return;
    u64 now = c->bus_cycles;
    if (now <= c->drive_host_cycle_synced) return;
    u64 elapsed = now - c->drive_host_cycle_synced;
    c->drive_host_cycle_synced = now;
    if (!drive_probe_active(c)) {
        c->drive_clock_fraction = 0;
        return;
    }
    unsigned drive_hz_per_frame = c->real_drive_type[0] == 1581 ||
                                  c->integrated_drive.clock_2mhz ? 40000u : 20000u;
    u64 scaled = c->drive_clock_fraction + elapsed * drive_hz_per_frame;
    int budget = (int)(scaled / c->drive_clock_denominator);
    c->drive_clock_fraction = (unsigned)(scaled % c->drive_clock_denominator);
    if (c->real_drive_type[0] == 1581) drive1581_advance(&c->real1581[0], budget);
    else drive1571cr_advance(&c->integrated_drive, budget);
    if (c->drive2_raw_iec) {
        unsigned hz2 = c->real_drive_type[1] == 1581 ||
                       c->second_real_drive.clock_2mhz ? 40000u : 20000u;
        u64 scaled2 = c->drive2_clock_fraction + elapsed * hz2;
        int budget2 = (int)(scaled2 / c->drive_clock_denominator);
        c->drive2_clock_fraction = (unsigned)(scaled2 % c->drive_clock_denominator);
        if (c->real_drive_type[1] == 1581) drive1581_advance(&c->real1581[1], budget2);
        else drive1571cr_advance(&c->second_real_drive, budget2);
    }
}

static void drive_via_port_change(void *ctx, unsigned port, u8 pins) {
    C128 *c = ctx;
    if (port == 1 && c->real_drive_type[0] != 1581) iec_bus_set_drive(&c->iec_bus, pins);
}

static void drive2_via_port_change(void *ctx, unsigned port, u8 pins) {
    C128 *c = ctx;
    if (port == 1 && c->real_drive_type[1] != 1581) iec_bus_set_drive2(&c->iec_bus, pins);
}

static void drive1581_port_change(void *ctx, u8 pins) {
    C128 *c = ctx;
    if (c->real_drive_type[0] == 1581) iec_bus_set_drive(&c->iec_bus, pins);
}

static void drive2_1581_port_change(void *ctx, u8 pins) {
    C128 *c = ctx;
    if (c->real_drive_type[1] == 1581) iec_bus_set_drive2(&c->iec_bus, pins);
}

static void drive1581_inputs(void *ctx, unsigned unit, bool atn, bool clock, bool data) {
    Drive1581 *d = ctx;
    drive1581_set_unit(d, unit);
    drive1581_set_iec(d, atn, clock, data);
}

static bool real_rom_loaded(const C128 *c, unsigned slot) {
    if (c->real_drive_type[slot] == 1581) return c->real1581[slot].rom_loaded;
    return slot ? c->second_real_drive.rom_loaded : c->integrated_drive.rom_loaded;
}

static void connect_real_drive(C128 *c, unsigned slot, unsigned unit) {
    if (c->real_drive_type[slot] == 1581)
        iec_bus_attach_1581(&c->iec_bus, slot, drive1581_inputs, &c->real1581[slot], unit);
    else if (slot)
        iec_bus_attach_second(&c->iec_bus, &c->second_real_drive.via1, unit);
    else iec_bus_attach_first(&c->iec_bus, &c->integrated_drive.via1, unit);
}

bool c128_configure_real_drives(C128 *c) {
    c->real_drive_type[0] = c->cfg->drive_type;
    c->real_drive_type[1] = c->cfg->drive2_type;
    c->drive_raw_iec = c->cfg->real_disk_drive && real_rom_loaded(c, 0) &&
                      (!c->cfg->second_drive || real_rom_loaded(c, 1));
    c->drive2_raw_iec = c->drive_raw_iec && c->cfg->second_drive;
    connect_real_drive(c, 0, (unsigned)c->cfg->drive_unit);
    connect_real_drive(c, 1, (unsigned)c->cfg->drive2_unit);
    iec_bus_enable_second(&c->iec_bus, c->drive2_raw_iec);
    return c->drive_raw_iec;
}

C128DriveChangeResult c128_change_drive_type(C128 *c, unsigned slot, int type) {
    if (slot > 1 || (type != 1571 && type != 1581) ||
        !c->cfg->real_disk_drive || (slot && !c->cfg->second_drive))
        return C128_DRIVE_CHANGE_INVALID;
    int types[2] = {c->cfg->drive_type, c->cfg->drive2_type};
    types[slot] = type;
    for (unsigned i = 0; i < (c->cfg->second_drive ? 2u : 1u); ++i) {
        bool loaded = types[i] == 1581 ? c->real1581[i].rom_loaded :
            (i ? c->second_real_drive.rom_loaded : c->integrated_drive.rom_loaded);
        if (!loaded) return C128_DRIVE_CHANGE_ROM_MISSING;
    }
    /* A power cycle affects both devices, even if only one model changes.
     * Do not detach/reset either until all pending GCR data is durable. */
    if (gcr_drive_flush(&c->integrated_drive.gcr) != DISK_SAVE_OK ||
        gcr_drive_flush(&c->second_real_drive.gcr) != DISK_SAVE_OK)
        return C128_DRIVE_CHANGE_WRITE_FAILED;
    gcr_drive_attach(&c->integrated_drive.gcr, NULL);
    gcr_drive_attach(&c->second_real_drive.gcr, NULL);
    wd1770_attach(&c->real1581[0].fdc, NULL);
    wd1770_attach(&c->real1581[1].fdc, NULL);
    if (slot) c->cfg->drive2_type = type;
    else c->cfg->drive_type = type;
    c128_configure_real_drives(c);
    /* A previous missing-ROM fallback may have patched both KERNALs. */
    cpu_remove_iec_traps(c->mem.kernal, c->mem.c64_kernal);
    c128_power_cycle(c);
    real_drive_media(c, 0);
    real_drive_media(c, 1);
    return C128_DRIVE_CHANGE_OK;
}

bool c128_enable_second_real_drive(C128 *c, bool enabled) {
    if (!enabled && c->drive2_raw_iec && c->real_drive_type[1] != 1581 &&
        gcr_drive_flush(&c->second_real_drive.gcr) != DISK_SAVE_OK) return false;
    bool was_real = c->drive2_raw_iec;
    c->drive2_raw_iec = enabled && c->drive_raw_iec && real_rom_loaded(c, 1);
    if (c->drive2_raw_iec && !was_real) {
        if (c->real_drive_type[1] == 1581) drive1581_reset(&c->real1581[1]);
        else drive1571cr_reset(&c->second_real_drive);
        drive_monitor_reset(&c->drive2_monitor);
        c->drive2_clock_fraction = 0;
        c->drive2_media_generation = (unsigned)-1;
    }
    iec_bus_enable_second(&c->iec_bus, c->drive2_raw_iec);
    return true; /* missing ROM: save the preference, but leave device absent */
}

static int flush_integrated_drive(void *ctx) {
    C128 *c = ctx;
    if (gcr_drive_flush(&c->integrated_drive.gcr) != DISK_SAVE_OK) return -1;
    gcr_drive_attach(&c->integrated_drive.gcr, NULL);
    wd1770_attach(&c->real1581[0].fdc, NULL);
    return 0;
}

static int flush_second_real_drive(void *ctx) {
    C128 *c = ctx;
    if (gcr_drive_flush(&c->second_real_drive.gcr) != DISK_SAVE_OK) return -1;
    gcr_drive_attach(&c->second_real_drive.gcr, NULL);
    wd1770_attach(&c->real1581[1].fdc, NULL);
    return 0;
}

static void tape_read_pulse(void *ctx) {
    Cia *cia = ctx;
    cia_set_flag(cia, false);
    cia_set_flag(cia, true);
}

static void tape_motor_update(C128 *c) {
    tape_set_motor(&c->tape, (c->cpu.io_ddr & 0x20) &&
                   !(c->cpu.io_port & 0x20));
}

static bool t64_header_callback(void *ctx, u8 header[21]) {
    return tape_t64_next_header(&((C128 *)ctx)->tape, header);
}

static int t64_byte_callback(void *ctx) {
    return tape_t64_read_byte(&((C128 *)ctx)->tape);
}

void c128_eject_tape(C128 *c) {
    cpu_set_tape_traps(c->mem.kernal, NULL);
    cpu_set_c64_tape_traps(c->mem.c64_kernal, NULL);
    tape_eject(&c->tape);
}

bool c128_mount_tape(C128 *c, const char *path) {
    c128_eject_tape(c);
    if (!tape_mount(&c->tape, path)) return false;
    if (c->tape.kind == TAPE_T64) {
        TapeCallbacks cb = {
            .ctx = c,
            .next_header = t64_header_callback,
            .read_byte = t64_byte_callback,
        };
        cpu_set_tape_traps(c->mem.kernal, &cb);
        if (mem_c64_roms_loaded(&c->mem))
            cpu_set_c64_tape_traps(c->mem.c64_kernal, &cb);
    }
    return true;
}

/* --- CPU bus: route CPU reads/writes through memory + I/O. --- */

u8 c128_vdc_port_read(C128 *c, u16 addr) {
    vdc_set_bus_clock(&c->vdc, c->bus_cycles, false);
    return (addr & 1) == 0 ? vdc_read_status(&c->vdc)
                           : vdc_read_data(&c->vdc);
}

void c128_vdc_port_write(C128 *c, u16 addr, u8 val) {
    vdc_set_bus_clock(&c->vdc, c->bus_cycles, false);
    if ((addr & 1) == 0)
        vdc_write_index(&c->vdc, val);
    else
        vdc_write_data(&c->vdc, val);
}

/* IRQ/NMI sources are level-sensitive.  A device register can acknowledge
 * an interrupt in the middle of an instruction, so propagate the changed
 * line to the active CPU immediately instead of leaving it asserted until
 * the next raster-line boundary. */
static void c128_refresh_interrupt_lines(C128 *c) {
    bool irq = cia_irq_line(&c->cia1) || (c->vic.irq_status & 0x80) || reu_irq(&c->reu);
    if (mmu_cpu_is_8502(&c->mem.mmu)) {
        bool bank_access = kernal_bank_access_active(c);
        if (irq && bank_access)
            cpu_irq_defer(&c->cpu);
        else
            cpu_irq(&c->cpu, irq);
        cpu_nmi(&c->cpu, cia_irq_line(&c->cia2) || c->restore_down);
    } else {
        c->z80.pending_irq = irq;
    }
}

u8 c128_keyboard_port_b(const C128 *c, u8 base_row_select) {
    u8 columns = 0xFF;
    for (int row = 0; row < KBD_BASE_ROWS; ++row)
        if ((base_row_select & (1u << row)) == 0)
            columns &= kbd_matrix(&c->kbd, row);
    for (int row = 0; row < KBD_EXT_ROWS; ++row)
        if ((c->vic.keyboard_select & (1u << row)) == 0)
            columns &= kbd_matrix(&c->kbd, KBD_BASE_ROWS + row);
    return columns;
}

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
        unsigned bank = mem_c64_mode(&c->mem) ? 0 :
                        c->mem.pla_data & 0x01;   /* CPU colour-RAM bank */
        v = c->mem.color_ram[bank * 0x400 + (addr & 0x3FF)];
    }
    else if (!mem_c64_mode(&c->mem) &&
             addr >= 0xD500 && addr < 0xD510 && c->mem.mmu.mmio)
        v = mmu_read(&c->mem.mmu, addr);
    else if (addr >= 0xDC00 && addr < 0xDD00) {
        /* CIA1 keyboard scan: the KERNAL drives port A as the row output and
         * reads port B for the columns. */
        if ((addr & 0x0F) == 0x01) {              /* port B = columns + port 1 */
            /* Row select is the driven port A value: PRA | ~DDRA (VICE's old_pa).
             * A row is scanned while its bit is low. */
            u8 rowsel = (c->cia1.pra | ~c->cia1.ddra) &
                joyports_digital(&c->joyports, 1,
                                 c->cfg->joy_port_mode[1] == JOYPORT_MOUSE);
            u8 cols = c128_keyboard_port_b(c, rowsel);
            v = cols & (c->cia1.prb | ~c->cia1.ddrb) &
                joyports_digital(&c->joyports, 0,
                                 c->cfg->joy_port_mode[0] == JOYPORT_MOUSE);
        } else if ((addr & 0x0F) == 0x00) {      /* port A = rows + port 2 */
            u8 colsel = (c->cia1.prb | ~c->cia1.ddrb) &
                joyports_digital(&c->joyports, 0,
                                 c->cfg->joy_port_mode[0] == JOYPORT_MOUSE);
            u8 rows = 0xff;
            /* Only the base rows are wired back to CIA1 port A. The three
             * C128-only rows are output selectors at $D02F, not PA inputs. */
            for (int row = 0; row < KBD_BASE_ROWS; ++row)
                if ((kbd_matrix(&c->kbd, row) & colsel) != colsel)
                    rows &= (u8)~(1u << row);
            v = rows & (c->cia1.pra | ~c->cia1.ddra) &
                joyports_digital(&c->joyports, 1,
                                 c->cfg->joy_port_mode[1] == JOYPORT_MOUSE);
        } else {
            v = cia_read(&c->cia1, addr);
        }
    }
    else if (addr >= 0xDD00 && addr < 0xDE00) {
        drive_sync_to_cpu(c);
        if ((addr & 15) == 0 && drive_probe_active(c)) {
            /* CIA2 PA6/PA7 are serial CLOCK/DATA inputs regardless of DDR. */
            v = (u8)((cia_read(&c->cia2, addr) & 0x3f) |
                     iec_bus_host_inputs(&c->iec_bus));
        } else v = cia_read(&c->cia2, addr);
    }
    else if (addr >= 0xD600 && addr < 0xD700) {
        /* The VDC remains mapped in the C128's C64 personality. Software
         * such as Elite128 probes and uses it there; VICE likewise registers
         * the $D600-$D6FF device independently of the active personality. */
        v = c128_vdc_port_read(c, addr);
    }
    else if (addr >= 0xDF00) v = reu_read(&c->reu, addr);
    else v = 0xFF;
    if ((addr >= 0xD000 && addr < 0xD400) ||
        (addr >= 0xDC00 && addr < 0xDE00) || addr >= 0xDF00)
        c128_refresh_interrupt_lines(c);
    return v;
}

static void io_write(C128 *c, u16 addr, u8 val) {
    if (addr >= 0xDF00) {
        reu_write(&c->reu, addr, val);
        c128_refresh_interrupt_lines(c);
        return;
    }
    if (addr >= 0xD000 && addr < 0xD400) {
        if ((addr & 0x3F) == 0x19 && cpu_rmw_active())
            vic_write_rmw(&c->vic, addr, val);
        else
            vic_write(&c->vic, addr, val);
        c128_refresh_interrupt_lines(c);
        return;
    }
    if (addr >= 0xD400 && addr < 0xD500) { sid_write(&c->sid, addr, val); return; }
    if (addr >= 0xD800 && addr < 0xDC00) {
        unsigned bank = mem_c64_mode(&c->mem) ? 0 :
                        c->mem.pla_data & 0x01;   /* CPU colour-RAM bank */
        c->mem.color_ram[bank * 0x400 + (addr & 0x3FF)] = val & 0x0F;
        return;
    }
    if (!mem_c64_mode(&c->mem) && addr >= 0xD500 && addr < 0xD510) {
        if (c->mem.mmu.mmio) {
            bool was_c64 = mmu_is_c64_mode(&c->mem.mmu);
            bool d505_c64 = (addr & 0xff) == 0x05 && (val & 0x40);
            /* BASIC's GO64 routine performs its final $D505 write from the
             * four-byte trampoline at $0000-$0003. Keep that user command
             * behind Advanced > C64 Test Mode, but accept direct MMU writes
             * from native C128 loaders just like the hardware and VICE do. */
            bool go64_trampoline = d505_c64 && c->cpu.pc == 0x0004;
            bool missing_c64_roms = d505_c64 &&
                                    !mem_c64_roms_loaded(&c->mem);
            if (go64_trampoline || missing_c64_roms)
                mmu_write_go64(&c->mem.mmu, val);
            else
                mmu_write(&c->mem.mmu, addr, val);
            c128_refresh_vic_bank(c);
            if ((addr & 0xff) == 0x06 || (addr & 0xff) == 0x09)
                cpu_set_stack_page(c->mem.ram + mem_cpu_page_offset(&c->mem, 1));
            if (mmu_take_c64_request(&c->mem.mmu)) {
                notify_post(missing_c64_roms
                    ? "C64 COMPATIBILITY ROMS ARE REQUIRED"
                    : "C64 MODE IS NOT SUPPORTED - USING NATIVE C128 MODE");
            }
            if (!was_c64 && mmu_is_c64_mode(&c->mem.mmu)) {
                cpu_set_stack_page(c->mem.ram +
                    ((u32)c->mem.mmu.c64_ram_bank << 16) + 0x100);
                display_set_vdc_active(&c->display, false);
                notify_post("EXPERIMENTAL C64 TEST MODE");
            }
            return;
        }
    }
    if (addr >= 0xDC00 && addr < 0xDD00) {
        cia_write(&c->cia1, addr, val);
        c128_refresh_interrupt_lines(c);
        return;
    }
    if (addr >= 0xDD00 && addr < 0xDE00) {
        drive_sync_to_cpu(c);
        cia_write(&c->cia2, addr, val);
        if ((addr & 15) == 0 || (addr & 15) == 2) {
            c128_refresh_vic_bank(c);
            iec_bus_set_host(&c->iec_bus, c->cia2.pra, c->cia2.ddra);
        }
        c128_refresh_interrupt_lines(c);
        return;
    }
    if (addr >= 0xD600 && addr < 0xD700) {
        c128_vdc_port_write(c, addr, val);
        return;
    }
}

/* Decode the 8502 $01 port (the PLA). The effective port value is
 * (data & dir) | ~dir; bits 0-1 select colour-RAM banks and bit 2 determines
 * whether the VIC sees character ROM or RAM in its $1000-$1FFF window. */
static void pla_update(C128 *c) {
    mem_set_processor_port(&c->mem, c->cpu.io_ddr, c->cpu.io_port);
}

u8 c128_cpu_port_value(const C128 *c) {
    u8 value = c->cpu.io_port;
    if (!(c->cpu.io_ddr & 0x10)) {
        value |= 0x10; /* unpressed cassette switch is pulled high */
        if (c->tape.play_button) value &= (u8)~0x10;
    }
    if (!(c->cpu.io_ddr & 0x40)) {
        value |= 0x40; /* released CAPS/ASCII-DIN switch is pulled high */
        if (c->kbd.caps_lock) value &= (u8)~0x40;
    }
    return value;
}

u8 c128_mem_read(void *ctx, u16 addr) {
    C128 *c = ctx;
    c128_cpu_bus_wait(c, false,
        addr >= 0xd000 && addr < 0xe000 && mem_io_visible(&c->mem));
    /* 8502 on-chip I/O port at $0000 (DDR) and $0001 (port) drives the MMU. */
    if (addr == 0x0000) return c->cpu.io_ddr;
    if (addr == 0x0001) return c128_cpu_port_value(c);
    if (!mem_c64_mode(&c->mem) && addr >= 0xFF00 && addr <= 0xFF04)
        return mmu_ffxx_read(&c->mem.mmu, addr);
    if (addr >= 0xD000 && addr < 0xE000 && mem_io_visible(&c->mem))
        return io_read(c, addr);
    return mem_read(&c->mem, addr);
}

void c128_mem_write(void *ctx, u16 addr, u8 val) {
    C128 *c = ctx;
    c128_cpu_bus_wait(c, true,
        addr >= 0xd000 && addr < 0xe000 && mem_io_visible(&c->mem));
    if (addr == 0x0000) { c->cpu.io_ddr = val; pla_update(c); tape_motor_update(c); return; }
    if (addr == 0x0001) { c->cpu.io_port = val; pla_update(c); tape_motor_update(c); return; }
    if (!mem_c64_mode(&c->mem) && addr >= 0xFF00 && addr <= 0xFF04) {
        mmu_ffxx_write(&c->mem.mmu, addr, val);
        c128_refresh_vic_bank(c);
        c128_refresh_interrupt_lines(c);
        if (addr == 0xFF00) reu_ff00_trigger(&c->reu);
        return;
    }
    if (addr >= 0xD000 && addr < 0xE000 && mem_io_visible(&c->mem)) {
        io_write(c, addr, val);
        return;
    }
    mem_write(&c->mem, addr, val);
    if (addr == 0xFF00) reu_ff00_trigger(&c->reu);
}

/* REU uses the native memory decoder (even while the Z80 owns the bus),
 * but bypasses the 8502 port and page/shared-RAM translations. See VICE's
 * c128mem.c mem_dma_read/store and c128mmu.c mmu_set_dma_bank. */
static u8 reu_host_read(void *ctx, u16 addr) {
    C128 *c = ctx;
    if (!mem_c64_mode(&c->mem) && addr >= 0xFF00 && addr <= 0xFF04)
        return mmu_ffxx_read(&c->mem.mmu, addr);
    if (addr >= 0xD000 && addr < 0xE000 && mem_io_visible(&c->mem))
        return io_read(c, addr);
    return mem_dma_read(&c->mem, addr);
}

static void reu_host_write(void *ctx, u16 addr, u8 value) {
    C128 *c = ctx;
    if (!mem_c64_mode(&c->mem) && addr >= 0xFF00 && addr <= 0xFF04) {
        mmu_ffxx_write(&c->mem.mmu, addr, value);
        c128_refresh_vic_bank(c);
    } else if (addr >= 0xD000 && addr < 0xE000 && mem_io_visible(&c->mem)) {
        io_write(c, addr, value);
    } else {
        mem_dma_write(&c->mem, addr, value);
    }
}

/* --- Z80 bus (native C128 / CP/M mode). -------------------------------
 *
 * The C128 Z80 does not share the 8502's on-chip $00/$01 port and accesses
 * peripherals through its I/O address space.  At reset the 4 KiB Z80 BIOS is
 * mirrored at $0000-$0fff; it copies the common-RAM CPU handoff at $ffd0 and
 * then gives the bus to the 8502 through $d505. */
static u8 z80_mem_read(void *ctx, u16 addr) {
    C128 *c = ctx;
    Mmu *mmu = &c->mem.mmu;

    /* VICE z80mem configurations 0/1: bank zero exposes the private boot
     * BIOS in the first 4 KiB.  It is a Z80-only mirror of the $d000 ROM. */
    if (addr < 0x1000 && !(mmu->mcr & 0x40))
        return c->mem.z80bios[addr];

    /* With I/O selected, the Z80 sees colour RAM at $1000-$13ff. */
    if (addr >= 0x1000 && addr < 0x1400 && !(mmu->mcr & 0x01))
        return c->mem.color_ram[addr & 0x3ff] | 0xf0;

    if (addr >= 0xff00 && addr <= 0xff04)
        return mmu_ffxx_read(mmu, addr);

    /* Unlike the 8502, the Z80 cannot select character ROM here. */
    if (addr >= 0xd000 && addr < 0xe000 &&
        ((mmu->mcr >> 4) & 3) == 0)
        return c->mem.ram[addr];

    return mem_read(&c->mem, addr);
}

static void z80_mem_write(void *ctx, u16 addr, u8 val) {
    C128 *c = ctx;
    Mmu *mmu = &c->mem.mmu;

    if (addr >= 0xff00 && addr <= 0xff04) {
        mmu_ffxx_write(mmu, addr, val);
        c128_refresh_vic_bank(c);
        if (addr == 0xff00) reu_ff00_trigger(&c->reu);
        return;
    }
    if (addr >= 0x1000 && addr < 0x1400 && !(mmu->mcr & 0x01)) {
        c->mem.color_ram[addr & 0x3ff] = val & 0x0f;
        return;
    }
    /* Writes beneath the reset BIOS go to bank-zero $d000-$dfff RAM. */
    if (addr < 0x1000 && !(mmu->mcr & 0x40)) {
        c->mem.ram[0xd000u + addr] = val;
        return;
    }
    mem_write(&c->mem, addr, val);
}

static u8 z80_io_read(void *ctx, u16 port) {
    C128 *c = ctx;
    unsigned page = port >> 8;

    /* Ports $0000-$0fff alias bank-zero RAM at $d000-$dfff. */
    if (page <= 0x0f) {
        if (c->mem.mmu.mcr & 0x40)
            return z80_mem_read(ctx, port);
        return c->mem.ram[0xd000u | (port & 0x0fff)];
    }

    /* Z80 I/O always decodes the peripheral pages.  Only the MMU page at
     * $d5 is gated by CR bit 0 in native C128 mode. */
    if (page >= 0xd0 && page <= 0xdf &&
        (page != 0xd5 || !(c->mem.mmu.mcr & 0x01))) {
        return io_read(c, port);
    }

    /* In native mode CR bit 0 disconnects only the MMU's Z80 I/O page.
     * VICE and the hardware return the open-bus value zero here rather than
     * accidentally turning the port number into a memory read. */
    if (page == 0xd5 && (c->mem.mmu.mcr & 0x01))
        return 0;

    /* Unconnected Z80 I/O cycles fall through to the memory bus. */
    return z80_mem_read(ctx, port);
}

static void z80_io_write(void *ctx, u16 port, u8 val) {
    C128 *c = ctx;
    unsigned page = port >> 8;

    if (page <= 0x0f) {
        if (c->mem.mmu.mcr & 0x40)
            z80_mem_write(ctx, port, val);
        else
            c->mem.ram[0xd000u | (port & 0x0fff)] = val;
        return;
    }
    if (page >= 0xd0 && page <= 0xdf &&
        (page != 0xd5 || !(c->mem.mmu.mcr & 0x01))) {
        io_write(c, port, val);
        return;
    }
    if (page == 0xd5 && (c->mem.mmu.mcr & 0x01))
        return;
    z80_mem_write(ctx, port, val);
}

/* --- Instruction-boundary debugger -------------------------------------- */

C128DebugCpu c128_debug_owner(const C128 *c) {
    return mmu_cpu_is_8502(&c->mem.mmu)
        ? C128_DEBUG_CPU_8502 : C128_DEBUG_CPU_Z80;
}

u16 c128_debug_pc(const C128 *c, C128DebugCpu cpu) {
    return cpu == C128_DEBUG_CPU_Z80 ? c->z80.pc : c->cpu.pc;
}

/* Debug reads must not acknowledge CIA/VIC interrupts or advance the VDC
 * update address. Read mutable devices through a temporary copy so a full
 * 64K monitor snapshot is observational. */
static u8 debug_io_peek(C128 *c, u16 address) {
    if (address < 0xd400) {
        Vic copy = c->vic;
        return vic_read(&copy, address);
    }
    if (address < 0xd500) return sid_read(&c->sid, address);
    if (!mem_c64_mode(&c->mem) && address < 0xd510 && c->mem.mmu.mmio)
        return mmu_read(&c->mem.mmu, address);
    if (address >= 0xd600 && address < 0xd700) {
        Vdc copy = c->vdc;
        return (address & 1) ? vdc_read_data(&copy) : vdc_read_status(&copy);
    }
    if (address >= 0xd800 && address < 0xdc00) {
        unsigned bank = mem_c64_mode(&c->mem) ? 0 : c->mem.pla_data & 1;
        return c->mem.color_ram[bank * 0x400 + (address & 0x3ff)];
    }
    if (address >= 0xdc00 && address < 0xdd00) {
        Cia copy = c->cia1;
        return cia_read(&copy, address);
    }
    if (address >= 0xdd00 && address < 0xde00) {
        Cia copy = c->cia2;
        return cia_read(&copy, address);
    }
    if (address >= 0xdf00) return reu_peek(&c->reu, address);
    return 0xff;
}

u8 c128_debug_mem_read(C128 *c, C128DebugCpu cpu, u16 address) {
    if (cpu == C128_DEBUG_CPU_Z80) return z80_mem_read(c, address);
    if (address == 0) return c->cpu.io_ddr;
    if (address == 1) return c->cpu.io_port;
    if (!mem_c64_mode(&c->mem) && address >= 0xff00 && address <= 0xff04)
        return mmu_ffxx_read(&c->mem.mmu, address);
    if (address >= 0xd000 && address < 0xe000 && mem_io_visible(&c->mem))
        return debug_io_peek(c, address);
    return mem_read(&c->mem, address);
}

void c128_debug_mem_write(C128 *c, C128DebugCpu cpu, u16 address, u8 value) {
    if (cpu == C128_DEBUG_CPU_Z80)
        z80_mem_write(c, address, value);
    else
        c128_mem_write(c, address, value);
}

static void debug_set_stop(C128 *c, C128DebugStopReason reason,
                           C128DebugCpu cpu, u16 address) {
    c->paused = true;
    c->debug.step_pending = false;
    c->debug.stop_reason = reason;
    c->debug.stop_cpu = cpu;
    c->debug.stop_address = address;
}

void c128_debug_pause(C128 *c) {
    C128DebugCpu cpu = c128_debug_owner(c);
    debug_set_stop(c, C128_DEBUG_STOP_PAUSE, cpu, c128_debug_pc(c, cpu));
}

void c128_debug_continue(C128 *c) {
    C128DebugCpu cpu = c128_debug_owner(c);
    c->debug.skip_break_once = true;
    c->debug.skip_cpu = cpu;
    c->debug.skip_address = c128_debug_pc(c, cpu);
    c->debug.step_pending = false;
    c->debug.stop_reason = C128_DEBUG_STOP_NONE;
    c->paused = false;
}

bool c128_debug_step(C128 *c, C128DebugCpu cpu) {
    if (!c->paused || c128_debug_owner(c) != cpu) return false;
    c->debug.skip_break_once = true;
    c->debug.skip_cpu = cpu;
    c->debug.skip_address = c128_debug_pc(c, cpu);
    c->debug.step_cpu = cpu;
    c->debug.step_pending = true;
    c->debug.stop_reason = C128_DEBUG_STOP_NONE;
    return true;
}

unsigned c128_debug_breakpoint_add(C128 *c, C128DebugCpu cpu, u16 address) {
    for (unsigned i = 0; i < C128_DEBUG_MAX_BREAKPOINTS; ++i) {
        C128DebugBreakpoint *bp = &c->debug.breakpoints[i];
        if (bp->used && bp->cpu == cpu && bp->address == address)
            return bp->id;
    }
    for (unsigned i = 0; i < C128_DEBUG_MAX_BREAKPOINTS; ++i) {
        C128DebugBreakpoint *bp = &c->debug.breakpoints[i];
        if (bp->used) continue;
        unsigned id = ++c->debug.next_breakpoint_id;
        if (id == 0) id = ++c->debug.next_breakpoint_id;
        *bp = (C128DebugBreakpoint){
            .id = id, .cpu = cpu, .address = address,
            .enabled = true, .used = true
        };
        return id;
    }
    return 0;
}

bool c128_debug_breakpoint_remove(C128 *c, unsigned id) {
    for (unsigned i = 0; i < C128_DEBUG_MAX_BREAKPOINTS; ++i) {
        C128DebugBreakpoint *bp = &c->debug.breakpoints[i];
        if (bp->used && bp->id == id) {
            memset(bp, 0, sizeof(*bp));
            return true;
        }
    }
    return false;
}

bool c128_debug_breakpoint_enable(C128 *c, unsigned id, bool enabled) {
    for (unsigned i = 0; i < C128_DEBUG_MAX_BREAKPOINTS; ++i) {
        C128DebugBreakpoint *bp = &c->debug.breakpoints[i];
        if (bp->used && bp->id == id) {
            bp->enabled = enabled;
            return true;
        }
    }
    return false;
}

const C128DebugBreakpoint *c128_debug_breakpoint_at(const C128 *c,
                                                     unsigned slot) {
    if (slot >= C128_DEBUG_MAX_BREAKPOINTS) return NULL;
    return c->debug.breakpoints[slot].used ? &c->debug.breakpoints[slot] : NULL;
}

C128DebugStopReason c128_debug_take_stop(C128 *c, C128DebugCpu *cpu,
                                          u16 *address) {
    C128DebugStopReason reason = c->debug.stop_reason;
    if (reason == C128_DEBUG_STOP_NONE) return reason;
    if (cpu) *cpu = c->debug.stop_cpu;
    if (address) *address = c->debug.stop_address;
    c->debug.stop_reason = C128_DEBUG_STOP_NONE;
    return reason;
}

static bool debug_before_instruction(C128 *c, C128DebugCpu cpu, u16 pc) {
    if (c->debug.skip_break_once && c->debug.skip_cpu == cpu &&
        c->debug.skip_address == pc) {
        c->debug.skip_break_once = false;
        return false;
    }
    for (unsigned i = 0; i < C128_DEBUG_MAX_BREAKPOINTS; ++i) {
        const C128DebugBreakpoint *bp = &c->debug.breakpoints[i];
        if (bp->used && bp->enabled && bp->cpu == cpu && bp->address == pc) {
            debug_set_stop(c, C128_DEBUG_STOP_BREAKPOINT, cpu, pc);
            return true;
        }
    }
    return false;
}

static bool debug_after_instruction(C128 *c, C128DebugCpu cpu, u16 pc) {
    if (!c->debug.step_pending || c->debug.step_cpu != cpu) return false;
    debug_set_stop(c, C128_DEBUG_STOP_STEP, cpu, pc);
    return true;
}

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
    vdc_set_ram_size_kb(&c->vdc, cfg->vdc_ram_kb);
    cia_init(&c->cia1);
    cia_init(&c->cia2);
    sid_init(&c->sid);
    kbd_init(&c->kbd);
    joyports_reset(&c->joyports);
    tape_init(&c->tape);
    reu_configure(&c->reu, cfg->reu_enabled ? (unsigned)cfg->reu_size_kb : 0);
    reu_reset(&c->reu);
    config_normalize_drive_units(cfg);
    drive_init(&c->drive, cfg);
    drive_set_media_change_hook(&c->drive, flush_integrated_drive, c);
    drive_init(&c->drive2, cfg);
    drive_set_slot(&c->drive2, 1);
    drive_set_unit(&c->drive2, cfg->drive2_unit);
    drive_set_media_change_hook(&c->drive2, flush_second_real_drive, c);
    drive1571cr_init(&c->integrated_drive);
    drive1571cr_init(&c->second_real_drive);
    drive1581_init(&c->real1581[0]);
    drive1581_init(&c->real1581[1]);
    c->real_drive_type[0] = c->real_drive_type[1] = 1571;
    iec_bus_init(&c->iec_bus, &c->integrated_drive.via1);
    iec_bus_set_unit(&c->iec_bus, cfg->drive_unit);
    iec_bus_attach_second(&c->iec_bus, &c->second_real_drive.via1,
                          cfg->drive2_unit);
    via6522_set_port_hook(&c->integrated_drive.via1,
                          drive_via_port_change, c);
    via6522_set_port_hook(&c->second_real_drive.via1,
                          drive2_via_port_change, c);
    drive1581_set_port_hook(&c->real1581[0], drive1581_port_change, c);
    drive1581_set_port_hook(&c->real1581[1], drive2_1581_port_change, c);

    /* Reset is deferred: the host loads machine ROMs after c128_init(), and
     * the reset vector must be read from the loaded KERNAL ROM. */
}

static void c128_reset_internal(C128 *c, bool power_cycle) {
    bool caps_lock = c->kbd.caps_lock;
    mem_reset(&c->mem);
    cpu_set_stack_page(c->mem.ram + mem_cpu_page_offset(&c->mem, 1));
    cpu_reset(&c->cpu);
    z80_reset(&c->z80);
    vic_reset(&c->vic);
    vdc_reset(&c->vdc);
    cia_reset(&c->cia1);
    cia_reset(&c->cia2);
    sid_reset(&c->sid);
    c->audio_count = 0;
    c->peripheral_fast_remainder = 0;
    c->z80_peripheral_remainder = 0;
    kbd_reset(&c->kbd);
    reu_reset(&c->reu);
    c->kbd.caps_lock = caps_lock; /* physical locking switch survives RESET */
    c->restore_down = false;
    joyports_reset(&c->joyports);
    tape_set_motor(&c->tape, false);
    drive_reset(&c->drive);
    drive_reset(&c->drive2);
    if (power_cycle) {
        drive1571cr_power_cycle(&c->integrated_drive);
        drive1571cr_power_cycle(&c->second_real_drive);
        drive1581_power_cycle(&c->real1581[0]);
        drive1581_power_cycle(&c->real1581[1]);
    } else {
        drive1571cr_reset(&c->integrated_drive);
        drive1571cr_reset(&c->second_real_drive);
        drive1581_reset(&c->real1581[0]);
        drive1581_reset(&c->real1581[1]);
    }
    drive_monitor_reset(&c->drive_monitor);
    drive_monitor_reset(&c->drive2_monitor);
    iec_bus_reset(&c->iec_bus);
    iec_bus_set_host(&c->iec_bus, c->cia2.pra, c->cia2.ddra);
    c->drive_clock_fraction = 0;
    c->drive2_clock_fraction = 0;
    c->drive_clock_denominator = 0;
    c->drive_host_cycle_synced = 0;
    c->drive_media_generation = (unsigned)-1;
    c->drive2_media_generation = (unsigned)-1;
    drive_set_unit(&c->drive2, c->cfg->drive2_unit);
    c->paused = false;
    c->debug.step_pending = false;
    c->debug.skip_break_once = false;
    c->debug.stop_reason = C128_DEBUG_STOP_NONE;
    c->debug.partial_frame = false;
    c->frames_since_reset = 0;
    c->cpu_frame_debt = 0;
    c->z80_frame_debt = 0;
    c->bus_cycles = 0;
    c->cpu_clock_active = false;
    c->cpu_clock_synced = cpu_cycles();
    leds_set_cpu_frequency(1);
    leds_set_z80_frequency(c->cfg && c->cfg->double_z80_frequency ? 4 : 2);
    /* Preserve the 40/80 column choice across resets. */
    c->mem.mmu.col4080 = !c->col_mode_80;
    display_set_vdc_active(&c->display, c->col_mode_80);
}

void c128_reset(C128 *c) {
    c128_reset_internal(c, false);
}

void c128_power_cycle(C128 *c) {
    /* Unlike RESET, removing power destroys both banks of system RAM and
     * VDC RAM. ROMs, inserted media, configuration, and the physical 40/80
     * key remain in place. */
    mem_power_cycle(&c->mem);
    vdc_powerup(&c->vdc);
    reu_power_cycle(&c->reu);
    c->cpu.io_ddr = 0;
    c->cpu.io_port = 0;
    c->total_cycles = 0;
    c128_reset_internal(c, true);
}

bool c128_set_reu(C128 *c, bool enabled, unsigned size_kb) {
    if (!size_kb || !reu_valid_size(size_kb)) return false;
    reu_configure(&c->reu, enabled ? size_kb : 0);
    c->cfg->reu_enabled = enabled;
    c->cfg->reu_size_kb = (int)size_kb;
    c128_refresh_interrupt_lines(c);
    return true;
}

/* Advance the shared clock independently of the 8502 clock. CPU reads,
 * writes, RDY stalls and 1/2 MHz changes all meet here, so a raster write
 * cannot be applied to the beginning of an already scanned line. */
static void c128_clock_halves(C128 *c, unsigned halves) {
    while (halves--) {
        vic_clock_half(&c->vic, &c->mem);
        if (c->vic.beam_half_clock & 1) continue;
        ++c->bus_cycles;
        if (c->vic.cycle == 0)
            vdc_set_raster_line(&c->vdc, c->vic.current_raster);
        cia_tick(&c->cia1, 1);
        cia_tick(&c->cia2, 1);
        tape_advance(&c->tape, 1, tape_read_pulse, &c->cia1);
        int produced = sid_clock(&c->sid, 1,
            c->audio_frame + c->audio_count,
            C128_AUDIO_FRAME_CAPACITY - c->audio_count);
        tape_mix_audio(&c->tape, c->audio_frame + c->audio_count,
                       produced, c->cfg->tape_audio_monitor);
        c->audio_count += produced;
    }
    drive_sync_to_cpu(c);
    c128_refresh_interrupt_lines(c);
}

static void c128_sync_cpu(C128 *c) {
    if (!c->cpu_clock_active) return;
    u64 now = cpu_cycles();
    /* The inherited core clock is 32 bits. Handle wrap, but not its short
     * backwards adjustment while replacing a KERNAL trap instruction. */
    u32 elapsed = (u32)(now - c->cpu_clock_synced);
    if (!elapsed || elapsed > 0x80000000u) return;
    bool fast = c->fast || c->vic.fast_mode;
    c->cpu_clock_synced = now;
    while (elapsed--) {
        /* Five refresh accesses occupy PHI1 even at 2 MHz. */
        if (fast && !(c->vic.beam_half_clock & 1) &&
            c->vic.cycle >= 11 && c->vic.cycle <= 15) {
            cpu_stall(1);
            c->cpu_clock_synced = (u32)(c->cpu_clock_synced + 1);
            c128_clock_halves(c, 1);
        }
        c128_clock_halves(c, fast ? 1 : 2);
    }
}

static void c128_cpu_bus_wait(C128 *c, bool write, bool io) {
    if (!c->cpu_clock_active) return;
    c128_sync_cpu(c);
    bool fast = c->fast || c->vic.fast_mode;
    /* The core timestamps a read at its start, but STORE_ABS includes the
     * write's final clock. VICE likewise subtracts one half-cycle before
     * stretching a write and adds it back afterward. */
    if (fast && io && (c->vic.beam_half_clock & 1) == (unsigned)write) {
        cpu_stall(1);
        c128_sync_cpu(c);
    }
    /* RDY stops read cycles, not writes: in particular RMW's two bus
     * writes must be allowed to finish when BA falls. */
    while (!write && !fast && vic_cpu_ba_low(&c->vic)) {
        cpu_stall(1);
        c128_sync_cpu(c);
    }
}

static void real_drive_media(C128 *c, unsigned slot) {
    Drive *media = slot ? &c->drive2 : &c->drive;
    Drive1571Cr *gcr = slot ? &c->second_real_drive : &c->integrated_drive;
    unsigned *generation = slot ? &c->drive2_media_generation : &c->drive_media_generation;
    if (*generation == media->media_generation) return;
    DiskImage *image = media->disk_attached ? &media->image : NULL;
    if (c->real_drive_type[slot] == 1581) {
        if (!wd1770_attach(&c->real1581[slot].fdc, image)) {
            wd1770_attach(&c->real1581[slot].fdc, NULL);
            if (slot ? c->drive2_raw_iec : c->drive_raw_iec) {
                notify_post("1581 DRIVE %u NEEDS A D81 IMAGE", slot + 1);
                fprintf(stderr, "1986: drive %u: 1581 requires an ordinary D81; hardware has no disk\n", slot + 1);
            }
        }
    } else {
        gcr_drive_attach(&gcr->gcr, image);
        gcr_drive_update_via(&gcr->gcr, &gcr->via2);
        if (image && !gcr->gcr.image &&
            (slot ? c->drive2_raw_iec : c->drive_raw_iec))
            notify_post("1571 DRIVE %u NEEDS A D64 OR D71 IMAGE", slot + 1);
    }
    *generation = media->media_generation;
}

static bool real_drive_monitor(C128 *c, unsigned slot) {
    DriveMonitor *monitor = slot ? &c->drive2_monitor : &c->drive_monitor;
    if (c->real_drive_type[slot] == 1581) {
        Drive1581 *d = &c->real1581[slot];
        return drive_monitor_update(monitor, d->fdc.motor, d->led,
            2 * (d->fdc.head_track + 1), (unsigned)d->fdc.head_steps,
            (unsigned)d->fdc.read_bytes, (unsigned)d->fdc.write_bytes);
    }
    GcrDrive *g = slot ? &c->second_real_drive.gcr : &c->integrated_drive.gcr;
    return drive_monitor_update(monitor, g->motor, g->led, g->half_track,
                                 g->step_events, g->read_events, g->write_events);
}

int c128_frame(C128 *c) {
    if (c->paused && !c->debug.step_pending) return 0;
    bool resuming_frame = c->debug.partial_frame;
    c->tape.frame_edges = 0;
    real_drive_media(c, 0);
    real_drive_media(c, 1);
    /* The raster clock, not a per-line CPU budget, determines the end of a
     * frame. BA stalls and D030 changes can now occur inside an instruction. */
    c->drive_clock_denominator = CPU_PAL_FRAME_CYCLES;
    c->drive_host_cycle_synced = c->bus_cycles;
    c->cpu_clock_synced = cpu_cycles();
    int total = 0;
    bool ran_8502 = false, ran_z80 = false, debug_halted = false;
    if (!resuming_frame) c->audio_count = 0;
    c128_refresh_vic_bank(c);
    u64 frame_end = (c->vic.beam_half_clock / (126 * VIC_RASTER_LINES) + 1) *
                    (126 * VIC_RASTER_LINES);
    while (c->vic.beam_half_clock < frame_end) {
        if (c->reu.active) {
            /* DMA owns the shared 1 MHz bus, independently of CPU speed.
             * Do not retire CPU instructions; keep VIC/VDC, CIAs, audio and
             * drives running, yielding to the frontend at each frame. */
            if (c->vic.beam_half_clock & 1) c128_clock_halves(c, 1);
            ReuBus bus = {c, reu_host_read, reu_host_write};
            if (!vic_cpu_ba_low(&c->vic)) reu_tick(&c->reu, &bus);
            if (mmu_cpu_is_8502(&c->mem.mmu)) {
                unsigned clocks = c->fast || c->vic.fast_mode ? 2 : 1;
                cpu_stall(clocks);
                c->cpu.cycles += clocks;
            }
            c->cpu_clock_synced = cpu_cycles();
            c128_clock_halves(c, 2);
            ++total;
            continue;
        }
        bool z80 = !mmu_cpu_is_8502(&c->mem.mmu);
        C128DebugCpu owner = z80 ? C128_DEBUG_CPU_Z80 : C128_DEBUG_CPU_8502;
        u16 pc = z80 ? c->z80.pc : c->cpu.pc;
        if (debug_before_instruction(c, owner, pc)) {
            debug_halted = true;
            break;
        }
        c128_refresh_interrupt_lines(c);
        if (z80) {
            int ran = z80_step(&c->z80, &c->z80_bus);
            if (ran <= 0) break;
            unsigned ratio = c->cfg->double_z80_frequency ? 2 : 1;
            unsigned halves = ran + c->z80_peripheral_remainder;
            c->z80_peripheral_remainder = halves % ratio;
            c128_clock_halves(c, halves / ratio);
            total += ran / (2 * ratio);
            ran_z80 = true;
            c->cpu_clock_synced = cpu_cycles();
        } else {
            c->cpu.fast = c->fast || c->vic.fast_mode;
            c->cpu_clock_active = true;
            int ran = cpu_step_budget(&c->cpu, 1);
            c128_sync_cpu(c);
            c->cpu_clock_active = false;
            if (ran <= 0) break;
            u16 jam_pc;
            if (cpu_take_jam(&jam_pc)) {
                fprintf(stderr, "1986: 8502 JAM at $%04X; resetting machine\n", jam_pc);
                notify_post("8502 JAM AT $%04X - MACHINE RESET", jam_pc);
                c128_reset(c);
                return total;
            }
            total += ran;
            ran_8502 = true;
        }
        if (debug_after_instruction(c, owner, z80 ? c->z80.pc : c->cpu.pc)) {
            debug_halted = true;
            break;
        }
    }
    c->debug.partial_frame = debug_halted;
    c->debug.partial_video_line = c->vic.current_raster;
    c->cpu_frame_debt = c->z80_frame_debt = 0;
    leds_set_cpu_frequency((c->fast || c->vic.fast_mode) ? 2 : 1);
    if (ran_8502)
        leds_ping(LED_CPU_8502);
    if (ran_z80)
        leds_ping(LED_CPU_Z80);
    /* The 6526 TOD input follows the PAL 50 Hz mains signal, not the 8502
     * clock (which may run at 2 MHz). One completed PAL frame is one pulse. */
    if (!debug_halted) {
        cia_tod_tick(&c->cia1);
        cia_tod_tick(&c->cia2);
    }
    c->total_cycles += (u64)total;
    if (c->drive_raw_iec && real_drive_monitor(c, 0))
        leds_ping(LED_FDC_A);
    if (c->drive2_raw_iec && real_drive_monitor(c, 1))
        leds_ping(LED_FDC_B);
    for (unsigned slot = 0; slot < 2; ++slot) {
        Wd1770 *fdc = &c->real1581[slot].fdc;
        if ((slot ? c->drive2_raw_iec : c->drive_raw_iec) &&
            c->real_drive_type[slot] == 1581 &&
            fdc->write_error != DISK_SAVE_OK && !fdc->write_error_reported) {
            notify_post(fdc->write_error == DISK_SAVE_WRITE_PROTECT
                ? "1581 DRIVE %u DISK IS WRITE PROTECTED"
                : "1581 DRIVE %u WRITE COULD NOT BE SAVED", slot + 1);
            fprintf(stderr, "1986: drive %u: 1581 write failed (error %d)\n",
                    slot + 1, (int)fdc->write_error);
            fdc->write_error_reported = true;
        }
    }
    GcrDrive *gcr = &c->integrated_drive.gcr;
    if (c->drive_raw_iec && gcr->write_error != DISK_SAVE_OK &&
        !gcr->write_error_reported) {
        notify_post(gcr->write_error == DISK_SAVE_WRITE_PROTECT
                    ? "1571 DISK IS WRITE PROTECTED"
                    : "1571 WRITE COULD NOT BE SAVED");
        fprintf(stderr, "1986: 1571 GCR write not saved (error %d)\n",
                (int)gcr->write_error);
        gcr->write_error_reported = true;
    }
    GcrDrive *gcr2 = &c->second_real_drive.gcr;
    if (c->drive2_raw_iec && gcr2->write_error != DISK_SAVE_OK &&
        !gcr2->write_error_reported) {
        notify_post(gcr2->write_error == DISK_SAVE_WRITE_PROTECT
                    ? "1571 DRIVE 2 DISK IS WRITE PROTECTED"
                    : "1571 DRIVE 2 WRITE COULD NOT BE SAVED");
        fprintf(stderr, "1986: drive 2 GCR write not saved (error %d)\n",
                (int)gcr2->write_error);
        gcr2->write_error_reported = true;
    }
    if (!debug_halted) {
        c128_frame_count++;
        c->frames_since_reset++;
    }
    if (c->tape.kind == TAPE_TAP && getenv("C128_TAPE_TRACE") &&
        c128_frame_count % 100 == 0)
        fprintf(stderr, "[tape] frame=%d play=%d motor=%d pos=%zu/%zu edges=%u pc=$%04x\n",
                c128_frame_count, c->tape.play_button, c->tape.motor_on,
                c->tape.position, c->tape.payload_end,
                c->tape.frame_edges, c->cpu.pc);
    if (drive_probe_active(c) && c->real_drive_type[0] != 1581 && getenv("C128_1571_TRACE") &&
        c->frames_since_reset % 50 == 0) {
        fprintf(stderr, "[1571] frame=%d pc=$%04x cycles=%llu via1=$%02x/$%02x pcr=$%02x ifr=$%02x ier=$%02x ca1=%d irq=%d CIA2=$%02x/$%02x IEC=%d%d%d host=%u drive=%u lines=%u ram79=$%02x ram7a=$%02x ram83=$%02x ram84=$%02x GCR=m%d led%d s%u h%u z%u p%u $%02x sync%d R%u W%u%s\n",
                c->frames_since_reset, c->integrated_drive.cpu.pc,
                (unsigned long long)c->integrated_drive.cpu.cycles,
                c->integrated_drive.via1.ora, c->integrated_drive.via1.orb,
                c->integrated_drive.via1.pcr,
                c->integrated_drive.via1.ifr,
                c->integrated_drive.via1.ier,
                c->integrated_drive.via1.ca1,
                c->integrated_drive.cpu.irq,
                c->cia2.pra, c->cia2.ddra,
                c->iec_bus.atn_high, c->iec_bus.clock_high,
                c->iec_bus.data_high,
                c->iec_bus.host_changes, c->iec_bus.drive_changes,
                c->iec_bus.line_changes,
                c->integrated_drive.ram[0x79],
                c->integrated_drive.ram[0x7a],
                c->integrated_drive.ram[0x83],
                c->integrated_drive.ram[0x84],
                c->integrated_drive.gcr.motor,
                c->integrated_drive.gcr.led,
                c->integrated_drive.gcr.side,
                c->integrated_drive.gcr.half_track,
                c->integrated_drive.gcr.zone,
                c->integrated_drive.gcr.byte_pos,
                c->integrated_drive.gcr.read_byte,
                c->integrated_drive.gcr.sync,
                c->integrated_drive.gcr.read_events,
                c->integrated_drive.gcr.write_events,
                c->integrated_drive.cpu.jammed ? " JAMMED" : "");
    }

    /* Render both video devices. The latched physical 40/80 key selects the
     * visible output; $00D7 is a KERNAL software flag and can disagree with
     * it (notably when a cartridge draws to VIC while BASIC is in 80-col). */
    vic_render(&c->vic, &c->mem, &c->display);
    vdc_render(&c->vdc, c->display.vdc_pixels, VDC_SCREEN_W, VDC_SCREEN_H);
    display_set_vdc_active(&c->display,
                           mem_c64_mode(&c->mem) ? false : !c->mem.mmu.col4080);
    return total;
}

u64 c128_cycles_to_ns(const C128 *c, int cycles) {
    (void)c;
    (void)cycles;
    return ((u64)CPU_PAL_FRAME_CYCLES * 1000000000ULL) / 1000000ULL;
}

bool c128_set_c64_test_mode(C128 *c, bool enabled) {
    if (enabled && !mem_c64_roms_loaded(&c->mem)) return false;
    bool active = mmu_is_c64_mode(&c->mem.mmu);
    mmu_set_c64_enabled(&c->mem.mmu, enabled);
    if (!enabled && active) c128_reset(c);
    return true;
}

bool c128_is_c64_mode(const C128 *c) {
    return mmu_is_c64_mode(&c->mem.mmu);
}

void c128_key_event(C128 *c, int scancode, bool down) {
    if (scancode == SDL_SCANCODE_PAGEUP) {
        c->restore_down = down;
        return;
    }
    if (scancode == SDL_SCANCODE_CAPSLOCK) {
        if (down && !c->kbd.caps_key_down)
            c->kbd.caps_lock = !c->kbd.caps_lock;
        c->kbd.caps_key_down = down;
        return;
    }
    int row, col;
    bool shift;
    if (!kbd_map_scancode(scancode, &row, &col, &shift)) return;
    /* Some positional mappings, such as even-numbered function keys, require
     * a virtual C128 Shift alongside their matrix position. */
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
