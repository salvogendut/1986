/* mem.h - minimal VICE memory shim. Routes to 1986's mem (see src/cpu.c).
 * GPLv2+; see Development.md. */
#ifndef VICE_MEM_H
#define VICE_MEM_H
#include "types.h"

typedef BYTE read_func_t(WORD addr);
typedef read_func_t *read_func_ptr_t;
typedef void store_func_t(WORD addr, BYTE value);
typedef store_func_t *store_func_ptr_t;

extern read_func_ptr_t *_mem_read_tab_ptr;
extern store_func_ptr_t *_mem_write_tab_ptr;

extern BYTE *mem_ram;
extern void mem_mmu_translate(unsigned int addr, BYTE **base, int *start, int *limit);
extern void mem_powerup(void);
extern int mem_rom_trap_allowed(unsigned int addr);
#endif
