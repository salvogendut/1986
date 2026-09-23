#pragma once
#include "types.h"
#include "mmu.h"
#include "cartridge.h"

/*
 * C128 memory: 128 KiB of RAM (two 64K banks), the ROM images, and the I/O
 * window. Read/write honours the MMU configuration register ($D500 / the
 * 8502's $01 port) so the CPU bus can be wired straight to mem_read()/
 * mem_write().
 *
 * C128DCR ROM layout (a single 32K "kernal" dump + a 32K BASIC dump):
 *   $4000-$7FFF  BASIC 7.0 low  (basic[0x0000..0x3FFF])
 *   $8000-$BFFF  BASIC 7.0 high (basic[0x4000..0x7FFF])
 *   $C000-$CFFF  EDITOR          (editor[0x0000..0x0FFF])
 *   $D000-$DFFF  I/O (or Z80 BIOS / function ROM depending on the MMU)
 *   $E000-$FFFF  KERNAL          (kernal[0x0000..0x1FFF])
 */

#define RAM_TOTAL     0x20000   /* 128 KiB, two 64K banks */
#define ROM_BASIC     0x8000
#define ROM_EDITOR    0x1000
#define ROM_Z80BIOS   0x1000
#define ROM_KERNAL    0x2000
#define ROM_CHARGEN   0x2000   /* C64 and native-C128 4K character banks */
#define ROM_C64_BASIC 0x2000
#define ROM_C64_KERNAL 0x2000
#define ROM_U36       0x8000   /* optional internal function ROM socket */

typedef struct {
    Mmu  mmu;
    u8   ram[RAM_TOTAL];
    u8   basic[ROM_BASIC];
    u8   editor[ROM_EDITOR];
    u8   z80bios[ROM_Z80BIOS];
    u8   kernal[ROM_KERNAL];
    u8   chargen[ROM_CHARGEN];
    u8   c64_basic[ROM_C64_BASIC];
    u8   c64_kernal[ROM_C64_KERNAL];
    bool c64_roms_loaded;
    u8   u36_rom[ROM_U36];
    bool u36_attached;
    u8   color_ram[0x800];   /* $D800-$DBFF nibbles, two 1K banks */
    Cartridge cart;          /* native external function ROM, if attached */
    u8   pla_data;           /* 8502 $01 port decoded value (PLA output) */
} Mem;

void mem_init(Mem *m);
void mem_reset(Mem *m);
/* Reinitialize volatile RAM as a C128 power-up would, preserving ROM/media. */
void mem_power_cycle(Mem *m);
/* Decode the effective 8502 port pins; input lines float high. */
void mem_set_processor_port(Mem *m, u8 dir, u8 data);
u8   mem_read(Mem *m, u16 addr);
void mem_write(Mem *m, u16 addr, u8 val);
/* Physical RAM page backing CPU page 0 or 1 (also used by the 8502 stack). */
u32 mem_cpu_page_offset(const Mem *m, unsigned page);
/* CR bit 0 is active-low: zero exposes I/O at $D000-$DFFF. */
bool mem_io_visible(const Mem *m);
bool mem_c64_mode(const Mem *m);
bool mem_c64_roms_loaded(const Mem *m);

/* Load a C128DCR ROM set from a directory. Expects the VICE-split files:
 *   kernal.bin (0x4000: EDITOR+Z80BIOS+KERNAL), basic.bin (0x8000:
 *   BASIC lo+hi) and optionally chargen.bin (0x2000; legacy 0x1000 images
 *   are mirrored into both banks). A single-file 32K kernal dump is also
 *   accepted. Returns the number of ROMs loaded. */
int  mem_load_c128_roms(Mem *m, const char *dir);

/* Raw 8/16/32 KiB U36 images; smaller dumps mirror through the 32 KiB
 * socket address space. Failed attachment leaves the socket empty. */
bool mem_attach_u36(Mem *m, const char *path);
void mem_detach_u36(Mem *m);
