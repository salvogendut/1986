#include "d64.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int d64_track_sectors(int track) {
    if (track < 1 || track > D64_MAX_TRACKS) return 0;
    if (track <= 17) return 21;
    if (track <= 24) return 19;
    if (track <= 30) return 18;
    return 17;
}

int d64_track_offset(int track) {
    if (track < 1 || track > D64_MAX_TRACKS + 1) return -1;
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

    /* 35-track image = 683 sectors, optionally followed by one error byte per
     * sector. D71 is deliberately not accepted as a large D64. */
    size_t base35 = (size_t)d64_track_offset(36);
    if (d->size == base35) {
        d->tracks = 35;
        d->has_errors = false;
    } else if (d->size == base35 + 683) {
        d->tracks = 35;
        d->has_errors = true;
    } else {
        d64_close(d);
        return -1;
    }
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

/* The directory lives at track 18 sector 1. Each sector is 256 bytes: bytes
 * 0-1 are the next-directory-sector link (track/sector; 0 = end of chain), and
 * 8 entries of 32 bytes follow at offsets 0,32,64,...,224 (slot 0 overlaps the
 * link bytes — a CBM DOS quirk). Each 32-byte slot is:
 *   [2] file-type byte, [3] first-track, [4] first-sector,
 *   [5..20] PETSCII filename (0xA0 padded), [30..31] block count (LE). */
static int d64_decode_slot(const u8 *e, D64DirEntry *ent) {
    if (e[2] == 0 || e[2] == 0xFF) return -1;
    ent->blocks = (int)(e[30] | (e[31] << 8));
    ent->type   = e[2] & 0x07;
    ent->start_track = e[3];
    ent->start_sector = e[4];
    ent->closed = (e[2] & 0x80) != 0;
    ent->locked = (e[2] & 0x40) != 0;
    int n = 0;
    for (int j = 5; j < 21 && n < 16; j++) {
        u8 c = e[j];
        if (c == 0xA0) break;
        ent->name[n++] = (c >= 0x20 && c < 0x80) ? (char)c : '?';
    }
    ent->name[n] = '\0';
    return 0;
}

static int d64_scan_directory(const D64 *d, D64DirEntry *ents, int cap) {
    u8 sec[256];
    int count = 0;
    int track = 18, sector = 1;

    for (int chain = 0; chain < 200 && track != 0; chain++) {
        if (d64_read_sector(d, track, sector, sec) != 0) break;
        for (int i = 0; i < 256; i += 32) {
            D64DirEntry e;
            if (d64_decode_slot(sec + i, &e) != 0) continue;
            if (count < cap) ents[count++] = e;
        }
        track = sec[0]; sector = sec[1];
    }
    return count;
}

int d64_read_directory_entries(const D64 *d, D64DirEntry *ents, int cap) {
    return d64_scan_directory(d, ents, cap);
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
            if (trk == 18) continue; /* directory track is not user capacity */
            const u8 *bam = sec + 4 + (trk - 1) * 4;
            int sectors = d64_track_sectors(trk);
            for (int s = 0; s < sectors; s++)
                if (bam[1 + s / 8] & (1u << (s & 7))) total++;
        }
        *free_blocks = total;
    }
    return 0;
}

static const char *const filetype_name[8] = {
    "DEL", "SEQ", "PRG", "USR", "REL", "CBM", "DIR", "???"
};

size_t d64_build_directory_program(const D64 *d, u8 *out, size_t cap) {
    if (!d || !d->data || !out) return 0;

    u8 bam[256];
    if (d64_read_sector(d, 18, 0, bam) != 0) return 0;

    D64DirEntry ents[512];
    int count = d64_read_directory_entries(d, ents, 512);
    size_t need = 32u + (size_t)count * 32u + 31u;
    if (cap < need) return 0;

    /* Header record. The first two bytes are the BASIC load address. */
    u8 *record = out;
    memset(record, 0x20, 32);
    record[0] = 0x01; record[1] = 0x04;
    record[2] = 0x01; record[3] = 0x01;
    record[4] = 0x00; record[5] = 0x00;
    record[6] = 0x12;
    record[7] = '"';
    for (int i = 0; i < 16; i++)
        record[8 + i] = bam[0x90 + i] == 0xA0 ? 0x20 : bam[0x90 + i];
    record[24] = '"';
    record[25] = ' ';
    for (int i = 0; i < 5; i++) {
        u8 b = bam[0xA2 + i];
        record[26 + i] = b == 0xA0 ? 0x20 : b;
    }
    record[31] = 0;

    /* VICE emits each file as one fixed-width 32-byte BASIC record. */
    for (int i = 0; i < count; i++) {
        const D64DirEntry *e = &ents[i];
        record = out + 32u + (size_t)i * 32u;
        memset(record, 0x20, 32);
        record[0] = 0x01; record[1] = 0x01;
        record[2] = (u8)e->blocks;
        record[3] = (u8)(e->blocks >> 8);

        int quote = 5;
        if (e->blocks < 100) quote++;
        if (e->blocks < 10) quote++;
        record[quote] = '"';
        size_t len = strlen(e->name);
        if (len > 16) len = 16;
        memcpy(record + quote + 1, e->name, len);
        record[quote + 1 + len] = '"';

        int name_field = quote + 1;
        record[name_field + 17] = e->closed ? ' ' : '*';
        memcpy(record + name_field + 18, filetype_name[e->type & 7], 3);
        record[name_field + 21] = e->locked ? '<' : ' ';
        record[31] = 0;
    }

    int free_blocks = 0;
    char name[17], id[2];
    d64_read_bam(d, name, sizeof(name), id, NULL, &free_blocks);
    record = out + 32u + (size_t)count * 32u;
    memset(record, 0x20, 31);
    record[0] = 0x01; record[1] = 0x01;
    record[2] = (u8)free_blocks;
    record[3] = (u8)(free_blocks >> 8);
    memcpy(record + 4, "BLOCKS FREE.", 12);
    record[29] = 0;
    record[30] = 0;
    return need;
}

static bool filename_matches(const char *pattern, const char *name) {
    while (*pattern) {
        if (*pattern == '*') return true;
        if (!*name) return false;
        if (*pattern != '?' &&
            toupper((unsigned char)*pattern) != toupper((unsigned char)*name))
            return false;
        pattern++;
        name++;
    }
    return *name == '\0';
}

int d64_find_file(const D64 *d, const char *name, D64DirEntry *entry) {
    if (!d || !name || !entry) return -1;

    while (*name == ' ' || *name == '@') name++;
    if (name[0] >= '0' && name[0] <= '9' && name[1] == ':') name += 2;

    char pattern[17];
    size_t n = 0;
    while (*name && *name != ',' && n < sizeof(pattern) - 1)
        pattern[n++] = *name++;
    while (n > 0 && pattern[n - 1] == ' ') n--;
    pattern[n] = '\0';
    if (n == 0) return -1;

    D64DirEntry entries[512];
    int count = d64_read_directory_entries(d, entries, 512);
    for (int i = 0; i < count; i++) {
        if (entries[i].type != 0 && filename_matches(pattern, entries[i].name)) {
            *entry = entries[i];
            return 0;
        }
    }
    return -1;
}

int d64_read_file(const D64 *d, const D64DirEntry *entry, u8 *out, size_t cap) {
    if (!d || !entry || !out) return -1;
    int track = entry->start_track;
    int sector = entry->start_sector;
    bool visited[D64_MAX_TRACKS + 1][21];
    memset(visited, 0, sizeof(visited));
    size_t length = 0;

    while (track != 0) {
        int sectors = d64_track_sectors(track);
        if (sectors == 0 || sector < 0 || sector >= sectors ||
            visited[track][sector])
            return -1;
        visited[track][sector] = true;

        u8 block[D64_SECTOR_BYTES];
        if (d64_read_sector(d, track, sector, block) != 0) return -1;

        int next_track = block[0];
        int next_sector = block[1];
        size_t bytes = next_track == 0
            ? (next_sector > 0 ? (size_t)next_sector - 1u : 0u)
            : 254u;
        if (bytes > 254 || length + bytes > cap) return -1;
        memcpy(out + length, block + 2, bytes);
        length += bytes;

        track = next_track;
        sector = next_sector;
    }
    return (int)length;
}
