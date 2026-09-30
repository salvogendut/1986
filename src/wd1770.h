#pragma once
#include "disk_image.h"

/* First WD1770 slice: decoded, read-only D81 media at 2 MHz / 250 kbit/s.
 * This is not yet a raw MFM-track implementation. Writes report protection;
 * raw track commands report RNF instead of silently succeeding. */
enum {
    WD1770_BUSY = 0x01, WD1770_DRQ = 0x02, WD1770_LOST = 0x04,
    WD1770_CRC = 0x08, WD1770_RNF = 0x10, WD1770_SPINUP = 0x20,
    WD1770_WP = 0x40, WD1770_MOTOR = 0x80
};
enum { WD1770_BYTE_CYCLES = 64, WD1770_REV_CYCLES = 400000 };

typedef struct {
    const DiskImage *image; /* borrowed; detach before closing/replacing it */
    u8 track, sector, data, command, status;
    unsigned head_track, side; /* physical head, NOT D81 side numbering */
    bool motor, disk_changed, irq, type1;
    int direction;
    unsigned phase, delay, rotation, idle_cycles, steps;
    unsigned position, length;
    u8 buffer[512];
    u64 cycles, read_bytes, sectors_read, head_steps;
} Wd1770;

void wd1770_init(Wd1770 *fdc);
/* Keeps mounted medium and head position; cancels pending transfers. */
void wd1770_reset(Wd1770 *fdc);
/* Accepts only ordinary 80-track D81 images, or NULL to eject. A rejected
 * image leaves the current medium alone. All media are write-protected. */
bool wd1770_attach(Wd1770 *fdc, const DiskImage *image);
void wd1770_set_motor(Wd1770 *fdc, bool on);
void wd1770_set_side(Wd1770 *fdc, unsigned side);
void wd1770_tick(Wd1770 *fdc, unsigned cycles);
u8 wd1770_read(Wd1770 *fdc, u16 addr);
void wd1770_write(Wd1770 *fdc, u16 addr, u8 value);
