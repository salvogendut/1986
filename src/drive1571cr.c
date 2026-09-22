#include "drive1571cr.h"
#include <stdio.h>
#include <string.h>

enum { C = 1, Z = 2, I = 4, D = 8, B = 16, U = 32, V = 64, N = 128 };
typedef enum { IMM, ZP, ZPX, ZPY, ABS, ABSX, ABSY, INDX, INDY } AddrMode;

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

static u16 read16(Drive1571Cr *d, u16 addr) {
    u8 lo = drive1571cr_read(d, addr);
    return (u16)(lo | ((u16)drive1571cr_read(d, (u16)(addr + 1)) << 8));
}

void drive1571cr_reset(Drive1571Cr *d) {
    Drive1571CrCpu *cpu = &d->cpu;
    via6522_reset(&d->via1);
    via6522_reset(&d->via2);
    cia_reset(&d->mos5710);
    gcr_drive_reset(&d->gcr);
    gcr_drive_update_via(&d->gcr, &d->via2);
    update_via1_mechanism(d);
    d->external_irq = false;
    d->clock_debt = 0;
    d->clock_2mhz = false;
    cpu->a = cpu->x = cpu->y = 0;
    cpu->sp = 0xfd;
    cpu->p = I | U;
    cpu->pc = read16(d, 0xfffc);
    cpu->cycles = 0;
    cpu->irq = cpu->nmi_pending = cpu->jammed = false;
}

void drive1571cr_irq(Drive1571Cr *d, bool level) {
    d->external_irq = level;
    update_irq(d);
}
void drive1571cr_nmi(Drive1571Cr *d) { d->cpu.nmi_pending = true; }

static void flag(Drive1571CrCpu *c, u8 mask, bool set) {
    if (set) c->p |= mask;
    else c->p &= (u8)~mask;
}

static void nz(Drive1571CrCpu *c, u8 value) {
    flag(c, Z, value == 0);
    flag(c, N, (value & 0x80) != 0);
}

static u8 fetch(Drive1571Cr *d) {
    return drive1571cr_read(d, d->cpu.pc++);
}

static u16 fetch16(Drive1571Cr *d) {
    u8 lo = fetch(d), hi = fetch(d);
    return (u16)(lo | ((u16)hi << 8));
}

static u16 addr(Drive1571Cr *d, AddrMode mode, bool *crossed) {
    Drive1571CrCpu *c = &d->cpu;
    u16 base;
    *crossed = false;
    switch (mode) {
        case IMM: return c->pc++;
        case ZP: return fetch(d);
        case ZPX: return (u8)(fetch(d) + c->x);
        case ZPY: return (u8)(fetch(d) + c->y);
        case ABS: return fetch16(d);
        case ABSX:
            base = fetch16(d);
            *crossed = ((base & 0xff) + c->x) > 0xff;
            return (u16)(base + c->x);
        case ABSY:
            base = fetch16(d);
            *crossed = ((base & 0xff) + c->y) > 0xff;
            return (u16)(base + c->y);
        case INDX:
            base = (u8)(fetch(d) + c->x);
            return (u16)(drive1571cr_read(d, base) |
                ((u16)drive1571cr_read(d, (u8)(base + 1)) << 8));
        case INDY:
            base = fetch(d);
            base = (u16)(drive1571cr_read(d, base) |
                ((u16)drive1571cr_read(d, (u8)(base + 1)) << 8));
            *crossed = ((base & 0xff) + c->y) > 0xff;
            return (u16)(base + c->y);
    }
    return 0;
}

static void push(Drive1571Cr *d, u8 value) {
    drive1571cr_write(d, (u16)(0x100 | d->cpu.sp--), value);
}

static u8 pull(Drive1571Cr *d) {
    return drive1571cr_read(d, (u16)(0x100 | ++d->cpu.sp));
}

static void interrupt(Drive1571Cr *d, u16 vector, bool brk) {
    Drive1571CrCpu *c = &d->cpu;
    push(d, (u8)(c->pc >> 8));
    push(d, (u8)c->pc);
    push(d, (u8)((c->p | U) | (brk ? B : 0)));
    c->p = (u8)((c->p | I | U) & ~B);
    c->pc = read16(d, vector);
}

static void compare(Drive1571CrCpu *c, u8 left, u8 right) {
    unsigned result = (unsigned)left - right;
    flag(c, C, left >= right);
    nz(c, (u8)result);
}

static void adc(Drive1571CrCpu *c, u8 value) {
    unsigned carry = !!(c->p & C);
    unsigned binary = c->a + value + carry;
    flag(c, V, (~(c->a ^ value) & (c->a ^ binary) & 0x80) != 0);
    /* NMOS 6502 takes N/Z/V from the binary operation even in decimal mode. */
    nz(c, (u8)binary);
    if (c->p & D) {
        unsigned lo = (c->a & 15) + (value & 15) + carry;
        unsigned hi = (c->a >> 4) + (value >> 4);
        if (lo > 9) { lo += 6; hi++; }
        if (hi > 9) hi += 6;
        flag(c, C, hi > 15);
        c->a = (u8)(((unsigned)(hi & 15) << 4) | (lo & 15));
    } else {
        flag(c, C, binary > 255);
        c->a = (u8)binary;
    }
}

static void sbc(Drive1571CrCpu *c, u8 value) {
    unsigned borrow = !(c->p & C);
    int binary = (int)c->a - value - (int)borrow;
    flag(c, V, ((c->a ^ value) & (c->a ^ binary) & 0x80) != 0);
    nz(c, (u8)binary);
    flag(c, C, binary >= 0);
    if (c->p & D) {
        int lo = (c->a & 15) - (value & 15) - (int)borrow;
        int hi = (c->a >> 4) - (value >> 4);
        if (lo < 0) { lo -= 6; hi--; }
        if (hi < 0) hi -= 6;
        c->a = (u8)((hi << 4) | (lo & 15));
    } else c->a = (u8)binary;
}

static int step_group1(Drive1571Cr *d, u8 op) {
    Drive1571CrCpu *c = &d->cpu;
    int operation = op >> 5;
    int mode_index = (op >> 2) & 7;
    static const AddrMode modes[8] = { INDX, ZP, IMM, ABS, INDY, ZPX, ABSY, ABSX };
    static const int read_cycles[8] = { 6, 3, 2, 4, 5, 4, 4, 4 };
    static const int write_cycles[8] = { 6, 3, 0, 4, 6, 4, 5, 5 };
    if (operation == 4 && mode_index == 2) return 0; /* $89 is illegal */
    bool crossed;
    u16 at = addr(d, modes[mode_index], &crossed);
    if (operation == 4) {
        drive1571cr_write(d, at, c->a);
        return write_cycles[mode_index];
    }
    u8 value = drive1571cr_read(d, at);
    switch (operation) {
        case 0: c->a |= value; nz(c, c->a); break; /* ORA */
        case 1: c->a &= value; nz(c, c->a); break; /* AND */
        case 2: c->a ^= value; nz(c, c->a); break; /* EOR */
        case 3: adc(c, value); break;
        case 5: c->a = value; nz(c, c->a); break; /* LDA */
        case 6: compare(c, c->a, value); break;
        case 7: sbc(c, value); break;
    }
    return read_cycles[mode_index] +
        ((mode_index == 4 || mode_index == 6 || mode_index == 7) && crossed);
}

static int shift(Drive1571Cr *d, u8 op, AddrMode mode, int cycles) {
    Drive1571CrCpu *c = &d->cpu;
    bool crossed;
    bool accumulator = op == 0x0a || op == 0x2a || op == 0x4a || op == 0x6a;
    u16 at = accumulator ? 0 : addr(d, mode, &crossed);
    u8 old = accumulator ? c->a : drive1571cr_read(d, at);
    u8 result;
    switch ((op >> 5) & 3) {
        case 0: flag(c, C, old & 0x80); result = (u8)(old << 1); break; /* ASL */
        case 1: result = (u8)((old << 1) | !!(c->p & C));
                flag(c, C, old & 0x80); break; /* ROL */
        case 2: flag(c, C, old & 1); result = old >> 1; break; /* LSR */
        default: result = (u8)((old >> 1) | ((c->p & C) ? 0x80 : 0));
                 flag(c, C, old & 1); break; /* ROR */
    }
    nz(c, result);
    if (accumulator) c->a = result;
    else {
        drive1571cr_write(d, at, old); /* NMOS read-modify-write dummy write */
        drive1571cr_write(d, at, result);
    }
    return cycles;
}

int drive1571cr_step(Drive1571Cr *d) {
    Drive1571CrCpu *c = &d->cpu;
    if (!d->rom_loaded || c->jammed) return 0;
    if (c->nmi_pending || (c->irq && !(c->p & I))) {
        bool nmi = c->nmi_pending;
        c->nmi_pending = false;
        interrupt(d, nmi ? 0xfffa : 0xfffe, false);
        via6522_tick(&d->via1, 7);
        via6522_tick(&d->via2, 7);
        cia_tick(&d->mos5710, 7);
        if (gcr_drive_tick(&d->gcr, &d->via2, 7, d->clock_2mhz)) c->p |= V;
        update_irq(d);
        c->cycles += 7;
        return 7;
    }
    u8 op = fetch(d);
    int cycles = 0;
    bool crossed;
    u16 at;
    u8 value;
    if ((op & 3) == 1) cycles = step_group1(d, op);
    else if ((op & 0x1f) == 0x10) {
        static const u8 masks[8] = { N, N, V, V, C, C, Z, Z };
        int which = op >> 5;
        bool condition = !!(c->p & masks[which]);
        int8_t offset = (int8_t)fetch(d);
        cycles = 2;
        if (condition == !!(which & 1)) {
            u16 old = c->pc;
            c->pc = (u16)(c->pc + offset);
            cycles += 1 + ((old & 0xff00) != (c->pc & 0xff00));
        }
    } else switch (op) {
        case 0x00: c->pc++; interrupt(d, 0xfffe, true); cycles = 7; break;
        case 0x20: at = fetch16(d); /* JSR */
                   push(d, (u8)((c->pc - 1) >> 8));
                   push(d, (u8)(c->pc - 1)); c->pc = at; cycles = 6; break;
        case 0x40: c->p = (u8)((pull(d) & ~B) | U);
                   c->pc = pull(d); c->pc |= (u16)pull(d) << 8; cycles = 6; break;
        case 0x60: c->pc = pull(d); c->pc |= (u16)pull(d) << 8;
                   c->pc++; cycles = 6; break;
        case 0x4c: c->pc = fetch16(d); cycles = 3; break;
        case 0x6c: at = fetch16(d);
                   c->pc = (u16)(drive1571cr_read(d, at) |
                       ((u16)drive1571cr_read(d, (u16)((at & 0xff00) | (u8)(at + 1))) << 8));
                   cycles = 5; break;
        case 0x08: push(d, c->p | B | U); cycles = 3; break;
        case 0x28: c->p = (u8)((pull(d) & ~B) | U); cycles = 4; break;
        case 0x48: push(d, c->a); cycles = 3; break;
        case 0x68: c->a = pull(d); nz(c, c->a); cycles = 4; break;
        case 0x18: flag(c, C, false); cycles = 2; break;
        case 0x38: flag(c, C, true); cycles = 2; break;
        case 0x58: flag(c, I, false); cycles = 2; break;
        case 0x78: flag(c, I, true); cycles = 2; break;
        case 0xb8: flag(c, V, false); cycles = 2; break;
        case 0xd8: flag(c, D, false); cycles = 2; break;
        case 0xf8: flag(c, D, true); cycles = 2; break;
        case 0xea: cycles = 2; break;
        case 0x88: c->y--; nz(c, c->y); cycles = 2; break;
        case 0xc8: c->y++; nz(c, c->y); cycles = 2; break;
        case 0xca: c->x--; nz(c, c->x); cycles = 2; break;
        case 0xe8: c->x++; nz(c, c->x); cycles = 2; break;
        case 0x8a: c->a = c->x; nz(c, c->a); cycles = 2; break;
        case 0x98: c->a = c->y; nz(c, c->a); cycles = 2; break;
        case 0x9a: c->sp = c->x; cycles = 2; break;
        case 0xaa: c->x = c->a; nz(c, c->x); cycles = 2; break;
        case 0xa8: c->y = c->a; nz(c, c->y); cycles = 2; break;
        case 0xba: c->x = c->sp; nz(c, c->x); cycles = 2; break;

        case 0x0a: case 0x2a: case 0x4a: case 0x6a:
            cycles = shift(d, op, IMM, 2); break;
        case 0x06: case 0x26: case 0x46: case 0x66:
            cycles = shift(d, op, ZP, 5); break;
        case 0x16: case 0x36: case 0x56: case 0x76:
            cycles = shift(d, op, ZPX, 6); break;
        case 0x0e: case 0x2e: case 0x4e: case 0x6e:
            cycles = shift(d, op, ABS, 6); break;
        case 0x1e: case 0x3e: case 0x5e: case 0x7e:
            cycles = shift(d, op, ABSX, 7); break;

        case 0x24: case 0x2c:
            at = addr(d, op == 0x24 ? ZP : ABS, &crossed);
            value = drive1571cr_read(d, at);
            flag(c, Z, !(c->a & value));
            flag(c, V, value & 0x40); flag(c, N, value & 0x80);
            cycles = op == 0x24 ? 3 : 4; break;

        case 0x84: case 0x94: case 0x8c: /* STY */
        case 0x86: case 0x96: case 0x8e: /* STX */
            at = addr(d, (op & 0x0f) == 0x0c || (op & 0x0f) == 0x0e ? ABS :
                (op & 0x10) ? ((op & 2) ? ZPY : ZPX) : ZP, &crossed);
            drive1571cr_write(d, at, (op & 2) ? c->x : c->y);
            cycles = (op & 0x0f) >= 0x0c ? 4 : (op & 0x10) ? 4 : 3;
            break;
        case 0xa0: case 0xa4: case 0xb4: case 0xac: case 0xbc: /* LDY */
        case 0xa2: case 0xa6: case 0xb6: case 0xae: case 0xbe: /* LDX */
            at = addr(d, !(op & 0x1c) ? IMM :
                (op & 0x1c) == 0x04 ? ZP :
                (op & 0x1c) == 0x0c ? ABS :
                (op & 0x1c) == 0x14 ? ((op & 2) ? ZPY : ZPX) :
                ((op & 2) ? ABSY : ABSX), &crossed);
            value = drive1571cr_read(d, at);
            if (op & 2) { c->x = value; nz(c, c->x); }
            else { c->y = value; nz(c, c->y); }
            cycles = (op & 0x1c) == 0 ? 2 :
                     (op & 0x1c) == 0x04 ? 3 :
                     (op & 0x1c) == 0x0c ? 4 :
                     (op & 0x1c) == 0x14 ? 4 : 4 + crossed;
            break;

        case 0xc0: case 0xc4: case 0xcc: /* CPY */
        case 0xe0: case 0xe4: case 0xec: /* CPX */
            at = addr(d, (op & 0x0f) == 0 ? IMM :
                (op & 0x0f) == 4 ? ZP : ABS, &crossed);
            compare(c, (op & 0x20) ? c->x : c->y, drive1571cr_read(d, at));
            cycles = (op & 0x0f) == 0 ? 2 : (op & 0x0f) == 4 ? 3 : 4;
            break;

        case 0xc6: case 0xd6: case 0xce: case 0xde: /* DEC */
        case 0xe6: case 0xf6: case 0xee: case 0xfe: /* INC */
            at = addr(d, (op & 0x0f) == 6 ?
                ((op & 0x10) ? ZPX : ZP) :
                ((op & 0x10) ? ABSX : ABS), &crossed);
            value = drive1571cr_read(d, at);
            drive1571cr_write(d, at, value); /* NMOS RMW dummy write */
            value = (u8)(value + ((op & 0x20) ? 1 : -1));
            drive1571cr_write(d, at, value); nz(c, value);
            cycles = (op & 0x0f) == 6 ? ((op & 0x10) ? 6 : 5) :
                     ((op & 0x10) ? 7 : 6);
            break;
        default:
            c->jammed = true; /* Illegal opcode: never silently treat as NOP. */
            return 0;
    }
    if (!cycles) { c->jammed = true; return 0; }
    via6522_tick(&d->via1, (unsigned)cycles);
    via6522_tick(&d->via2, (unsigned)cycles);
    cia_tick(&d->mos5710, cycles);
    if (gcr_drive_tick(&d->gcr, &d->via2, (unsigned)cycles,
                       d->clock_2mhz)) c->p |= V;
    update_irq(d);
    c->cycles += (u64)cycles;
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
