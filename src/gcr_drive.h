#pragma once
#include "disk_image.h"
#include "via6522.h"

/* D64/D71 GCR mechanism. Decoded sectors are expanded to a circular track;
 * the DOS ROM sees bytes through VIA2, not the command-level VirtualDrive.
 * Standard, checksum-valid sector writes are decoded back to disk images.
 * Flux-level and protection-track formats remain out of scope. */
#define GCR_TRACK_CAPACITY 7692

typedef struct {
    DiskImage *image;
    u8 track_data[GCR_TRACK_CAPACITY];
    u8 track_sync[GCR_TRACK_CAPACITY];
    u8 sector_for_pos[GCR_TRACK_CAPACITY];
    unsigned track_length, byte_pos;
    unsigned half_track; /* 2 = track 1, 3 = track 1.5, ... */
    unsigned side, zone;
    bool motor, led, sync, byte_ready;
    u8 read_byte;
    u64 bit_budget;
    unsigned step_events, read_events; /* presentation-only activity counters */
    unsigned write_events;
    bool write_mode, dirty, dirty_unmapped;
    unsigned dirty_sector_mask;
    u8 write_value, write_shift;
    DiskSaveResult write_error;
    bool write_error_reported;
} GcrDrive;

void gcr_drive_init(GcrDrive *g);
void gcr_drive_reset(GcrDrive *g);
void gcr_drive_attach(GcrDrive *g, DiskImage *image);
void gcr_drive_set_side(GcrDrive *g, unsigned side);
void gcr_drive_set_port_b(GcrDrive *g, u8 pins);
void gcr_drive_set_write_mode(GcrDrive *g, bool enabled);
void gcr_drive_write_byte(GcrDrive *g, u8 value);
DiskSaveResult gcr_drive_flush(GcrDrive *g);
void gcr_drive_update_via(GcrDrive *g, Via6522 *via);
/* Advance the rotating medium by drive-CPU cycles. True if a new data byte
 * asserts BYTE READY/SO; the caller applies the CPU V flag. */
bool gcr_drive_tick(GcrDrive *g, Via6522 *via, unsigned cycles, bool fast);
u8 gcr_drive_read_byte(GcrDrive *g);
