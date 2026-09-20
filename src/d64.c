#include "d64.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int d64_track_sectors(int track) {
    if (track <= 17) return 21;
    if (track <= 24) return 19;
    if (track <= 30) return 18;
    return 17;
}

int d64_track_offset(int track) {
    int off = 0;
    for (int t = 1; t < track && t <= D64_MAX_TRACKS; t++)
        off += d64_track_sectors(t) * D64_SECTOR_BYTES;
    return off;
}

int d64_open(D64 *d, const char *path) {
    memset(d, 0, sizeof(*d));
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return -1; }

    d->data = malloc((size_t)sz);
    if (!d->data) { fclose(f); return -1; }
    if (fread(d->data, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); d64_close(d); return -1; }
    fclose(f);

    d->size = (size_t)sz;

    /* 35-track image = 174848 bytes (plus optional 2732-byte error block). */
    size_t base35 = (size_t)d64_track_offset(36);
    if (d->size >= base35) { d->tracks = 35; d->has_errors = d->size >= base35 + 683 * 4; }
    else if (d->size >= 174848) { d->tracks = 35; d->has_errors = d->size >= 177632; }
    else if (d->size >= 699392) { d->tracks = 70; d->has_errors = d->size >= 699392 + 1366 * 4; }
    else { d64_close(d); return -1; }
    return 0;
}

void d64_close(D64 *d) {
    free(d->data);
    memset(d, 0, sizeof(*d));
}

int d64_read_sector(const D64 *d, int track, int sector, u8 *buf) {
    if (track < 1 || track > d->tracks) return -1;
    if (sector < 0 || sector >= d64_track_sectors(track)) return -1;
    int off = d64_track_offset(track) + sector * D64_SECTOR_BYTES;
    if (off + D64_SECTOR_BYTES > (int)d->size) return -1;
    memcpy(buf, d->data + off, D64_SECTOR_BYTES);
    return 0;
}

/* The directory lives at track 18 sector 1. Each entry is 32 bytes:
 *   [0] next-track, [1] next-sector, [2..3] file type + track/sector,
 *   [4..5] size (LE), [6..21] PETSCII filename, [22..27] ... , [28..31] .
 * Filenames are PETSCII; convert to ASCII and trim trailing 0xA0. */
int d64_read_directory(const D64 *d, char *out, size_t cap) {
    u8 sec[256];
    int count = 0;
    int track = 18, sector = 1;

    for (int chain = 0; chain < 200 && (track || sector); chain++) {
        if (d64_read_sector(d, track, sector, sec) != 0) break;
        int next_t = sec[0], next_s = sec[1];
        for (int i = 0; i < 256; i += 32) {
            u8 *e = sec + i;
            if (e[0] == 0 && e[1] == 0xFF) { track = next_t; sector = next_s; goto next_sector; }
            if (e[2] == 0 || e[2] == 0xFF) continue;   /* unused entry */
            unsigned size = (unsigned)e[4] | ((unsigned)e[5] << 8);
            /* Filename: bytes 6..21 (PETSCII). Some images store the first
             * character in byte 5 (the size high byte), so prepend it when it
             * is a printable ASCII letter. */
            char name[17];
            int n = 0;
            if (e[5] >= 0x41 && e[5] <= 0x5A) name[n++] = (char)e[5];
            for (int j = 6; j < 22 && n < 16; j++) {
                u8 c = e[j];
                if (c == 0xA0) break;
                name[n++] = (c >= 0x20 && c < 0x80) ? (char)c : '?';
            }
            name[n] = '\0';
            if (count < (int)cap) {
                int len = snprintf(out + strlen(out), cap - strlen(out), "%u %s\n", size, name);
                if (len < 0) break;
            }
            count++;
        }
next_sector:
        track = next_t; sector = next_s;
    }
    return count;
}

/* The disk header lives in the BAM (track 18, sector 0): disk name at
 * offset 0x90 (PETSCII, 16 bytes), ID at 0xA2, DOS type at 0xA4. The free
 * block count is derived from the BAM bitmaps (offsets 4..0x8F). */
int d64_read_bam(const D64 *d, char *name, size_t name_cap,
                 char id[2], u8 *dos_type, int *free_blocks) {
    u8 sec[256];
    if (d64_read_sector(d, 18, 0, sec) != 0) return -1;

    int n = 0;
    for (int i = 0x90; i < 0xA0 && n < (int)name_cap - 1; i++) {
        u8 c = sec[i];
        if (c == 0xA0) break;
        name[n++] = (c >= 0x20 && c < 0x80) ? (char)c : '?';
    }
    name[n] = '\0';
    id[0] = (char)sec[0xA2];
    id[1] = (char)sec[0xA3];
    if (dos_type) *dos_type = sec[0xA4];

    if (free_blocks) {
        int total = 0;
        for (int trk = 1; trk <= d->tracks; trk++) {
            int sectors = d64_track_sectors(trk);
            u8 *bam = sec + 4 + (trk - 1) * 4;
            int free = bam[1] | (bam[2] << 8);
            for (int s = 0; s < sectors; s++)
                if (free & (1 << s)) total++;
        }
        *free_blocks = total;
    }
    return 0;
}
