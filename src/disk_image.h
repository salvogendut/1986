#pragma once
#include "types.h"
#include <stdbool.h>
#include <stddef.h>

/* Raw, decoded Commodore disk images and a read-only single-PRG medium.
 * D64, D71, and D81 share file chains and directory-slot encoding but have
 * distinct geometry, BAMs, and headers. */

#define DISK_MAX_TRACKS   80
#define DISK_MAX_SECTORS  40
#define DISK_SECTOR_BYTES 256

typedef enum {
    DISK_FORMAT_D64,
    DISK_FORMAT_D71,
    DISK_FORMAT_D81,
    DISK_FORMAT_PRG,
} DiskFormat;

typedef struct {
    u8  *data;        /* whole image in memory */
    size_t size;      /* image size in bytes */
    DiskFormat format;
    int tracks;
    bool has_errors;  /* one trailing error byte per image sector */
    char *path;       /* source path for atomic write-back */
    bool writable;    /* regular, writable image (not a symlink) */
    char prg_name[17];/* single-file PRG directory name (without extension) */
} DiskImage;

typedef enum {
    DISK_SAVE_OK = 0,
    DISK_SAVE_EXISTS,
    DISK_SAVE_TYPE_MISMATCH,
    DISK_SAVE_DOS_MISMATCH,
    DISK_SAVE_DISK_FULL,
    DISK_SAVE_DIR_ERROR,
    DISK_SAVE_WRITE_PROTECT,
    DISK_SAVE_IO_ERROR,
    DISK_SAVE_BAD_NAME,
    DISK_SAVE_NOT_FOUND,
} DiskSaveResult;

/* One decoded directory entry (CBM DOS slot layout). */
typedef struct {
    int  blocks;      /* number of blocks (256-byte) used by the file */
    int  type;        /* CBMDOS file type (0=DEL,1=SEQ,2=PRG,3=USR,4=REL,...) */
    int  start_track; /* first sector in the file chain */
    int  start_sector;
    bool closed;      /* the entry was closed (no trailing '*' marker) */
    bool locked;      /* the entry is write-protected (locked) */
    char name[17];    /* PETSCII->ASCII filename, NUL terminated */
} DiskDirEntry;

/* D64 geometry helpers, also useful when constructing test fixtures. */
int  disk_image_d64_track_sectors(int track);
int  disk_image_d64_track_offset(int track);

/* Geometry for an opened image. */
int disk_image_track_sectors(const DiskImage *d, int track);
int disk_image_track_offset(const DiskImage *d, int track);
const char *disk_image_format_name(const DiskImage *d);

/* Detect D64/D71/D81 from exact size, or a .prg file from its extension.
 * Returns 0 on success, -1 on unsupported size or I/O error. */
int  disk_image_open(DiskImage *d, const char *path);

/* Free the image. */
void disk_image_close(DiskImage *d);

/* Read one sector into buf[256]. Returns 0 on success, -1 if out of range. */
int  disk_image_read_sector(const DiskImage *d, int track, int sector, u8 *buf);

/* Persist selected decoded sectors of one D64/D71 GCR track in one atomic
 * image replacement. sector_data contains all track sectors consecutively;
 * bit N of sector_mask selects sector N. The live image stays unchanged on
 * failure. */
DiskSaveResult disk_image_write_gcr_track(DiskImage *d, int track,
                                          const u8 *sector_data,
                                          unsigned sector_mask);

/* Read the directory into an array of decoded entries (up to cap). Returns
 * the number of entries read. */
int  disk_image_read_directory_entries(const DiskImage *d, DiskDirEntry *ents, int cap);

/* Read the disk header and free-block count. Returns 0 on success. */
int  disk_image_read_bam(const DiskImage *d, char *name, size_t name_cap,
                  char id[2], u8 *dos_type, int *free_blocks);

/* Build the byte stream returned for a "$" directory request. The format is
 * VICE/CBM-DOS compatible: a 32-byte header, fixed 32-byte file records, and
 * a 31-byte BLOCKS FREE record. Returns its length, or 0 on failure. */
size_t disk_image_build_directory_program(const DiskImage *d, u8 *out, size_t cap);

/* Find a directory entry by CBM DOS name. Matching is ASCII-case-insensitive
 * for host convenience and supports '*' and '?' wildcards. An optional drive
 * prefix such as "0:" and comma-separated file options are ignored. */
int disk_image_find_file(const DiskImage *d, const char *name, DiskDirEntry *entry);

/* Follow an entry's track/sector chain and copy its raw contents, including
 * the two-byte PRG load address. Returns the byte count, or -1 for a malformed
 * chain or insufficient output space. */
int disk_image_read_file(const DiskImage *d, const DiskDirEntry *entry, u8 *out, size_t cap);

/* Save raw PRG bytes (including the two-byte load address) and atomically
 * replace the host image. Existing names require replace=true. Both the live
 * image and the file remain unchanged on error. */
DiskSaveResult disk_image_save_prg(DiskImage *d, const char *name, const u8 *data,
                          size_t length, bool replace);

/* Mutate ordinary SEQ/PRG/USR directory entries. SCRATCH accepts '*' and '?'
 * and reports the number removed; RENAME requires two literal names. Both
 * operations leave the image unchanged if validation or write-back fails. */
DiskSaveResult disk_image_scratch(DiskImage *d, const char *pattern, int *removed);
DiskSaveResult disk_image_rename(DiskImage *d, const char *new_name,
                                 const char *old_name);
