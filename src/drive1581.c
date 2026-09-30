#include "drive1581.h"
#include <stdio.h>
#include <string.h>

static u8 cpu_read(void *ctx, u16 addr) { return drive1581_read(ctx, addr); }
static void cpu_write(void *ctx, u16 addr, u8 value) {
    drive1581_write(ctx, addr, value);
}

u8 drive1581_port_b(const Drive1581 *d) {
    return (u8)(d->cia.prb | ~d->cia.ddrb);
}

bool drive1581_clock_released(const Drive1581 *d) {
    return !(drive1581_port_b(d) & 0x08);
}

bool drive1581_data_released(const Drive1581 *d) {
    u8 pins = drive1581_port_b(d);
    /* 1581 ATN gate differs from the 1571's XOR acknowledgement circuit.
     * See VICE's cia1581d.c store_ciapb / iecbus.c drive-port calculation. */
    return !(pins & 0x02) && (!(pins & 0x10) || d->atn_high);
}

static void port_a_changed(Drive1581 *d) {
    u8 pins = (u8)(d->cia.pra | ~d->cia.ddra);
    wd1770_set_side(&d->fdc, (pins & 1) ? 0 : 1);
    wd1770_set_motor(&d->fdc, !(pins & 4));
    d->led = (pins & 0x40) != 0;
}

static void port_b_changed(Drive1581 *d) {
    if (d->port_hook) d->port_hook(d->port_ctx, drive1581_port_b(d));
}

void drive1581_init(Drive1581 *d) {
    memset(d, 0, sizeof(*d));
    cia_init(&d->cia);
    wd1770_init(&d->fdc);
    d->unit = 8;
    d->atn_high = d->clock_high = d->data_high = true;
    d->led = true;
}

bool drive1581_load_rom(Drive1581 *d, const char *path) {
    if (!path) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    u8 image[sizeof(d->rom)];
    size_t count = fread(image, 1, sizeof(image), file);
    int extra = fgetc(file), error = ferror(file);
    fclose(file);
    if (count != sizeof(image) || extra != EOF || error) return false;
    memcpy(d->rom, image, sizeof(image));
    d->rom_loaded = true;
    return true;
}

void drive1581_reset(Drive1581 *d) {
    cia_reset(&d->cia);
    wd1770_reset(&d->fdc);
    d->clock_debt = 0;
    port_a_changed(d);
    port_b_changed(d);
    drive_cpu6502_reset(&d->cpu, cpu_read, cpu_write, d);
    cia_set_flag(&d->cia, d->atn_high);
    d->cpu.irq = cia_irq_line(&d->cia);
}

void drive1581_power_cycle(Drive1581 *d) {
    memset(d->ram, 0, sizeof(d->ram));
    drive1581_reset(d);
}

bool drive1581_set_unit(Drive1581 *d, unsigned unit) {
    if (unit < 8 || unit > 11) return false;
    d->unit = unit;
    return true;
}

void drive1581_set_port_hook(Drive1581 *d, Drive1581PortHook hook, void *ctx) {
    d->port_hook = hook;
    d->port_ctx = ctx;
    port_b_changed(d);
}

void drive1581_set_iec(Drive1581 *d, bool atn, bool clock, bool data) {
    d->atn_high = atn;
    d->clock_high = clock;
    d->data_high = data;
    cia_set_flag(&d->cia, atn);
    d->cpu.irq = cia_irq_line(&d->cia);
}

u8 drive1581_read(Drive1581 *d, u16 addr) {
    if (addr < 0x2000) return d->ram[addr];
    if (addr < 0x4000) return 0xff; /* not an 8 KiB RAM mirror */
    if (addr < 0x6000) {
        unsigned reg = addr & 15;
        if (reg == 0) {
            u8 input = (u8)((d->unit - 8) << 3);
            if (!d->fdc.disk_changed) input |= 0x80;
            return (u8)((d->cia.pra & d->cia.ddra) | (input & ~d->cia.ddra));
        }
        if (reg == 1) {
            u8 input = (d->data_high ? 0 : 1) | (d->clock_high ? 0 : 4) |
                       (d->atn_high ? 0 : 0x80);
            if (!wd1770_write_protected(&d->fdc)) input |= 0x40;
            return (u8)((d->cia.prb & d->cia.ddrb) | (input & ~d->cia.ddrb));
        }
        u8 value = cia_read(&d->cia, addr);
        d->cpu.irq = cia_irq_line(&d->cia);
        return value;
    }
    if (addr < 0x8000) return wd1770_read(&d->fdc, addr);
    return d->rom_loaded ? d->rom[addr - 0x8000] : 0xff;
}

void drive1581_write(Drive1581 *d, u16 addr, u8 value) {
    if (addr < 0x2000) { d->ram[addr] = value; return; }
    if (addr >= 0x4000 && addr < 0x6000) {
        cia_write(&d->cia, addr, value);
        unsigned reg = addr & 15;
        if (reg == 0 || reg == 2) port_a_changed(d);
        if (reg == 1 || reg == 3) port_b_changed(d);
        d->cpu.irq = cia_irq_line(&d->cia);
    } else if (addr >= 0x6000 && addr < 0x8000) wd1770_write(&d->fdc, addr, value);
}

int drive1581_step(Drive1581 *d) {
    if (!d->rom_loaded) return 0;
    int cycles = drive_cpu6502_step(&d->cpu);
    if (cycles) {
        cia_tick(&d->cia, cycles);
        wd1770_tick(&d->fdc, (unsigned)cycles);
        /* DOS polls the WD1770 status/DRQ; its IRQ is not the CPU's CIA IRQ. */
        d->cpu.irq = cia_irq_line(&d->cia);
    }
    return cycles;
}

int drive1581_advance(Drive1581 *d, int cycles) {
    if (cycles <= 0 || !d->rom_loaded || d->cpu.jammed) return 0;
    int budget = cycles - d->clock_debt, used = 0;
    while (used < budget) {
        int n = drive1581_step(d);
        if (!n) break;
        used += n;
    }
    d->clock_debt = d->cpu.jammed ? 0 : used - budget;
    return used;
}
