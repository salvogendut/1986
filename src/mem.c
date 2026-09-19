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

/* Map the $4000-$BFFF BASIC window. Returns the ROM byte or -1 if RAM. */
static int read_basic(const Mem *m, u16 addr) {
    if (addr < 0x8000) {                     /* BASIC lo */
        if (m->mmu.mcr & 0x10) return m->basic[addr - 0x4000];
        return -1;
    }
    if (m->mmu.mcr & 0x08) return m->basic[0x4000 + (addr - 0x8000)];
    return -1;
}

u8 mem_read(Mem *m, u16 addr) {
    if (addr < 0x4000) return m->ram[bank_off(m, addr)];

    if (addr < 0x8000) {                     /* $4000-$7FFF */
        int v = read_basic(m, addr);
        return v >= 0 ? (u8)v : m->ram[bank_off(m, addr)];
    }
    if (addr < 0xC000) {                     /* $8000-$BFFF */
        int v = read_basic(m, addr);
        return v >= 0 ? (u8)v : m->ram[bank_off(m, addr)];
    }
    if (addr < 0xD000) {                     /* $C000-$CFFF EDITOR */
        if (m->mmu.mcr & 0x20) return m->editor[addr - 0xC000];
        return m->ram[bank_off(m, addr)];
    }
    if (addr < 0xE000) {                     /* $D000-$DFFF I/O */
        return m->ram[bank_off(m, addr)];    /* I/O is decoded above this layer */
    }
    /* $E000-$FFFF KERNAL */
    if (m->mmu.mcr & 0x01) return m->kernal[addr - 0xE000];
    return m->ram[bank_off(m, addr)];
}

void mem_write(Mem *m, u16 addr, u8 val) {
    if (addr >= 0x4000 && addr < 0xE000) {
        /* ROM windows are read-only while their MMU bits are set; writing
         * the banked RAM underneath is a no-op until the window is dropped. */
        bool rom = false;
        if (addr < 0x8000)   rom = (m->mmu.mcr & 0x10) != 0;
        else if (addr < 0xC000) rom = (m->mmu.mcr & 0x08) != 0;
        else if (addr < 0xD000) rom = (m->mmu.mcr & 0x20) != 0;
        if (rom) return;
    }
    if (addr >= 0xE000 && (m->mmu.mcr & 0x01)) return;   /* KERNAL window */
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

    u8 img[0x4000];

    /* KERNAL dump: 32K = EDITOR + Z80BIOS + KERNAL. */
    for (int i = 0; kernal_names[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, kernal_names[i]);
        size_t n = read_file(path, img, sizeof(img));
        if (n >= 0x4000) {
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
            /* Accept a 32K or 64K dump; copy what we have. */
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
