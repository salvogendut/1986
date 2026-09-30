#include "drive1571cr.h"
#include <stdio.h>
#include <string.h>

static void via2_port_change(void *ctx, unsigned port, u8 pins) {
    Drive1571Cr *d = ctx;
    if (port == 1) gcr_drive_set_port_b(&d->gcr, pins);
}

void drive1571cr_init(Drive1571Cr *d) {
    memset(d, 0, sizeof(*d));
    via6522_init(&d->via1);
    via6522_init(&d->via2);
    cia_init(&d->mos5710);
    gcr_drive_init(&d->gcr);
    via6522_set_port_hook(&d->via2, via2_port_change, d);
}

bool drive1571cr_load_rom(Drive1571Cr *d, const char *path) {
    if (!path) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    u8 image[sizeof(d->rom)];
    size_t count = fread(image, 1, sizeof(image), file);
    int extra = fgetc(file);
    int error = ferror(file);
    fclose(file);
    if (count != sizeof(image) || extra != EOF || error) return false;
    memcpy(d->rom, image, sizeof(image));
    d->rom_loaded = true;
    return true;
}

void drive1571cr_set_io(Drive1571Cr *d, Drive1571CrIoRead read,
                        Drive1571CrIoWrite write, void *ctx) {
    d->io_read = read;
    d->io_write = write;
    d->io_ctx = ctx;
}

static bool decode_io(u16 addr, Drive1571CrIo *chip) {
    if (addr >= 0x1000 && addr < 0x2000) {
        *chip = (addr & 0x0400) ? DRIVE1571CR_VIA2 : DRIVE1571CR_VIA1;
    } else if (addr >= 0x2000 && addr < 0x3000) {
        *chip = DRIVE1571CR_FDC;
    } else if (addr >= 0x4000 && addr < 0x8000) {
        *chip = DRIVE1571CR_MOS5710;
    } else return false;
    return true;
}

static void update_irq(Drive1571Cr *d) {
    d->cpu.irq = d->external_irq || via6522_irq(&d->via1) ||
                 via6522_irq(&d->via2) || cia_irq_line(&d->mos5710);
}

static void update_via1_mechanism(Drive1571Cr *d) {
    /* 1571CR PA0 is the active-low track-zero sensor; PA7 reports the
     * inverse of the byte-ready level. The DOS ROM homes the head using PA0. */
    via6522_set_input_a(&d->via1,
        (u8)((d->gcr.byte_ready ? 0 : 0x80) |
             (d->gcr.half_track == 2 ? 0 : 1)));
}

static u8 mos5710_read(Drive1571Cr *d, u16 addr) {
    u8 reg = addr & 0x1f;
    if (reg >= 0x10)
        return d->io_read ? d->io_read(d->io_ctx, DRIVE1571CR_MOS5710, addr) : 0;
    if (reg == 0x0c || reg == 0x0d || reg == 0x0e) {
        u8 value = cia_read(&d->mos5710, reg);
        update_irq(d);
        return value;
    }
    return 0xff;
}

static void mos5710_write(Drive1571Cr *d, u16 addr, u8 value) {
    u8 reg = addr & 0x1f;
    if (reg >= 0x10) {
        if (d->io_write) d->io_write(d->io_ctx, DRIVE1571CR_MOS5710, addr, value);
        return;
    }
    if (reg == 0x0d && (value & 0x80)) value &= 0x88; /* SDR IRQ only */
    if (reg == 0x0e) value = (value & 0x40) | 0x01;
    if (reg == 0x0c || reg == 0x0d || reg == 0x0e) {
        cia_write(&d->mos5710, reg, value);
        update_irq(d);
    }
}

u8 drive1571cr_read(Drive1571Cr *d, u16 addr) {
    if (addr < 0x1000) return d->ram[addr & 0x07ff];
    if (addr >= 0x8000) return d->rom_loaded ? d->rom[addr - 0x8000] : 0xff;
    Drive1571CrIo chip;
    if (decode_io(addr, &chip)) {
        if (chip == DRIVE1571CR_VIA1 || chip == DRIVE1571CR_VIA2) {
            if (chip == DRIVE1571CR_VIA2)
                gcr_drive_update_via(&d->gcr, &d->via2);
            else
                update_via1_mechanism(d);
            u8 value = via6522_read(chip == DRIVE1571CR_VIA1 ? &d->via1 :
                                    &d->via2, addr);
            if (chip == DRIVE1571CR_VIA2 &&
                ((addr & 15) == 0 || (addr & 15) == 1 || (addr & 15) == 15))
                gcr_drive_read_byte(&d->gcr);
            update_irq(d);
            return value;
        }
        if (chip == DRIVE1571CR_MOS5710) return mos5710_read(d, addr);
        if (d->io_read) return d->io_read(d->io_ctx, chip, addr);
    }
    return 0xff;
}

void drive1571cr_write(Drive1571Cr *d, u16 addr, u8 value) {
    if (addr < 0x1000) {
        d->ram[addr & 0x07ff] = value;
        return;
    }
    if (addr >= 0x8000) return;
    Drive1571CrIo chip;
    if (decode_io(addr, &chip)) {
        if (chip == DRIVE1571CR_VIA1 || chip == DRIVE1571CR_VIA2) {
            via6522_write(chip == DRIVE1571CR_VIA1 ? &d->via1 : &d->via2,
                          addr, value);
            if (chip == DRIVE1571CR_VIA1 &&
                ((addr & 15) == 1 || (addr & 15) == 3 || (addr & 15) == 15)) {
                d->clock_2mhz = (via6522_output_a(&d->via1) & 0x20) != 0;
                gcr_drive_set_side(&d->gcr,
                    (via6522_output_a(&d->via1) & 0x04) != 0);
            }
            if (chip == DRIVE1571CR_VIA2) {
                if ((addr & 15) == 1 || (addr & 15) == 15)
                    gcr_drive_write_byte(&d->gcr, value);
                if ((addr & 15) == 12)
                    gcr_drive_set_write_mode(&d->gcr,
                        (d->via2.pcr & 0x20) == 0);
                gcr_drive_update_via(&d->gcr, &d->via2);
            }
            update_irq(d);
        } else if (chip == DRIVE1571CR_MOS5710) mos5710_write(d, addr, value);
        else if (d->io_write) d->io_write(d->io_ctx, chip, addr, value);
    }
}

static u8 cpu_read(void *ctx, u16 addr) {
    return drive1571cr_read(ctx, addr);
}

static void cpu_write(void *ctx, u16 addr, u8 value) {
    drive1571cr_write(ctx, addr, value);
}

void drive1571cr_reset(Drive1571Cr *d) {
    via6522_reset(&d->via1);
    via6522_reset(&d->via2);
    cia_reset(&d->mos5710);
    gcr_drive_reset(&d->gcr);
    gcr_drive_update_via(&d->gcr, &d->via2);
    update_via1_mechanism(d);
    d->external_irq = false;
    d->clock_debt = 0;
    d->clock_2mhz = false;
    drive_cpu6502_reset(&d->cpu, cpu_read, cpu_write, d);
}

void drive1571cr_power_cycle(Drive1571Cr *d) {
    memset(d->ram, 0, sizeof(d->ram));
    drive1571cr_reset(d);
}

void drive1571cr_irq(Drive1571Cr *d, bool level) {
    d->external_irq = level;
    update_irq(d);
}
void drive1571cr_nmi(Drive1571Cr *d) { d->cpu.nmi_pending = true; }

int drive1571cr_step(Drive1571Cr *d) {
    if (!d->rom_loaded) return 0;
    int cycles = drive_cpu6502_step(&d->cpu);
    if (cycles) {
        via6522_tick(&d->via1, (unsigned)cycles);
        via6522_tick(&d->via2, (unsigned)cycles);
        cia_tick(&d->mos5710, cycles);
        if (gcr_drive_tick(&d->gcr, &d->via2, (unsigned)cycles,
                           d->clock_2mhz)) d->cpu.p |= 0x40; /* SO -> V */
        update_irq(d);
    }
    return cycles;
}

int drive1571cr_run(Drive1571Cr *d, int cycle_budget) {
    int elapsed = 0;
    while (elapsed < cycle_budget) {
        int cycles = drive1571cr_step(d);
        if (!cycles) break;
        elapsed += cycles;
    }
    return elapsed;
}

int drive1571cr_advance(Drive1571Cr *d, int cycle_budget) {
    if (cycle_budget <= 0 || !d->rom_loaded || d->cpu.jammed) return 0;
    int remaining = cycle_budget - d->clock_debt;
    if (remaining <= 0) {
        d->clock_debt = -remaining;
        return 0;
    }
    int elapsed = drive1571cr_run(d, remaining);
    d->clock_debt = elapsed ? elapsed - remaining : 0;
    return elapsed;
}
