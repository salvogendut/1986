#include "mem.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void mem_init(Mem *m) {
    memset(m, 0, sizeof(*m));
    mmu_init(&m->mmu);
}

void mem_reset(Mem *m) {
    mmu_reset(&m->mmu);
}

static u16 bank_off(const Mem *m, u16 addr) {
    u8 bank = m->mmu.ram_bank & 0x01;
    return (u16)((bank << 16) | addr);
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

/* $8000-$BFFF: k = cfg&7 -> 0,1 BASIC-hi; 6,7 RAM; 2-5 function ROM (RAM for now). */
static bool cfg_8000_is_rom(unsigned cfg) {
    return (cfg & 7) == 0 || (cfg & 7) == 1;
}
/* $E000-$FFFF: k = cfg&7 -> 0,1 KERNAL; 6,7 RAM; 2-5 function ROM (RAM for now). */
static bool cfg_e000_is_rom(unsigned cfg) {
    return (cfg & 7) == 0 || (cfg & 7) == 1;
}
/* $4000-$7FFF: BASIC-lo ROM for configs 32-63 and 96-127. */
static bool cfg_4000_is_rom(unsigned cfg) {
    return (cfg >= 32 && cfg < 64) || (cfg >= 96 && cfg < 128);
}

u8 mem_read(Mem *m, u16 addr) {
    unsigned cfg = c128_config(m);

    if (addr < 0x4000) return m->ram[bank_off(m, addr)];

    if (addr < 0x8000) {                     /* $4000-$7FFF */
        return cfg_4000_is_rom(cfg) ? m->basic[addr - 0x4000]
                                    : m->ram[bank_off(m, addr)];
    }
    if (addr < 0xC000) {                     /* $8000-$BFFF */
        if (cfg_8000_is_rom(cfg))
            return m->basic[0x4000 + (addr - 0x8000)];
        return m->ram[bank_off(m, addr)];
    }
    if (addr < 0xD000) {                     /* $C000-$CFFF EDITOR (always ROM in C128 mode) */
        return m->editor[addr - 0xC000];
    }
    if (addr < 0xE000) {                     /* $D000-$DFFF I/O */
        return m->ram[bank_off(m, addr)];    /* I/O is decoded above this layer */
    }
    /* $E000-$FFFF */
    if (cfg_e000_is_rom(cfg)) return m->kernal[addr - 0xE000];
    return m->ram[bank_off(m, addr)];
}

void mem_write(Mem *m, u16 addr, u8 val) {
    unsigned cfg = c128_config(m);

    /* ROM windows are read-only while mapped. */
    bool rom = false;
    if (addr >= 0x4000 && addr < 0x8000)   rom = cfg_4000_is_rom(cfg);
    else if (addr >= 0x8000 && addr < 0xC000) rom = cfg_8000_is_rom(cfg);
    else if (addr >= 0xC000 && addr < 0xD000) rom = true;   /* EDITOR */
    else if (addr >= 0xE000)                rom = cfg_e000_is_rom(cfg);
    if (rom) return;

    m->ram[bank_off(m, addr)] = val;
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

    /* CHARGEN dump: 4K. */
    for (int i = 0; chargen_names[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, chargen_names[i]);
        size_t n = read_file(path, m->chargen, sizeof(m->chargen));
        if (n >= 0x1000) { loaded++; break; }
    }

    return loaded;
}
