#include "drive_cpu6502.h"

/* Shared instruction-level NMOS core, extracted from the 1571CR. Peripherals
 * and CPU speed remain the responsibility of each drive board. */
enum { C = 1, Z = 2, I = 4, D = 8, B = 16, U = 32, V = 64, N = 128 };
typedef enum { IMM, ZP, ZPX, ZPY, ABS, ABSX, ABSY, INDX, INDY } AddrMode;

static u8 bus_read(DriveCpu6502 *d, u16 addr) {
    return d->read(d->ctx, addr);
}

static void bus_write(DriveCpu6502 *d, u16 addr, u8 value) {
    d->write(d->ctx, addr, value);
}

static u16 read16(DriveCpu6502 *d, u16 addr) {
    u8 lo = bus_read(d, addr);
    return (u16)(lo | ((u16)bus_read(d, (u16)(addr + 1)) << 8));
}

void drive_cpu6502_reset(DriveCpu6502 *c, DriveCpu6502Read read,
                         DriveCpu6502Write write, void *ctx) {
    c->read = read;
    c->write = write;
    c->ctx = ctx;
    c->a = c->x = c->y = 0;
    c->sp = 0xfd;
    c->p = I | U;
    c->pc = read16(c, 0xfffc);
    c->cycles = 0;
    c->irq = c->nmi_pending = c->jammed = false;
}

static void flag(DriveCpu6502 *c, u8 mask, bool set) {
    if (set) c->p |= mask;
    else c->p &= (u8)~mask;
}

static void nz(DriveCpu6502 *c, u8 value) {
    flag(c, Z, value == 0);
    flag(c, N, (value & 0x80) != 0);
}

static u8 fetch(DriveCpu6502 *d) {
    return bus_read(d, d->pc++);
}

static u16 fetch16(DriveCpu6502 *d) {
    u8 lo = fetch(d), hi = fetch(d);
    return (u16)(lo | ((u16)hi << 8));
}

static u16 addr(DriveCpu6502 *d, AddrMode mode, bool *crossed) {
    DriveCpu6502 *c = d;
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
            return (u16)(bus_read(d, base) |
                ((u16)bus_read(d, (u8)(base + 1)) << 8));
        case INDY:
            base = fetch(d);
            base = (u16)(bus_read(d, base) |
                ((u16)bus_read(d, (u8)(base + 1)) << 8));
            *crossed = ((base & 0xff) + c->y) > 0xff;
            return (u16)(base + c->y);
    }
    return 0;
}

static void push(DriveCpu6502 *d, u8 value) {
    bus_write(d, (u16)(0x100 | d->sp--), value);
}

static u8 pull(DriveCpu6502 *d) {
    return bus_read(d, (u16)(0x100 | ++d->sp));
}

static void interrupt(DriveCpu6502 *d, u16 vector, bool brk) {
    DriveCpu6502 *c = d;
    push(d, (u8)(c->pc >> 8));
    push(d, (u8)c->pc);
    push(d, (u8)((c->p | U) | (brk ? B : 0)));
    c->p = (u8)((c->p | I | U) & ~B);
    c->pc = read16(d, vector);
}

static void compare(DriveCpu6502 *c, u8 left, u8 right) {
    unsigned result = (unsigned)left - right;
    flag(c, C, left >= right);
    nz(c, (u8)result);
}

static void adc(DriveCpu6502 *c, u8 value) {
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

static void sbc(DriveCpu6502 *c, u8 value) {
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

static int step_group1(DriveCpu6502 *d, u8 op) {
    DriveCpu6502 *c = d;
    int operation = op >> 5;
    int mode_index = (op >> 2) & 7;
    static const AddrMode modes[8] = { INDX, ZP, IMM, ABS, INDY, ZPX, ABSY, ABSX };
    static const int read_cycles[8] = { 6, 3, 2, 4, 5, 4, 4, 4 };
    static const int write_cycles[8] = { 6, 3, 0, 4, 6, 4, 5, 5 };
    if (operation == 4 && mode_index == 2) return 0; /* $89 is illegal */
    bool crossed;
    u16 at = addr(d, modes[mode_index], &crossed);
    if (operation == 4) {
        bus_write(d, at, c->a);
        return write_cycles[mode_index];
    }
    u8 value = bus_read(d, at);
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

static int shift(DriveCpu6502 *d, u8 op, AddrMode mode, int cycles) {
    DriveCpu6502 *c = d;
    bool crossed;
    bool accumulator = op == 0x0a || op == 0x2a || op == 0x4a || op == 0x6a;
    u16 at = accumulator ? 0 : addr(d, mode, &crossed);
    u8 old = accumulator ? c->a : bus_read(d, at);
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
        bus_write(d, at, old); /* NMOS read-modify-write dummy write */
        bus_write(d, at, result);
    }
    return cycles;
}

int drive_cpu6502_step(DriveCpu6502 *d) {
    DriveCpu6502 *c = d;
    if (c->jammed) return 0;
    if (c->nmi_pending || (c->irq && !(c->p & I))) {
        bool nmi = c->nmi_pending;
        c->nmi_pending = false;
        interrupt(d, nmi ? 0xfffa : 0xfffe, false);
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
                   c->pc = (u16)(bus_read(d, at) |
                       ((u16)bus_read(d, (u16)((at & 0xff00) | (u8)(at + 1))) << 8));
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
            value = bus_read(d, at);
            flag(c, Z, !(c->a & value));
            flag(c, V, value & 0x40); flag(c, N, value & 0x80);
            cycles = op == 0x24 ? 3 : 4; break;

        case 0x84: case 0x94: case 0x8c: /* STY */
        case 0x86: case 0x96: case 0x8e: /* STX */
            at = addr(d, (op & 0x0f) == 0x0c || (op & 0x0f) == 0x0e ? ABS :
                (op & 0x10) ? ((op & 2) ? ZPY : ZPX) : ZP, &crossed);
            bus_write(d, at, (op & 2) ? c->x : c->y);
            cycles = (op & 0x0f) >= 0x0c ? 4 : (op & 0x10) ? 4 : 3;
            break;
        case 0xa0: case 0xa4: case 0xb4: case 0xac: case 0xbc: /* LDY */
        case 0xa2: case 0xa6: case 0xb6: case 0xae: case 0xbe: /* LDX */
            at = addr(d, !(op & 0x1c) ? IMM :
                (op & 0x1c) == 0x04 ? ZP :
                (op & 0x1c) == 0x0c ? ABS :
                (op & 0x1c) == 0x14 ? ((op & 2) ? ZPY : ZPX) :
                ((op & 2) ? ABSY : ABSX), &crossed);
            value = bus_read(d, at);
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
            compare(c, (op & 0x20) ? c->x : c->y, bus_read(d, at));
            cycles = (op & 0x0f) == 0 ? 2 : (op & 0x0f) == 4 ? 3 : 4;
            break;

        case 0xc6: case 0xd6: case 0xce: case 0xde: /* DEC */
        case 0xe6: case 0xf6: case 0xee: case 0xfe: /* INC */
            at = addr(d, (op & 0x0f) == 6 ?
                ((op & 0x10) ? ZPX : ZP) :
                ((op & 0x10) ? ABSX : ABS), &crossed);
            value = bus_read(d, at);
            bus_write(d, at, value); /* NMOS RMW dummy write */
            value = (u8)(value + ((op & 0x20) ? 1 : -1));
            bus_write(d, at, value); nz(c, value);
            cycles = (op & 0x0f) == 6 ? ((op & 0x10) ? 6 : 5) :
                     ((op & 0x10) ? 7 : 6);
            break;
        default:
            c->jammed = true; /* Illegal opcode: never silently treat as NOP. */
            return 0;
    }
    if (!cycles) { c->jammed = true; return 0; }
    c->cycles += (u64)cycles;
    return cycles;
}
