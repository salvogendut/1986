#pragma once
#include "types.h"
#include "config.h"
#include "d64.h"
#include <stdbool.h>
#include <stddef.h>

/* Commodore 1571 disk drive, pluggable backend.
 *
 * The C128's KERNAL talks to the drive over the IEC serial bus (via the
 * CIA1's serial port). The emulator intercepts the KERNAL's IEC routines
 * (LISTEN / TALK / send / receive) and forwards them to a DriveOps backend.
 *
 * A minimal backend (drive_smart) answers DIRECTORY/LOAD by reading the
 * attached .d64 directly. It can later be replaced by a full 1571 emulation
 * that runs the DOS ROM on its own 6502, by providing a different DriveOps.
 */

#define DRIVE_ROM_SIZE 0x8000   /* 1571 DOS ROM (32 KB) */

typedef struct Drive Drive;

typedef struct DriveOps {
    void (*reset)(Drive *d);
    int  (*attach_disk)(Drive *d, const char *path);
    void (*set_unit)(Drive *d, int unit);

    /* IEC bus callbacks (invoked by the KERNAL serial traps).
     * b is the byte on the bus for attention (LISTEN/TALK/secondary). */
    void (*attention)(Drive *d, u8 b);
    void (*send)(Drive *d, u8 byte);        /* C128 sends a command byte */
    int  (*receive)(Drive *d, u8 *byte);    /* drive returns a byte (1 = ok) */
    void (*unlisten)(Drive *d);             /* UNLISTEN */
    void (*untalk)(Drive *d);               /* UNTALK */
} DriveOps;

typedef struct Drive {
    Config *cfg;
    const DriveOps *ops;
    void *impl;         /* backend-specific state */

    int     unit;       /* IEC device number (8-11) */
    u8      rom[DRIVE_ROM_SIZE];
    bool    rom_loaded;

    D64     d64;        /* attached disk image */
    bool    disk_attached;
} Drive;

void drive_init(Drive *d, Config *cfg);
void drive_reset(Drive *d);

/* Load the 1571 DOS ROM (dos1571.bin). Returns 0 on success. */
int  drive_load_rom(Drive *d, const char *dir);

/* Attach (or detach with path=NULL) a .d64 image. */
int  drive_attach_disk(Drive *d, const char *path);

/* Select the IEC device number. */
void drive_set_unit(Drive *d, int unit);

/* Install the minimal smart-drive backend (the default). */
void drive_use_smart(Drive *d);
