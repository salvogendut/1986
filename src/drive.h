#pragma once
#include "types.h"
#include "config.h"
#include "d64.h"
#include <stdbool.h>
#include <stddef.h>

/* Commodore 1571 disk drive emulation.
 *
 * The drive runs a 6502 executing the 1571 DOS ROM, connected to the C128
 * through the IEC serial bus. A .d64 image is attached as the disk media.
 */

#define DRIVE_ROM_SIZE 0x8000   /* 1571 DOS ROM (32 KB) */

typedef struct {
    Config *cfg;
    int     unit;          /* IEC device number (8-11) */

    u8      rom[DRIVE_ROM_SIZE];
    bool    rom_loaded;

    D64     d64;           /* attached disk image */
    bool    disk_attached;
} Drive;

void drive_init(Drive *d, Config *cfg);
void drive_reset(Drive *d);

/* Load the 1571 DOS ROM (dos1571.bin). Returns 0 on success. */
int  drive_load_rom(Drive *d, const char *dir);

/* Attach (or detach with path=NULL) a .d64 image. */
int  drive_attach_disk(Drive *d, const char *path);

/* Read the directory of the attached image into out. Returns entry count. */
int  drive_directory(const Drive *d, char *out, size_t cap);
