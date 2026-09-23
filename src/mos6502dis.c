#include "mos6502dis.h"
#include <stdio.h>

typedef enum {
    IMP, ACC, IMM, ZP, ZPX, ZPY, ABS, ABX, ABY, IND, INX, INY, REL
} AddrMode;

typedef struct { const char *name; AddrMode mode; } Op;

#define O(code, name, mode) [code] = { name, mode }
static const Op ops[256] = {
    O(0x00,"BRK",IMP), O(0x01,"ORA",INX), O(0x05,"ORA",ZP),
    O(0x06,"ASL",ZP),  O(0x08,"PHP",IMP), O(0x09,"ORA",IMM),
    O(0x0A,"ASL",ACC), O(0x0D,"ORA",ABS), O(0x0E,"ASL",ABS),
    O(0x10,"BPL",REL), O(0x11,"ORA",INY), O(0x15,"ORA",ZPX),
    O(0x16,"ASL",ZPX), O(0x18,"CLC",IMP), O(0x19,"ORA",ABY),
    O(0x1D,"ORA",ABX), O(0x1E,"ASL",ABX),
    O(0x20,"JSR",ABS), O(0x21,"AND",INX), O(0x24,"BIT",ZP),
    O(0x25,"AND",ZP),  O(0x26,"ROL",ZP),  O(0x28,"PLP",IMP),
    O(0x29,"AND",IMM), O(0x2A,"ROL",ACC), O(0x2C,"BIT",ABS),
    O(0x2D,"AND",ABS), O(0x2E,"ROL",ABS), O(0x30,"BMI",REL),
    O(0x31,"AND",INY), O(0x35,"AND",ZPX), O(0x36,"ROL",ZPX),
    O(0x38,"SEC",IMP), O(0x39,"AND",ABY), O(0x3D,"AND",ABX),
    O(0x3E,"ROL",ABX),
    O(0x40,"RTI",IMP), O(0x41,"EOR",INX), O(0x45,"EOR",ZP),
    O(0x46,"LSR",ZP),  O(0x48,"PHA",IMP), O(0x49,"EOR",IMM),
    O(0x4A,"LSR",ACC), O(0x4C,"JMP",ABS), O(0x4D,"EOR",ABS),
    O(0x4E,"LSR",ABS), O(0x50,"BVC",REL), O(0x51,"EOR",INY),
    O(0x55,"EOR",ZPX), O(0x56,"LSR",ZPX), O(0x58,"CLI",IMP),
    O(0x59,"EOR",ABY), O(0x5D,"EOR",ABX), O(0x5E,"LSR",ABX),
    O(0x60,"RTS",IMP), O(0x61,"ADC",INX), O(0x65,"ADC",ZP),
    O(0x66,"ROR",ZP),  O(0x68,"PLA",IMP), O(0x69,"ADC",IMM),
    O(0x6A,"ROR",ACC), O(0x6C,"JMP",IND), O(0x6D,"ADC",ABS),
    O(0x6E,"ROR",ABS), O(0x70,"BVS",REL), O(0x71,"ADC",INY),
    O(0x75,"ADC",ZPX), O(0x76,"ROR",ZPX), O(0x78,"SEI",IMP),
    O(0x79,"ADC",ABY), O(0x7D,"ADC",ABX), O(0x7E,"ROR",ABX),
    O(0x81,"STA",INX), O(0x84,"STY",ZP),  O(0x85,"STA",ZP),
    O(0x86,"STX",ZP),  O(0x88,"DEY",IMP), O(0x8A,"TXA",IMP),
    O(0x8C,"STY",ABS), O(0x8D,"STA",ABS), O(0x8E,"STX",ABS),
    O(0x90,"BCC",REL), O(0x91,"STA",INY), O(0x94,"STY",ZPX),
    O(0x95,"STA",ZPX), O(0x96,"STX",ZPY), O(0x98,"TYA",IMP),
    O(0x99,"STA",ABY), O(0x9A,"TXS",IMP), O(0x9D,"STA",ABX),
    O(0xA0,"LDY",IMM), O(0xA1,"LDA",INX), O(0xA2,"LDX",IMM),
    O(0xA4,"LDY",ZP),  O(0xA5,"LDA",ZP),  O(0xA6,"LDX",ZP),
    O(0xA8,"TAY",IMP), O(0xA9,"LDA",IMM), O(0xAA,"TAX",IMP),
    O(0xAC,"LDY",ABS), O(0xAD,"LDA",ABS), O(0xAE,"LDX",ABS),
    O(0xB0,"BCS",REL), O(0xB1,"LDA",INY), O(0xB4,"LDY",ZPX),
    O(0xB5,"LDA",ZPX), O(0xB6,"LDX",ZPY), O(0xB8,"CLV",IMP),
    O(0xB9,"LDA",ABY), O(0xBA,"TSX",IMP), O(0xBC,"LDY",ABX),
    O(0xBD,"LDA",ABX), O(0xBE,"LDX",ABY),
    O(0xC0,"CPY",IMM), O(0xC1,"CMP",INX), O(0xC4,"CPY",ZP),
    O(0xC5,"CMP",ZP),  O(0xC6,"DEC",ZP),  O(0xC8,"INY",IMP),
    O(0xC9,"CMP",IMM), O(0xCA,"DEX",IMP), O(0xCC,"CPY",ABS),
    O(0xCD,"CMP",ABS), O(0xCE,"DEC",ABS), O(0xD0,"BNE",REL),
    O(0xD1,"CMP",INY), O(0xD5,"CMP",ZPX), O(0xD6,"DEC",ZPX),
    O(0xD8,"CLD",IMP), O(0xD9,"CMP",ABY), O(0xDD,"CMP",ABX),
    O(0xDE,"DEC",ABX),
    O(0xE0,"CPX",IMM), O(0xE1,"SBC",INX), O(0xE4,"CPX",ZP),
    O(0xE5,"SBC",ZP),  O(0xE6,"INC",ZP),  O(0xE8,"INX",IMP),
    O(0xE9,"SBC",IMM), O(0xEA,"NOP",IMP), O(0xEC,"CPX",ABS),
    O(0xED,"SBC",ABS), O(0xEE,"INC",ABS), O(0xF0,"BEQ",REL),
    O(0xF1,"SBC",INY), O(0xF5,"SBC",ZPX), O(0xF6,"INC",ZPX),
    O(0xF8,"SED",IMP), O(0xF9,"SBC",ABY), O(0xFD,"SBC",ABX),
    O(0xFE,"INC",ABX)

    /* NMOS undocumented opcodes used by a substantial amount of C64/C128
     * software. Keeping their addressing modes here is essential: treating
     * a multi-byte illegal opcode as one byte desynchronises the listing. */
    ,O(0x02,"JAM",IMP), O(0x03,"SLO",INX), O(0x04,"NOP",ZP),
    O(0x07,"SLO",ZP), O(0x0B,"ANC",IMM), O(0x0C,"NOP",ABS), O(0x0F,"SLO",ABS),
    O(0x12,"JAM",IMP), O(0x13,"SLO",INY), O(0x14,"NOP",ZPX),
    O(0x17,"SLO",ZPX), O(0x1A,"NOP",IMP), O(0x1B,"SLO",ABY),
    O(0x1C,"NOP",ABX), O(0x1F,"SLO",ABX),
    O(0x22,"JAM",IMP), O(0x23,"RLA",INX), O(0x27,"RLA",ZP),
    O(0x2B,"ANC",IMM), O(0x2F,"RLA",ABS),
    O(0x32,"JAM",IMP), O(0x33,"RLA",INY), O(0x34,"NOP",ZPX),
    O(0x37,"RLA",ZPX), O(0x3A,"NOP",IMP), O(0x3B,"RLA",ABY),
    O(0x3C,"NOP",ABX), O(0x3F,"RLA",ABX),
    O(0x42,"JAM",IMP), O(0x43,"SRE",INX), O(0x44,"NOP",ZP),
    O(0x47,"SRE",ZP), O(0x4B,"ALR",IMM), O(0x4F,"SRE",ABS),
    O(0x52,"JAM",IMP), O(0x53,"SRE",INY), O(0x54,"NOP",ZPX),
    O(0x57,"SRE",ZPX), O(0x5A,"NOP",IMP), O(0x5B,"SRE",ABY),
    O(0x5C,"NOP",ABX), O(0x5F,"SRE",ABX),
    O(0x62,"JAM",IMP), O(0x63,"RRA",INX), O(0x64,"NOP",ZP),
    O(0x67,"RRA",ZP), O(0x6B,"ARR",IMM), O(0x6F,"RRA",ABS),
    O(0x72,"JAM",IMP), O(0x73,"RRA",INY), O(0x74,"NOP",ZPX),
    O(0x77,"RRA",ZPX), O(0x7A,"NOP",IMP), O(0x7B,"RRA",ABY),
    O(0x7C,"NOP",ABX), O(0x7F,"RRA",ABX),
    O(0x80,"NOP",IMM), O(0x82,"NOP",IMM), O(0x83,"SAX",INX),
    O(0x87,"SAX",ZP), O(0x89,"NOP",IMM), O(0x8B,"XAA",IMM),
    O(0x8F,"SAX",ABS),
    O(0x92,"JAM",IMP), O(0x93,"AHX",INY), O(0x97,"SAX",ZPY),
    O(0x9B,"TAS",ABY), O(0x9C,"SHY",ABX), O(0x9E,"SHX",ABY),
    O(0x9F,"AHX",ABY),
    O(0xA3,"LAX",INX), O(0xA7,"LAX",ZP), O(0xAB,"LAX",IMM),
    O(0xAF,"LAX",ABS),
    O(0xB2,"JAM",IMP), O(0xB3,"LAX",INY), O(0xB7,"LAX",ZPY),
    O(0xBB,"LAS",ABY), O(0xBF,"LAX",ABY),
    O(0xC2,"NOP",IMM), O(0xC3,"DCP",INX), O(0xC7,"DCP",ZP),
    O(0xCB,"AXS",IMM), O(0xCF,"DCP",ABS),
    O(0xD2,"JAM",IMP), O(0xD3,"DCP",INY), O(0xD4,"NOP",ZPX),
    O(0xD7,"DCP",ZPX), O(0xDA,"NOP",IMP), O(0xDB,"DCP",ABY),
    O(0xDC,"NOP",ABX), O(0xDF,"DCP",ABX),
    O(0xE2,"NOP",IMM), O(0xE3,"ISC",INX), O(0xE7,"ISC",ZP),
    O(0xEB,"SBC",IMM), O(0xEF,"ISC",ABS),
    O(0xF2,"JAM",IMP), O(0xF3,"ISC",INY), O(0xF4,"NOP",ZPX),
    O(0xF7,"ISC",ZPX), O(0xFA,"NOP",IMP), O(0xFB,"ISC",ABY),
    O(0xFC,"NOP",ABX), O(0xFF,"ISC",ABX)
};
#undef O

int mos6502dis(const u8 *mem, u16 pc, char *out, size_t outsz) {
    u8 opcode = mem[pc];
    const Op *op = &ops[opcode];
    if (!op->name) {
        snprintf(out, outsz, ".BYTE $%02X", opcode);
        return 1;
    }
    u8 lo = mem[(u16)(pc + 1)];
    u8 hi = mem[(u16)(pc + 2)];
    u16 word = (u16)(lo | ((u16)hi << 8));
    switch (op->mode) {
    case IMP: snprintf(out, outsz, "%s", op->name); return 1;
    case ACC: snprintf(out, outsz, "%s A", op->name); return 1;
    case IMM: snprintf(out, outsz, "%s #$%02X", op->name, lo); return 2;
    case ZP:  snprintf(out, outsz, "%s $%02X", op->name, lo); return 2;
    case ZPX: snprintf(out, outsz, "%s $%02X,X", op->name, lo); return 2;
    case ZPY: snprintf(out, outsz, "%s $%02X,Y", op->name, lo); return 2;
    case ABS: snprintf(out, outsz, "%s $%04X", op->name, word); return 3;
    case ABX: snprintf(out, outsz, "%s $%04X,X", op->name, word); return 3;
    case ABY: snprintf(out, outsz, "%s $%04X,Y", op->name, word); return 3;
    case IND: snprintf(out, outsz, "%s ($%04X)", op->name, word); return 3;
    case INX: snprintf(out, outsz, "%s ($%02X,X)", op->name, lo); return 2;
    case INY: snprintf(out, outsz, "%s ($%02X),Y", op->name, lo); return 2;
    case REL: {
        u16 target = (u16)(pc + 2 + (s8)lo);
        snprintf(out, outsz, "%s $%04X", op->name, target);
        return 2;
    }
    }
    return 1;
}
