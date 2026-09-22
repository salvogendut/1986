#pragma once
#include "disk_image.h"
#include "via6522.h"

/* Read-only 1571 GCR mechanism. Decoded D64/D71 sectors are expanded to a
 * circular 1541/1571 track; the DOS ROM sees bytes through VIA2, not through
 * the command-level VirtualDrive. Flux-level timing and writes remain open. */
#define GCR_TRACK_CAPACITY 7692

typedef struct {
    const DiskImage *image;
    u8 track_data[GCR_TRACK_CAPACITY];
    u8 track_sync[GCR_TRACK_CAPACITY];
    unsigned track_length, byte_pos;
    unsigned half_track; /* 2 = track 1, 3 = track 1.5, ... */
    unsigned side, zone;
    bool motor, led, sync, byte_ready;
    u8 read_byte;
    u64 bit_budget;
} GcrDrive;

void gcr_drive_init(GcrDrive *g);
void gcr_drive_reset(GcrDrive *g);
void gcr_drive_attach(GcrDrive *g, const DiskImage *image);
void gcr_drive_set_side(GcrDrive *g, unsigned side);
void gcr_drive_set_port_b(GcrDrive *g, u8 pins);
void gcr_drive_update_via(GcrDrive *g, Via6522 *via);
/* Advance the rotating medium by drive-CPU cycles. True if a new data byte
 * asserts BYTE READY/SO; the caller applies the CPU V flag. */
bool gcr_drive_tick(GcrDrive *g, Via6522 *via, unsigned cycles, bool fast);
u8 gcr_drive_read_byte(GcrDrive *g);
