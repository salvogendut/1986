#include "cpu.h"
#include <stdbool.h>
#include <string.h>

/* Convenience wrappers over the bus callbacks. */
static inline u8  mem_rd(Cpu8502 *c, u16 a) { return c->bus.read(c->bus.ctx, a); }
static inline void mem_wr(Cpu8502 *c, u16 a, u8 v) { c->bus.write(c->bus.ctx, a, v); }

static inline void set_flag(Cpu8502 *c, u8 mask, bool on) {
    if (on) c->p |= mask; else c->p &= (u8)~mask;
}
static inline bool flag(Cpu8502 *c, u8 mask) { return (c->p & mask) != 0; }

static void push(Cpu8502 *c, u8 v) { mem_wr(c, 0x0100 | c->sp, v); c->sp--; }
static u8   pull(Cpu8502 *c) { c->sp++; return mem_rd(c, 0x0100 | c->sp); }

static u8 fetch(Cpu8502 *c) { return mem_rd(c, c->pc++); }

static u16 read16(Cpu8502 *c, u16 a) {
    u8 lo = mem_rd(c, a);
    u8 hi = mem_rd(c, (u16)(a + 1));
    return (u16)(lo | (hi << 8));
}

/* Push and pull 16-bit values (high byte first). */
static void push16(Cpu8502 *c, u16 v) { push(c, (u8)(v >> 8)); push(c, (u8)(v & 0xFF)); }
static u16  pull16(Cpu8502 *c) { u8 lo = pull(c); u8 hi = pull(c); return (u16)(lo | (hi << 8)); }

/* NZ flags from a value. */
static void nz(Cpu8502 *c, u8 v) {
    set_flag(c, P_Z, v == 0);
    set_flag(c, P_N, (v & 0x80) != 0);
}

/* --- Addressing modes: return effective address (and, for indirect ones,
 * read the operand). Relative operands are handled by branch helpers. --- */

static u16 addr_zp(Cpu8502 *c)   { return fetch(c); }
static u16 addr_zpx(Cpu8502 *c)  { return (u16)((u8)(fetch(c) + c->x)); }
static u16 addr_zpy(Cpu8502 *c)  { return (u16)((u8)(fetch(c) + c->y)); }
static u16 addr_abs(Cpu8502 *c)  { return read16(c, c->pc); }
static u16 addr_absx(Cpu8502 *c) { u16 b = read16(c, c->pc); return (u16)(b + c->x); }
static u16 addr_absy(Cpu8502 *c) { u16 b = read16(c, c->pc); return (u16)(b + c->y); }
static u16 addr_ind(Cpu8502 *c)  { return read16(c, read16(c, c->pc)); } /* JMP (abs) */

/* (zp,X) then (zp),Y — indirect zero page. */
static u16 addr_izx(Cpu8502 *c) {
    u8 z = (u8)(fetch(c) + c->x);
    u16 p = read16(c, z);
    return p;
}
static u16 addr_izy(Cpu8502 *c) {
    u8 z = fetch(c);
    u16 p = read16(c, z);
    return (u16)(p + c->y);
}

static u8 op_and(Cpu8502 *c, u8 v) { c->a &= v; nz(c, c->a); return 0; }
static u8 op_ora(Cpu8502 *c, u8 v) { c->a |= v; nz(c, c->a); return 0; }
static u8 op_eor(Cpu8502 *c, u8 v) { c->a ^= v; nz(c, c->a); return 0; }
static u8 op_cmp(Cpu8502 *c, u8 v) { u8 d = c->a - v; set_flag(c, P_C, c->a >= v); nz(c, d); return 0; }
static u8 op_cpx(Cpu8502 *c, u8 v) { u8 d = c->x - v; set_flag(c, P_C, c->x >= v); nz(c, d); return 0; }
static u8 op_cpy(Cpu8502 *c, u8 v) { u8 d = c->y - v; set_flag(c, P_C, c->y >= v); nz(c, d); return 0; }

static u8 op_adc(Cpu8502 *c, u8 v) {
    unsigned sum = c->a + v + (flag(c, P_C) ? 1u : 0u);
    u8 r = (u8)sum;
    set_flag(c, P_V, ((c->a ^ r) & (v ^ r) & 0x80) != 0);
    set_flag(c, P_C, sum > 0xFF);
    nz(c, r);
    c->a = r;
    return 0;
}
static u8 op_sbc(Cpu8502 *c, u8 v) {
    unsigned diff = c->a - v - (flag(c, P_C) ? 0u : 1u);
    u8 r = (u8)diff;
    set_flag(c, P_V, ((c->a ^ r) & (~v ^ r) & 0x80) != 0);
    set_flag(c, P_C, c->a >= (unsigned)v + (flag(c, P_C) ? 0u : 1u));
    nz(c, r);
    c->a = r;
    return 0;
}

/* Read-modify-write helpers. */
static u8 op_asl(Cpu8502 *c, u8 v) { set_flag(c, P_C, (v & 0x80) != 0); u8 r = (u8)(v << 1); nz(c, r); return r; }
static u8 op_lsr(Cpu8502 *c, u8 v) { set_flag(c, P_C, (v & 0x01) != 0); u8 r = (u8)(v >> 1); nz(c, r); return r; }
static u8 op_rol(Cpu8502 *c, u8 v) { u8 r = (u8)((v << 1) | (flag(c, P_C) ? 1 : 0)); set_flag(c, P_C, (v & 0x80) != 0); nz(c, r); return r; }
static u8 op_ror(Cpu8502 *c, u8 v) { u8 r = (u8)((v >> 1) | (flag(c, P_C) ? 0x80 : 0)); set_flag(c, P_C, (v & 0x01) != 0); nz(c, r); return r; }
static u8 op_inc(Cpu8502 *c, u8 v) { u8 r = (u8)(v + 1); nz(c, r); return r; }
static u8 op_dec(Cpu8502 *c, u8 v) { u8 r = (u8)(v - 1); nz(c, r); return r; }

/* Branch helper: return true if taken, consume the 8-bit offset. */
static bool branch(Cpu8502 *c, bool cond) {
    s8 off = (s8)fetch(c);
    if (cond) {
        u16 target = (u16)(c->pc + off);
        /* Page-cross penalty is folded in by the caller via a +1/+2. */
        (void)target;
        c->pc = target;
        return true;
    }
    return false;
}

static int rmv(Cpu8502 *c, u16 a, u8 (*f)(Cpu8502 *, u8)) {
    u8 v = mem_rd(c, a);
    u8 r = f(c, v);
    mem_wr(c, a, r);
    return 0;
}

int cpu_step(Cpu8502 *c) {
    u64 cycles_before = c->cycles;

    /* Interrupt handling: NMI (edge) then IRQ (level) unless I is set. */
    if (c->nmi_pending) {
        c->nmi_pending = false;
        push16(c, c->pc);
        push(c, c->p & (u8)~P_B);
        set_flag(c, P_I, true);
        c->pc = mem_rd(c, 0xFFFA) | (mem_rd(c, 0xFFFB) << 8);
        c->cycles += 7;
        return 7;
    }
    if (c->irq_level && !flag(c, P_I)) {
        push16(c, c->pc);
        push(c, c->p & (u8)~P_B);
        set_flag(c, P_I, true);
        c->pc = mem_rd(c, 0xFFFE) | (mem_rd(c, 0xFFFF) << 8);
        c->cycles += 7;
        return 7;
    }

    u8 op = fetch(c);

    switch (op) {
        /* --- Loads / stores --- */
        case 0xA9: c->a = fetch(c); nz(c, c->a); c->cycles += 2; break;               /* LDA # */
        case 0xA5: c->a = mem_rd(c, addr_zp(c)); nz(c, c->a); c->cycles += 3; break;  /* LDA zp */
        case 0xB5: c->a = mem_rd(c, addr_zpx(c)); nz(c, c->a); c->cycles += 4; break; /* LDA zp,x */
        case 0xAD: c->a = mem_rd(c, addr_abs(c)); nz(c, c->a); c->cycles += 4; break; /* LDA abs */
        case 0xBD: c->a = mem_rd(c, addr_absx(c)); nz(c, c->a); c->cycles += 4; break;/* LDA abs,x */
        case 0xB9: c->a = mem_rd(c, addr_absy(c)); nz(c, c->a); c->cycles += 4; break;/* LDA abs,y */
        case 0xA1: c->a = mem_rd(c, addr_izx(c)); nz(c, c->a); c->cycles += 6; break; /* LDA (zp,x) */
        case 0xB1: c->a = mem_rd(c, addr_izy(c)); nz(c, c->a); c->cycles += 5; break; /* LDA (zp),y */

        case 0xA2: c->x = fetch(c); nz(c, c->x); c->cycles += 2; break;               /* LDX # */
        case 0xA6: c->x = mem_rd(c, addr_zp(c)); nz(c, c->x); c->cycles += 3; break;  /* LDX zp */
        case 0xB6: c->x = mem_rd(c, addr_zpy(c)); nz(c, c->x); c->cycles += 4; break; /* LDX zp,y */
        case 0xAE: c->x = mem_rd(c, addr_abs(c)); nz(c, c->x); c->cycles += 4; break; /* LDX abs */
        case 0xBE: c->x = mem_rd(c, addr_absy(c)); nz(c, c->x); c->cycles += 4; break;/* LDX abs,y */

        case 0xA0: c->y = fetch(c); nz(c, c->y); c->cycles += 2; break;               /* LDY # */
        case 0xA4: c->y = mem_rd(c, addr_zp(c)); nz(c, c->y); c->cycles += 3; break;  /* LDY zp */
        case 0xB4: c->y = mem_rd(c, addr_zpx(c)); nz(c, c->y); c->cycles += 4; break; /* LDY zp,x */
        case 0xAC: c->y = mem_rd(c, addr_abs(c)); nz(c, c->y); c->cycles += 4; break; /* LDY abs */
        case 0xBC: c->y = mem_rd(c, addr_absx(c)); nz(c, c->y); c->cycles += 4; break;/* LDY abs,x */

        case 0x85: mem_wr(c, addr_zp(c), c->a); c->cycles += 3; break;                /* STA zp */
        case 0x95: mem_wr(c, addr_zpx(c), c->a); c->cycles += 4; break;               /* STA zp,x */
        case 0x8D: mem_wr(c, addr_abs(c), c->a); c->cycles += 4; break;               /* STA abs */
        case 0x9D: mem_wr(c, addr_absx(c), c->a); c->cycles += 5; break;              /* STA abs,x */
        case 0x99: mem_wr(c, addr_absy(c), c->a); c->cycles += 5; break;              /* STA abs,y */
        case 0x81: mem_wr(c, addr_izx(c), c->a); c->cycles += 6; break;               /* STA (zp,x) */
        case 0x91: mem_wr(c, addr_izy(c), c->a); c->cycles += 6; break;               /* STA (zp),y */

        case 0x86: mem_wr(c, addr_zp(c), c->x); c->cycles += 3; break;                /* STX zp */
        case 0x96: mem_wr(c, addr_zpy(c), c->x); c->cycles += 4; break;               /* STX zp,y */
        case 0x8E: mem_wr(c, addr_abs(c), c->x); c->cycles += 4; break;               /* STX abs */

        case 0x84: mem_wr(c, addr_zp(c), c->y); c->cycles += 3; break;                /* STY zp */
        case 0x94: mem_wr(c, addr_zpx(c), c->y); c->cycles += 4; break;               /* STY zp,x */
        case 0x8C: mem_wr(c, addr_abs(c), c->y); c->cycles += 4; break;               /* STY abs */

        /* --- Transfers --- */
        case 0xAA: c->x = c->a; nz(c, c->x); c->cycles += 2; break;                   /* TAX */
        case 0xA8: c->y = c->a; nz(c, c->y); c->cycles += 2; break;                   /* TAY */
        case 0x8A: c->a = c->x; nz(c, c->a); c->cycles += 2; break;                   /* TXA */
        case 0x98: c->a = c->y; nz(c, c->a); c->cycles += 2; break;                   /* TYA */
        case 0xBA: c->x = c->sp; nz(c, c->x); c->cycles += 2; break;                  /* TSX */
        case 0x9A: c->sp = c->x; c->cycles += 2; break;                               /* TXS */

        /* --- Stack --- */
        case 0x48: push(c, c->a); c->cycles += 3; break;                              /* PHA */
        case 0x68: c->a = pull(c); nz(c, c->a); c->cycles += 4; break;                /* PLA */
        case 0x08: push(c, c->p | P_B | 0x20); c->cycles += 3; break;                 /* PHP */
        case 0x28: c->p = pull(c); c->p &= (u8)~(P_B | 0x20); c->cycles += 4; break;  /* PLP */

        /* --- Logic / arithmetic --- */
        case 0x29: op_and(c, fetch(c)); c->cycles += 2; break;                        /* AND # */
        case 0x25: op_and(c, mem_rd(c, addr_zp(c))); c->cycles += 3; break;           /* AND zp */
        case 0x35: op_and(c, mem_rd(c, addr_zpx(c))); c->cycles += 4; break;          /* AND zp,x */
        case 0x2D: op_and(c, mem_rd(c, addr_abs(c))); c->cycles += 4; break;          /* AND abs */
        case 0x3D: op_and(c, mem_rd(c, addr_absx(c))); c->cycles += 4; break;         /* AND abs,x */
        case 0x39: op_and(c, mem_rd(c, addr_absy(c))); c->cycles += 4; break;         /* AND abs,y */
        case 0x21: op_and(c, mem_rd(c, addr_izx(c))); c->cycles += 6; break;          /* AND (zp,x) */
        case 0x31: op_and(c, mem_rd(c, addr_izy(c))); c->cycles += 5; break;          /* AND (zp),y */

        case 0x09: op_ora(c, fetch(c)); c->cycles += 2; break;                        /* ORA # */
        case 0x05: op_ora(c, mem_rd(c, addr_zp(c))); c->cycles += 3; break;           /* ORA zp */
        case 0x15: op_ora(c, mem_rd(c, addr_zpx(c))); c->cycles += 4; break;          /* ORA zp,x */
        case 0x0D: op_ora(c, mem_rd(c, addr_abs(c))); c->cycles += 4; break;          /* ORA abs */
        case 0x1D: op_ora(c, mem_rd(c, addr_absx(c))); c->cycles += 4; break;         /* ORA abs,x */
        case 0x19: op_ora(c, mem_rd(c, addr_absy(c))); c->cycles += 4; break;         /* ORA abs,y */
        case 0x01: op_ora(c, mem_rd(c, addr_izx(c))); c->cycles += 6; break;          /* ORA (zp,x) */
        case 0x11: op_ora(c, mem_rd(c, addr_izy(c))); c->cycles += 5; break;          /* ORA (zp),y */

        case 0x49: op_eor(c, fetch(c)); c->cycles += 2; break;                        /* EOR # */
        case 0x45: op_eor(c, mem_rd(c, addr_zp(c))); c->cycles += 3; break;           /* EOR zp */
        case 0x55: op_eor(c, mem_rd(c, addr_zpx(c))); c->cycles += 4; break;          /* EOR zp,x */
        case 0x4D: op_eor(c, mem_rd(c, addr_abs(c))); c->cycles += 4; break;          /* EOR abs */
        case 0x5D: op_eor(c, mem_rd(c, addr_absx(c))); c->cycles += 4; break;         /* EOR abs,x */
        case 0x59: op_eor(c, mem_rd(c, addr_absy(c))); c->cycles += 4; break;         /* EOR abs,y */
        case 0x41: op_eor(c, mem_rd(c, addr_izx(c))); c->cycles += 6; break;          /* EOR (zp,x) */
        case 0x51: op_eor(c, mem_rd(c, addr_izy(c))); c->cycles += 5; break;          /* EOR (zp),y */

        case 0x69: op_adc(c, fetch(c)); c->cycles += 2; break;                        /* ADC # */
        case 0x65: op_adc(c, mem_rd(c, addr_zp(c))); c->cycles += 3; break;           /* ADC zp */
        case 0x75: op_adc(c, mem_rd(c, addr_zpx(c))); c->cycles += 4; break;          /* ADC zp,x */
        case 0x6D: op_adc(c, mem_rd(c, addr_abs(c))); c->cycles += 4; break;          /* ADC abs */
        case 0x7D: op_adc(c, mem_rd(c, addr_absx(c))); c->cycles += 4; break;         /* ADC abs,x */
        case 0x79: op_adc(c, mem_rd(c, addr_absy(c))); c->cycles += 4; break;         /* ADC abs,y */
        case 0x61: op_adc(c, mem_rd(c, addr_izx(c))); c->cycles += 6; break;          /* ADC (zp,x) */
        case 0x71: op_adc(c, mem_rd(c, addr_izy(c))); c->cycles += 5; break;          /* ADC (zp),y */

        case 0xE9: op_sbc(c, fetch(c)); c->cycles += 2; break;                        /* SBC # */
        case 0xE5: op_sbc(c, mem_rd(c, addr_zp(c))); c->cycles += 3; break;           /* SBC zp */
        case 0xF5: op_sbc(c, mem_rd(c, addr_zpx(c))); c->cycles += 4; break;          /* SBC zp,x */
        case 0xED: op_sbc(c, mem_rd(c, addr_abs(c))); c->cycles += 4; break;          /* SBC abs */
        case 0xFD: op_sbc(c, mem_rd(c, addr_absx(c))); c->cycles += 4; break;         /* SBC abs,x */
        case 0xF9: op_sbc(c, mem_rd(c, addr_absy(c))); c->cycles += 4; break;         /* SBC abs,y */
        case 0xE1: op_sbc(c, mem_rd(c, addr_izx(c))); c->cycles += 6; break;          /* SBC (zp,x) */
        case 0xF1: op_sbc(c, mem_rd(c, addr_izy(c))); c->cycles += 5; break;          /* SBC (zp),y */

        case 0xC9: op_cmp(c, fetch(c)); c->cycles += 2; break;                        /* CMP # */
        case 0xC5: op_cmp(c, mem_rd(c, addr_zp(c))); c->cycles += 3; break;           /* CMP zp */
        case 0xD5: op_cmp(c, mem_rd(c, addr_zpx(c))); c->cycles += 4; break;          /* CMP zp,x */
        case 0xCD: op_cmp(c, mem_rd(c, addr_abs(c))); c->cycles += 4; break;          /* CMP abs */
        case 0xDD: op_cmp(c, mem_rd(c, addr_absx(c))); c->cycles += 4; break;         /* CMP abs,x */
        case 0xD9: op_cmp(c, mem_rd(c, addr_absy(c))); c->cycles += 4; break;         /* CMP abs,y */
        case 0xC1: op_cmp(c, mem_rd(c, addr_izx(c))); c->cycles += 6; break;          /* CMP (zp,x) */
        case 0xD1: op_cmp(c, mem_rd(c, addr_izy(c))); c->cycles += 5; break;          /* CMP (zp),y */

        case 0xE0: op_cpx(c, fetch(c)); c->cycles += 2; break;                        /* CPX # */
        case 0xE4: op_cpx(c, mem_rd(c, addr_zp(c))); c->cycles += 3; break;           /* CPX zp */
        case 0xEC: op_cpx(c, mem_rd(c, addr_abs(c))); c->cycles += 4; break;          /* CPX abs */

        case 0xC0: op_cpy(c, fetch(c)); c->cycles += 2; break;                        /* CPY # */
        case 0xC4: op_cpy(c, mem_rd(c, addr_zp(c))); c->cycles += 3; break;           /* CPY zp */
        case 0xCC: op_cpy(c, mem_rd(c, addr_abs(c))); c->cycles += 4; break;          /* CPY abs */

        /* --- Shifts / increments / decrements --- */
        case 0x0A: c->a = op_asl(c, c->a); c->cycles += 2; break;                     /* ASL A */
        case 0x06: rmv(c, addr_zp(c), op_asl); c->cycles += 5; break;                 /* ASL zp */
        case 0x16: rmv(c, addr_zpx(c), op_asl); c->cycles += 6; break;                /* ASL zp,x */
        case 0x0E: rmv(c, addr_abs(c), op_asl); c->cycles += 6; break;                /* ASL abs */
        case 0x1E: rmv(c, addr_absx(c), op_asl); c->cycles += 7; break;               /* ASL abs,x */

        case 0x4A: c->a = op_lsr(c, c->a); c->cycles += 2; break;                     /* LSR A */
        case 0x46: rmv(c, addr_zp(c), op_lsr); c->cycles += 5; break;                 /* LSR zp */
        case 0x56: rmv(c, addr_zpx(c), op_lsr); c->cycles += 6; break;                /* LSR zp,x */
        case 0x4E: rmv(c, addr_abs(c), op_lsr); c->cycles += 6; break;                /* LSR abs */
        case 0x5E: rmv(c, addr_absx(c), op_lsr); c->cycles += 7; break;               /* LSR abs,x */

        case 0x2A: c->a = op_rol(c, c->a); c->cycles += 2; break;                     /* ROL A */
        case 0x26: rmv(c, addr_zp(c), op_rol); c->cycles += 5; break;                 /* ROL zp */
        case 0x36: rmv(c, addr_zpx(c), op_rol); c->cycles += 6; break;                /* ROL zp,x */
        case 0x2E: rmv(c, addr_abs(c), op_rol); c->cycles += 6; break;                /* ROL abs */
        case 0x3E: rmv(c, addr_absx(c), op_rol); c->cycles += 7; break;               /* ROL abs,x */

        case 0x6A: c->a = op_ror(c, c->a); c->cycles += 2; break;                     /* ROR A */
        case 0x66: rmv(c, addr_zp(c), op_ror); c->cycles += 5; break;                 /* ROR zp */
        case 0x76: rmv(c, addr_zpx(c), op_ror); c->cycles += 6; break;                /* ROR zp,x */
        case 0x6E: rmv(c, addr_abs(c), op_ror); c->cycles += 6; break;                /* ROR abs */
        case 0x7E: rmv(c, addr_absx(c), op_ror); c->cycles += 7; break;               /* ROR abs,x */

        case 0xE6: rmv(c, addr_zp(c), op_inc); c->cycles += 5; break;                 /* INC zp */
        case 0xF6: rmv(c, addr_zpx(c), op_inc); c->cycles += 6; break;                /* INC zp,x */
        case 0xEE: rmv(c, addr_abs(c), op_inc); c->cycles += 6; break;                /* INC abs */
        case 0xFE: rmv(c, addr_absx(c), op_inc); c->cycles += 7; break;               /* INC abs,x */

        case 0xC6: rmv(c, addr_zp(c), op_dec); c->cycles += 5; break;                 /* DEC zp */
        case 0xD6: rmv(c, addr_zpx(c), op_dec); c->cycles += 6; break;                /* DEC zp,x */
        case 0xCE: rmv(c, addr_abs(c), op_dec); c->cycles += 6; break;                /* DEC abs */
        case 0xDE: rmv(c, addr_absx(c), op_dec); c->cycles += 7; break;               /* DEC abs,x */

        case 0xE8: c->x++; nz(c, c->x); c->cycles += 2; break;                        /* INX */
        case 0xC8: c->y++; nz(c, c->y); c->cycles += 2; break;                        /* INY */
        case 0xCA: c->x--; nz(c, c->x); c->cycles += 2; break;                        /* DEX */
        case 0x88: c->y--; nz(c, c->y); c->cycles += 2; break;                        /* DEY */

        /* --- Branches --- */
        case 0x90: { int extra = branch(c, !flag(c, P_C)) ? 1 : 0; c->cycles += 2 + extra; break; }  /* BCC */
        case 0xB0: { int extra = branch(c,  flag(c, P_C)) ? 1 : 0; c->cycles += 2 + extra; break; }  /* BCS */
        case 0xF0: { int extra = branch(c,  flag(c, P_Z)) ? 1 : 0; c->cycles += 2 + extra; break; }  /* BEQ */
        case 0xD0: { int extra = branch(c, !flag(c, P_Z)) ? 1 : 0; c->cycles += 2 + extra; break; }  /* BNE */
        case 0x30: { int extra = branch(c,  flag(c, P_N)) ? 1 : 0; c->cycles += 2 + extra; break; }  /* BMI */
        case 0x10: { int extra = branch(c, !flag(c, P_N)) ? 1 : 0; c->cycles += 2 + extra; break; }  /* BPL */
        case 0x50: { int extra = branch(c,  flag(c, P_V)) ? 1 : 0; c->cycles += 2 + extra; break; }  /* BVC */
        case 0x70: { int extra = branch(c, !flag(c, P_V)) ? 1 : 0; c->cycles += 2 + extra; break; }  /* BVS */

        /* --- Jumps / subroutine / interrupts --- */
        case 0x4C: c->pc = addr_abs(c); c->cycles += 3; break;                        /* JMP abs */
        case 0x6C: c->pc = addr_ind(c); c->cycles += 5; break;                        /* JMP (abs) */
        case 0x20: { u16 target = addr_abs(c); push16(c, c->pc); c->pc = target; c->cycles += 6; break; } /* JSR */
        case 0x60: c->pc = (u16)(pull16(c) + 1); c->cycles += 6; break;               /* RTS */
        case 0x40: c->p = pull(c); c->p &= (u8)~(P_B | 0x20); c->pc = pull16(c); c->cycles += 6; break;   /* RTI */
        case 0x00: push16(c, c->pc); push(c, c->p | P_B | 0x20); set_flag(c, P_I, true);
                   c->pc = mem_rd(c, 0xFFFE) | (mem_rd(c, 0xFFFF) << 8); c->cycles += 7; break;          /* BRK */

        /* --- Flags --- */
        case 0x18: set_flag(c, P_C, false); c->cycles += 2; break;                    /* CLC */
        case 0x38: set_flag(c, P_C, true);  c->cycles += 2; break;                    /* SEC */
        case 0xB8: set_flag(c, P_V, false); c->cycles += 2; break;                    /* CLV */
        case 0x58: set_flag(c, P_I, false); c->cycles += 2; break;                    /* CLI */
        case 0x78: set_flag(c, P_I, true);  c->cycles += 2; break;                    /* SEI */
        case 0xD8: set_flag(c, P_D, false); c->cycles += 2; break;                    /* CLD */
        case 0xF8: set_flag(c, P_D, true);  c->cycles += 2; break;                    /* SED */

        case 0xEA: c->cycles += 2; break;                                             /* NOP */

        default:
            /* Unknown opcode: treat as a 2-cycle NOP so the machine keeps
             * advancing. TODO: expand to full/undocumented opcode set. */
            c->cycles += 2;
            break;
    }

    return (int)(c->cycles - cycles_before);
}

void cpu_init(Cpu8502 *cpu, CpuBus bus) {
    memset(cpu, 0, sizeof(*cpu));
    cpu->bus = bus;
    cpu->io_ddr = 0;
    cpu->io_port = 0x07;   /* C128 default banking: RAM at $0000/$0001, no IO */
    cpu->sp = 0xFD;
    cpu->p = 0x24;         /* IRQ disabled, unused bit set */
    cpu->cycles = 0;
}

void cpu_reset(Cpu8502 *cpu) {
    cpu->sp = 0xFD;
    cpu->p = 0x24;
    cpu->a = cpu->x = cpu->y = 0;
    cpu->pc = mem_rd(cpu, 0xFFFC) | (mem_rd(cpu, 0xFFFD) << 8);
    cpu->irq_level = false;
    cpu->nmi_level = false;
    cpu->nmi_pending = false;
    cpu->cycles = 0;
}

void cpu_irq(Cpu8502 *cpu, bool level) {
    cpu->irq_level = level;
}

void cpu_nmi(Cpu8502 *cpu, bool level) {
    if (level && !cpu->nmi_level) cpu->nmi_pending = true;
    cpu->nmi_level = level;
}

void cpu_pc(Cpu8502 *cpu, u16 pc) {
    cpu->pc = pc;
}
