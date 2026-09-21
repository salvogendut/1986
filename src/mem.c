#include "mem.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void mem_init(Mem *m) {
    memset(m, 0, sizeof(*m));
    mmu_init(&m->mmu);
    mem_set_processor_port(m, 0, 0); /* all input lines float high */
}

void mem_reset(Mem *m) {
    mmu_reset(&m->mmu);
}

void mem_set_processor_port(Mem *m, u8 dir, u8 data) {
    m->pla_data = (u8)((data & dir) | (u8)~dir);
}

static u32 bank_off(const Mem *m, u16 addr) {
    /* CR bit 6 selects the CPU RAM bank on a 128K C128 (bit 7 mirrors it).
     * RCR bits 2-3 enable common RAM in bank 0 at the bottom and/or top;
     * bits 0-1 choose 1K, 4K, 8K, or 16K. Pages 0 and 1 are separately
     * relocated by the MMU and default to bank 0 (relocation is not yet
     * implemented here). */
    static const unsigned common_size[] = { 0x400, 0x1000, 0x2000, 0x4000 };
    unsigned size = common_size[m->mmu.rcr & 0x03];
    u8 bank = (m->mmu.mcr >> 6) & 0x01;
    if (addr < 0x200 ||
        ((m->mmu.rcr & 0x04) && addr < size) ||
        ((m->mmu.rcr & 0x08) && addr >= 0x10000 - size))
        bank = 0;
    return ((u32)bank << 16) | addr;
}

/* Derive the C128 memory-config index from the raw $D500 config register,
 * mirroring VICE's mmu_switch_to_c128mode(). */
static unsigned c128_config(const Mem *m) {
    u8 v = m->mmu.mcr;
    unsigned cfg = 0;
    cfg |= (v & 0x02) ? 0 : 1;   /* bit0 = !v.bit1 */
    cfg |= (v & 0x0c) >> 1;      /* bits1-2 = v.bits2-3 */
    cfg |= (v & 0x30) >> 1;      /* bits3-4 = v.bits4-5 */
    cfg |= (v & 0x40) ? 0x20 : 0;/* bit5 = v.bit6 */
    cfg |= (v & 0x01) ? 0 : 0x40;/* bit6 = !v.bit0 */
    return cfg & 0x7F;
}

/* $4000-$7FFF: BASIC-lo ROM when the config's low bit is set (odd config),
 * RAM otherwise. Mirrors VICE's basic_lo_read mapping. */
static bool cfg_4000_is_rom(unsigned cfg) {
    return (cfg & 1) != 0;
}
/* C128 MMU selectors: 00=BASIC/KERNAL, 01=internal function ROM,
 * 10=external function ROM, 11=RAM. Internal ROM remains unimplemented. */
static unsigned lower_rom_select(const Mem *m) {
    return (m->mmu.mcr >> 2) & 3;
}
static unsigned upper_rom_select(const Mem *m) {
    return (m->mmu.mcr >> 4) & 3;
}

bool mem_io_visible(const Mem *m) {
    return (m->mmu.mcr & 0x01) == 0;
}

u8 mem_read(Mem *m, u16 addr) {
    unsigned cfg = c128_config(m);

    if (addr < 0x4000) return m->ram[bank_off(m, addr)];

    if (addr < 0x8000) {                     /* $4000-$7FFF */
        return cfg_4000_is_rom(cfg) ? m->basic[addr - 0x4000]
                                    : m->ram[bank_off(m, addr)];
    }
    if (addr < 0xC000) {                     /* $8000-$BFFF */
        if (lower_rom_select(m) == 0)
            return m->basic[0x4000 + (addr - 0x8000)];
        if (lower_rom_select(m) == 2 && m->cart.attached)
            return m->cart.rom[addr - 0x8000];
        return m->ram[bank_off(m, addr)];
    }
    if (addr < 0xD000) {                     /* $C000-$CFFF */
        if (upper_rom_select(m) == 0) return m->editor[addr - 0xC000];
        if (upper_rom_select(m) == 2 && m->cart.attached)
            return m->cart.rom[addr - 0x8000];
        return m->ram[bank_off(m, addr)];
    }
    if (addr < 0xE000) {                     /* $D000-$DFFF */
        /* The CPU bus dispatches visible I/O before reaching this layer. */
        if (upper_rom_select(m) == 2 && m->cart.attached)
            return m->cart.rom[addr - 0x8000];
        if (cfg < 8)
            return m->chargen[0x1000 + (addr & 0x0FFF)];
        return m->ram[bank_off(m, addr)];
    }
    /* $E000-$FFFF */
    if (upper_rom_select(m) == 0) return m->kernal[addr - 0xE000];
    if (upper_rom_select(m) == 2 && m->cart.attached)
        return m->cart.rom[addr - 0x8000];
    return m->ram[bank_off(m, addr)];
}

void mem_write(Mem *m, u16 addr, u8 val) {
    /* ROM windows are read-only for reads, but writes pass through to the
     * RAM (bank) underneath (the C128's "RAM behind ROM" behaviour). */
    u32 off = bank_off(m, addr);
    m->ram[off] = val;
}

/* Read up to cap bytes; returns the byte count (0 on failure). */
static size_t read_file(const char *path, u8 *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t got = fread(buf, 1, cap, f);
    fclose(f);
    return got;
}

int mem_load_c128_roms(Mem *m, const char *dir) {
    if (!dir || !dir[0]) return 0;
    char path[CONFIG_PATH_MAX];
    int loaded = 0;

    static const char *kernal_names[] = {
        "kernal.bin", "kernal.rom", "318022-02.u34", NULL
    };
    static const char *basic_names[] = {
        "basic.bin", "basic.rom", "318023-02.u32", NULL
    };
    static const char *chargen_names[] = {
        "chargen.bin", "chargen.rom", "characters-c128d", NULL
    };

    u8 img[0x8000];

    /* KERNAL dump: 32K = C64-mode (first 16K) + C128-mode (second 16K)
     * images, each being EDITOR + Z80BIOS + KERNAL. A bare 16K image is
     * also accepted. */
    for (int i = 0; kernal_names[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, kernal_names[i]);
        size_t n = read_file(path, img, sizeof(img));
        if (n >= 0x8000) {
            const u8 *im = img + 0x4000;   /* C128-mode image */
            memcpy(m->editor, im, ROM_EDITOR);
            memcpy(m->z80bios, &im[ROM_EDITOR], ROM_Z80BIOS);
            memcpy(m->kernal, &im[ROM_EDITOR + ROM_Z80BIOS], ROM_KERNAL);
            loaded++;
            break;
        } else if (n >= 0x4000) {
            memcpy(m->editor, img, ROM_EDITOR);
            memcpy(m->z80bios, &img[ROM_EDITOR], ROM_Z80BIOS);
            memcpy(m->kernal, &img[ROM_EDITOR + ROM_Z80BIOS], ROM_KERNAL);
            loaded++;
            break;
        }
    }

    /* BASIC dump: 32K = BASIC lo + BASIC hi. */
    for (int i = 0; basic_names[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, basic_names[i]);
        size_t n = read_file(path, img, sizeof(img));
        if (n >= 0x4000) {
            memcpy(m->basic, img, (n > 0x8000) ? 0x8000 : n);
            loaded++;
            break;
        }
    }

    /* CHARGEN dump: 8K (C64 and native-C128 4K banks). Retain support for
     * older 4K dumps by mirroring their only bank. */
    for (int i = 0; chargen_names[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, chargen_names[i]);
        size_t n = read_file(path, m->chargen, sizeof(m->chargen));
        if (n >= 0x2000) {
            loaded++;
            break;
        }
        if (n >= 0x1000) {
            memcpy(&m->chargen[0x1000], m->chargen, 0x1000);
            loaded++;
            break;
        }
    }

    return loaded;
}
