#pragma once
#include "types.h"
#include <stdbool.h>

/* Standalone integrated 1571CR machine. This is deliberately independent of
 * the C128's singleton VICE 8502 and of the command-level VirtualDrive.
 * Hardware peripherals and the physical IEC connection will use the decoded
 * I/O hooks; an unconnected register reads as an open bus (0xff). */
typedef enum {
    DRIVE1571CR_VIA1,
    DRIVE1571CR_VIA2,
    DRIVE1571CR_FDC,
    DRIVE1571CR_MOS5710
} Drive1571CrIo;

/* Pass the full CPU address; each peripheral owns its register mirroring. */
typedef u8 (*Drive1571CrIoRead)(void *ctx, Drive1571CrIo chip, u16 addr);
typedef void (*Drive1571CrIoWrite)(void *ctx, Drive1571CrIo chip, u16 addr, u8 value);

typedef struct {
    u8 a, x, y, sp, p;
    u16 pc;
    u64 cycles;
    bool irq, nmi_pending, jammed;
} Drive1571CrCpu;

typedef struct {
    u8 ram[0x800];
    u8 rom[0x8000];
    bool rom_loaded;
    Drive1571CrCpu cpu;
    Drive1571CrIoRead io_read;
    Drive1571CrIoWrite io_write;
    void *io_ctx;
} Drive1571Cr;

void drive1571cr_init(Drive1571Cr *drive);
bool drive1571cr_load_rom(Drive1571Cr *drive, const char *path);
void drive1571cr_set_io(Drive1571Cr *drive, Drive1571CrIoRead read,
                        Drive1571CrIoWrite write, void *ctx);
u8 drive1571cr_read(Drive1571Cr *drive, u16 addr);
void drive1571cr_write(Drive1571Cr *drive, u16 addr, u8 value);
void drive1571cr_reset(Drive1571Cr *drive);
void drive1571cr_irq(Drive1571Cr *drive, bool level);
void drive1571cr_nmi(Drive1571Cr *drive);
/* One NMOS 6502 instruction; returns cycles used, or zero if stopped/jammed. */
int drive1571cr_step(Drive1571Cr *drive);
int drive1571cr_run(Drive1571Cr *drive, int cycle_budget);
