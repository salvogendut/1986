#pragma once
#include "types.h"
#include <stdbool.h>
#include <stddef.h>

/* Commodore 1541-compatible .d64 disk image (raw, decoded sectors).
 *
 * The .d64 is a linear image of 256-byte sectors. Tracks 1-17 have 21
 * sectors, 18-24 have 19, 25-30 have 18 and 31-35 have 17. Double-sided 1571
 * media uses the distinct D71 format and will be handled by a separate image
 * implementation. The directory is at track 18, sector 1.
 */

#define D64_MAX_TRACKS   35
#define D64_SECTOR_BYTES 256

typedef struct {
    u8  *data;        /* whole image in memory */
    size_t size;      /* image size in bytes */
    int  tracks;      /* 35 tracks; D71 is a distinct future format */
    bool has_errors;  /* a trailing D64 error-information block is present */
    char *path;       /* source path for atomic write-back */
    bool writable;    /* regular, writable image (not a symlink) */
} D64;

typedef enum {
    D64_SAVE_OK = 0,
    D64_SAVE_EXISTS,
    D64_SAVE_DISK_FULL,
    D64_SAVE_DIR_ERROR,
    D64_SAVE_WRITE_PROTECT,
    D64_SAVE_IO_ERROR,
    D64_SAVE_BAD_NAME,
} D64SaveResult;

/* One decoded directory entry (CBM DOS slot layout). */
typedef struct {
    int  blocks;      /* number of blocks (256-byte) used by the file */
    int  type;        /* CBMDOS file type (0=DEL,1=SEQ,2=PRG,3=USR,4=REL,...) */
    int  start_track; /* first sector in the file chain */
    int  start_sector;
    bool closed;      /* the entry was closed (no trailing '*' marker) */
    bool locked;      /* the entry is write-protected (locked) */
    char name[17];    /* PETSCII->ASCII filename, NUL terminated */
} D64DirEntry;

/* Number of sectors on a D64 track (1-35). */
int  d64_track_sectors(int track);

/* Byte offset of a track within the image. */
int  d64_track_offset(int track);

/* Load a .d64 image from a file. Returns 0 on success, -1 on failure. */
int  d64_open(D64 *d, const char *path);

/* Free the image. */
void d64_close(D64 *d);

/* Read one sector into buf[256]. Returns 0 on success, -1 if out of range. */
int  d64_read_sector(const D64 *d, int track, int sector, u8 *buf);

/* Read the directory into an array of decoded entries (up to cap). Returns
 * the number of entries read. */
int  d64_read_directory_entries(const D64 *d, D64DirEntry *ents, int cap);

/* Read the disk header (BAM): the disk name, ID, DOS type and free-block
 * count. Returns 0 on success. */
int  d64_read_bam(const D64 *d, char *name, size_t name_cap,
                  char id[2], u8 *dos_type, int *free_blocks);

/* Build the byte stream returned for a "$" directory request. The format is
 * VICE/CBM-DOS compatible: a 32-byte header, fixed 32-byte file records, and
 * a 31-byte BLOCKS FREE record. Returns its length, or 0 on failure. */
size_t d64_build_directory_program(const D64 *d, u8 *out, size_t cap);

/* Find a directory entry by CBM DOS name. Matching is ASCII-case-insensitive
 * for host convenience and supports '*' and '?' wildcards. An optional drive
 * prefix such as "0:" and comma-separated file options are ignored. */
int d64_find_file(const D64 *d, const char *name, D64DirEntry *entry);

/* Follow an entry's track/sector chain and copy its raw contents, including
 * the two-byte PRG load address. Returns the byte count, or -1 for a malformed
 * chain or insufficient output space. */
int d64_read_file(const D64 *d, const D64DirEntry *entry, u8 *out, size_t cap);

/* Save raw PRG bytes (including the two-byte load address) and atomically
 * replace the D64 file. Existing names require replace=true. Both the live
 * image and the file remain unchanged on error. */
D64SaveResult d64_save_prg(D64 *d, const char *name, const u8 *data,
                          size_t length, bool replace);
