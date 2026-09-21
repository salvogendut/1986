#define _POSIX_C_SOURCE 200809L
#include "disk_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static bool path_writable_regular(const char *path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES &&
        !(attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT |
                   FILE_ATTRIBUTE_READONLY));
#else
    struct stat st;
    return lstat(path, &st) == 0 && S_ISREG(st.st_mode) &&
        (st.st_mode & 0222) != 0;
#endif
}

int disk_image_d64_track_sectors(int track) {
    if (track < 1 || track > 35) return 0;
    if (track <= 17) return 21;
    if (track <= 24) return 19;
    if (track <= 30) return 18;
    return 17;
}

int disk_image_d64_track_offset(int track) {
    if (track < 1 || track > 36) return -1;
    int off = 0;
    for (int t = 1; t < track; t++)
        off += disk_image_d64_track_sectors(t) * DISK_SECTOR_BYTES;
    return off;
}

int disk_image_track_sectors(const DiskImage *d, int track) {
    if (!d || track < 1 || track > d->tracks) return 0;
    if (d->format == DISK_FORMAT_D81) return 40;
    return disk_image_d64_track_sectors(track > 35 ? track - 35 : track);
}

int disk_image_track_offset(const DiskImage *d, int track) {
    if (!d || track < 1 || track > d->tracks + 1) return -1;
    if (d->format == DISK_FORMAT_D81)
        return (track - 1) * 40 * DISK_SECTOR_BYTES;
    if (track <= 36) return disk_image_d64_track_offset(track);
    return disk_image_d64_track_offset(36) +
           disk_image_d64_track_offset(track - 35);
}

const char *disk_image_format_name(const DiskImage *d) {
    if (!d) return "DISK";
    switch (d->format) {
        case DISK_FORMAT_D64: return "D64";
        case DISK_FORMAT_D71: return "D71";
        case DISK_FORMAT_D81: return "D81";
    }
    return "DISK";
}

int disk_image_open(DiskImage *d, const char *path) {
    if (!d || !path) return -1;
    memset(d, 0, sizeof(*d));
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    d->size = (size_t)sz;

    /* Exact sizes distinguish these formats even without a file extension. */
    const size_t base35 = (size_t)disk_image_d64_track_offset(36);
    if (d->size == base35 || d->size == base35 + 683u) {
        d->format = DISK_FORMAT_D64;
        d->tracks = 35;
        d->has_errors = d->size != base35;
    } else if (d->size == base35 * 2 || d->size == base35 * 2 + 1366u) {
        d->format = DISK_FORMAT_D71;
        d->tracks = 70;
        d->has_errors = d->size != base35 * 2;
    } else if (d->size == 819200u || d->size == 822400u) {
        d->format = DISK_FORMAT_D81;
        d->tracks = 80;
        d->has_errors = d->size != 819200u;
    } else {
        fclose(f);
        memset(d, 0, sizeof(*d));
        return -1;
    }
    d->data = malloc(d->size);
    if (!d->data) { fclose(f); disk_image_close(d); return -1; }
    if (fread(d->data, 1, d->size, f) != d->size) {
        fclose(f);
        disk_image_close(d);
        return -1;
    }
    fclose(f);
    size_t path_len = strlen(path);
    d->path = malloc(path_len + 1);
    if (!d->path) { disk_image_close(d); return -1; }
    memcpy(d->path, path, path_len + 1);
    if (path_writable_regular(path)) {
        FILE *write_probe = fopen(path, "rb+");
        if (write_probe) { d->writable = true; fclose(write_probe); }
    }
    return 0;
}

void disk_image_close(DiskImage *d) {
    free(d->data);
    free(d->path);
    memset(d, 0, sizeof(*d));
}

int disk_image_read_sector(const DiskImage *d, int track, int sector, u8 *buf) {
    if (!d || !buf || track < 1 || track > d->tracks) return -1;
    if (sector < 0 || sector >= disk_image_track_sectors(d, track)) return -1;
    int off = disk_image_track_offset(d, track) + sector * DISK_SECTOR_BYTES;
    if (off + DISK_SECTOR_BYTES > (int)d->size) return -1;
    memcpy(buf, d->data + off, DISK_SECTOR_BYTES);
    return 0;
}

static int directory_track(const DiskImage *d) {
    return d->format == DISK_FORMAT_D81 ? 40 : 18;
}

static int directory_start(const DiskImage *d) {
    return d->format == DISK_FORMAT_D81 ? 3 : 1;
}

/* Each directory sector is 256 bytes: bytes
 * 0-1 are the next-directory-sector link (track/sector; 0 = end of chain), and
 * 8 entries of 32 bytes follow at offsets 0,32,64,...,224 (slot 0 overlaps the
 * link bytes — a CBM DOS quirk). Each 32-byte slot is:
 *   [2] file-type byte, [3] first-track, [4] first-sector,
 *   [5..20] PETSCII filename (0xA0 padded), [30..31] block count (LE). */
static int disk_image_decode_slot(const u8 *e, DiskDirEntry *ent) {
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

static int disk_image_scan_directory(const DiskImage *d, DiskDirEntry *ents, int cap) {
    u8 sec[256];
    int count = 0;
    int track = directory_track(d), sector = directory_start(d);
    bool visited[DISK_MAX_TRACKS + 1][DISK_MAX_SECTORS] = { { false } };

    for (int chain = 0; chain < 3200 && track != 0; chain++) {
        if (track < 1 || track > d->tracks || sector < 0 ||
            sector >= disk_image_track_sectors(d, track) ||
            visited[track][sector]) break;
        visited[track][sector] = true;
        if (disk_image_read_sector(d, track, sector, sec) != 0) break;
        for (int i = 0; i < 256; i += 32) {
            DiskDirEntry e;
            if (disk_image_decode_slot(sec + i, &e) != 0) continue;
            if (count < cap) ents[count++] = e;
        }
        track = sec[0]; sector = sec[1];
    }
    return count;
}

int disk_image_read_directory_entries(const DiskImage *d, DiskDirEntry *ents, int cap) {
    return disk_image_scan_directory(d, ents, cap);
}

static bool bam_free(const DiskImage *d, const u8 *image, int track, int sector);

/* D64/D71 headers live at 18/0; D81 has a separate header at 40/0. */
int disk_image_read_bam(const DiskImage *d, char *name, size_t name_cap,
                 char id[2], u8 *dos_type, int *free_blocks) {
    u8 sec[256];
    if (!d || !name || name_cap == 0 || !id ||
        disk_image_read_sector(d, directory_track(d), 0, sec) != 0) return -1;
    int name_offset = d->format == DISK_FORMAT_D81 ? 0x04 : 0x90;
    int id_offset = d->format == DISK_FORMAT_D81 ? 0x16 : 0xA2;

    int n = 0;
    for (int i = name_offset; i < name_offset + 16 &&
         (size_t)n + 1 < name_cap; i++) {
        u8 c = sec[i];
        if (c == 0xA0) break;
        name[n++] = (c >= 0x20 && c < 0x80) ? (char)c : '?';
    }
    name[n] = '\0';
    id[0] = (char)sec[id_offset];
    id[1] = (char)sec[id_offset + 1];
    if (dos_type) *dos_type = sec[d->format == DISK_FORMAT_D81 ? 0x19 : 0xA5];

    if (free_blocks) {
        int total = 0;
        for (int trk = 1; trk <= d->tracks; trk++) {
            if (trk == directory_track(d) ||
                (d->format == DISK_FORMAT_D71 && trk == 53)) continue;
            int sectors = disk_image_track_sectors(d, trk);
            for (int s = 0; s < sectors; s++)
                if (bam_free(d, d->data, trk, s)) total++;
        }
        *free_blocks = total;
    }
    return 0;
}

static const char *const filetype_name[8] = {
    "DEL", "SEQ", "PRG", "USR", "REL", "CBM", "DIR", "???"
};

size_t disk_image_build_directory_program(const DiskImage *d, u8 *out, size_t cap) {
    if (!d || !d->data || !out) return 0;

    u8 header[256];
    if (disk_image_read_sector(d, directory_track(d), 0, header) != 0) return 0;
    int name_offset = d->format == DISK_FORMAT_D81 ? 0x04 : 0x90;
    int id_offset = d->format == DISK_FORMAT_D81 ? 0x16 : 0xA2;

    DiskDirEntry ents[512];
    int count = disk_image_read_directory_entries(d, ents, 512);
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
        record[8 + i] = header[name_offset + i] == 0xA0 ? 0x20 :
                        header[name_offset + i];
    record[24] = '"';
    record[25] = ' ';
    record[26] = header[id_offset] == 0xA0 ? ' ' : header[id_offset];
    record[27] = header[id_offset + 1] == 0xA0 ? ' ' : header[id_offset + 1];
    record[28] = ' ';
    record[29] = header[d->format == DISK_FORMAT_D81 ? 0x19 : 0xA5];
    record[30] = header[d->format == DISK_FORMAT_D81 ? 0x1A : 0xA6];
    record[31] = 0;

    /* VICE emits each file as one fixed-width 32-byte BASIC record. */
    for (int i = 0; i < count; i++) {
        const DiskDirEntry *e = &ents[i];
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
    disk_image_read_bam(d, name, sizeof(name), id, NULL, &free_blocks);
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

int disk_image_find_file(const DiskImage *d, const char *name, DiskDirEntry *entry) {
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

    DiskDirEntry entries[512];
    int count = disk_image_read_directory_entries(d, entries, 512);
    for (int i = 0; i < count; i++) {
        if (entries[i].type != 0 && filename_matches(pattern, entries[i].name)) {
            *entry = entries[i];
            return 0;
        }
    }
    return -1;
}

int disk_image_read_file(const DiskImage *d, const DiskDirEntry *entry, u8 *out, size_t cap) {
    if (!d || !entry || !out) return -1;
    int track = entry->start_track;
    int sector = entry->start_sector;
    bool visited[DISK_MAX_TRACKS + 1][DISK_MAX_SECTORS];
    memset(visited, 0, sizeof(visited));
    size_t length = 0;

    while (track != 0) {
        int sectors = disk_image_track_sectors(d, track);
        if (sectors == 0 || sector < 0 || sector >= sectors ||
            visited[track][sector])
            return -1;
        visited[track][sector] = true;

        u8 block[DISK_SECTOR_BYTES];
        if (disk_image_read_sector(d, track, sector, block) != 0) return -1;

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

static u8 *sector_in(const DiskImage *d, u8 *image, int track, int sector) {
    if (!image || track < 1 || track > d->tracks ||
        sector < 0 || sector >= disk_image_track_sectors(d, track)) return NULL;
    return image + disk_image_track_offset(d, track) +
           sector * DISK_SECTOR_BYTES;
}

/* D71's second-side free counts are at 18/0 but its bitmaps are at 53/0.
 * D81 has two six-byte-per-track BAM sectors, at 40/1 and 40/2. */
static bool bam_offsets(const DiskImage *d, int track,
                        size_t *count, size_t *bits) {
    if (track < 1 || track > d->tracks) return false;
    if (d->format == DISK_FORMAT_D81) {
        size_t off = (size_t)disk_image_track_offset(d, 40) +
            (size_t)(track <= 40 ? 1 : 2) * 256u + 0x10u +
            (size_t)((track - 1) % 40) * 6u;
        *count = off;
        *bits = off + 1;
    } else if (d->format == DISK_FORMAT_D71 && track > 35) {
        *count = (size_t)disk_image_track_offset(d, 18) +
                 0xDDu + (size_t)(track - 36);
        *bits = (size_t)disk_image_track_offset(d, 53) +
                (size_t)(track - 36) * 3u;
    } else {
        *count = (size_t)disk_image_track_offset(d, 18) +
                 4u + (size_t)(track - 1) * 4u;
        *bits = *count + 1;
    }
    return true;
}

static bool bam_free(const DiskImage *d, const u8 *image, int track, int sector) {
    size_t count, bits;
    if (!image || sector < 0 || sector >= disk_image_track_sectors(d, track) ||
        !bam_offsets(d, track, &count, &bits)) return false;
    return (image[bits + (size_t)sector / 8] & (1u << (sector & 7))) != 0;
}

static void bam_mark(const DiskImage *d, u8 *image, int track, int sector,
                     bool free_sector) {
    size_t count, bits;
    if (!bam_offsets(d, track, &count, &bits)) return;
    u8 bit = (u8)(1u << (sector & 7));
    if (free_sector) image[bits + (size_t)sector / 8] |= bit;
    else image[bits + (size_t)sector / 8] &= (u8)~bit;
    int free_count = 0;
    for (int s = 0; s < disk_image_track_sectors(d, track); ++s)
        if (bam_free(d, image, track, s)) ++free_count;
    image[count] = (u8)free_count;
}

static void sector_error_ok(const DiskImage *d, u8 *image, int track, int sector) {
    if (!d->has_errors) return;
    size_t error_index = (size_t)disk_image_track_offset(d, d->tracks + 1) +
        (size_t)disk_image_track_offset(d, track) / DISK_SECTOR_BYTES +
        (size_t)sector;
    if (error_index < d->size) image[error_index] = 1;
}

static bool name_equals(const u8 *slot, const char *name) {
    size_t n = strlen(name);
    for (size_t i = 0; i < 16; ++i) {
        u8 ch = slot[5 + i];
        if (i >= n) return ch == 0xA0;
        if (toupper((unsigned char)ch) != toupper((unsigned char)name[i]))
            return false;
    }
    return true;
}

static DiskSaveResult find_save_slot(const DiskImage *d, u8 *image,
                                    const char *name, bool replace,
                                    u8 **slot_out, u8 **tail_out) {
    bool seen[DISK_MAX_SECTORS] = { false };
    u8 *free_slot = NULL;
    int track = directory_track(d), sector = directory_start(d);
    int sectors = disk_image_track_sectors(d, track);
    for (int chain = 0; chain < sectors; ++chain) {
        if (sector < directory_start(d) || sector >= sectors || seen[sector])
            return DISK_SAVE_DIR_ERROR;
        seen[sector] = true;
        u8 *dir = sector_in(d, image, track, sector);
        if (!dir) return DISK_SAVE_DIR_ERROR;
        for (int i = 0; i < 8; ++i) {
            u8 *slot = dir + i * 32;
            if (slot[2] == 0 || slot[2] == 0xFF) {
                if (!free_slot) free_slot = slot;
            } else if (name_equals(slot, name)) {
                if (!replace) return DISK_SAVE_EXISTS;
                if (slot[2] & 0x40) return DISK_SAVE_WRITE_PROTECT;
                /* REL side sectors and D81 CBM partitions are not PRG chains.
                 * Never free or overwrite structures we cannot reconstruct. */
                if ((slot[2] & 0x07) != 2) return DISK_SAVE_TYPE_MISMATCH;
                *slot_out = slot;
                *tail_out = dir;
                return DISK_SAVE_OK;
            }
        }
        if (dir[0] == 0) {
            *slot_out = free_slot;
            *tail_out = dir;
            return DISK_SAVE_OK;
        }
        if (dir[0] != track) return DISK_SAVE_DIR_ERROR;
        sector = dir[1];
    }
    return DISK_SAVE_DIR_ERROR;
}

static bool free_file_chain(const DiskImage *d, u8 *image,
                            int track, int sector) {
    bool seen[DISK_MAX_TRACKS + 1][DISK_MAX_SECTORS] = { { false } };
    for (int count = 0; track != 0 && count < 3200; ++count) {
        if (track == directory_track(d) ||
            (d->format == DISK_FORMAT_D71 && track == 53) ||
            !sector_in(d, image, track, sector) ||
            seen[track][sector]) return false;
        if (bam_free(d, image, track, sector)) return false;
        seen[track][sector] = true;
        u8 *block = sector_in(d, image, track, sector);
        int next_track = block[0], next_sector = block[1];
        bam_mark(d, image, track, sector, true);
        track = next_track;
        sector = next_sector;
    }
    return track == 0;
}

static int persist_image(const DiskImage *d, const u8 *image) {
    struct stat st;
    if (!d->path || !path_writable_regular(d->path) ||
        stat(d->path, &st) != 0 || (size_t)st.st_size != d->size)
        return -1;
    /* Refuse to replace a disk changed by another program since attach. */
    FILE *current = fopen(d->path, "rb");
    if (!current) return -1;
    u8 check[4096];
    size_t offset = 0;
    int matches = 1;
    while (offset < d->size) {
        size_t n = d->size - offset;
        if (n > sizeof(check)) n = sizeof(check);
        if (fread(check, 1, n, current) != n ||
            memcmp(check, d->data + offset, n) != 0) { matches = 0; break; }
        offset += n;
    }
    if (fclose(current) != 0 || !matches) return -1;
    size_t path_len = strlen(d->path);
#ifdef _WIN32
    char *temp = malloc(path_len + 32);
    if (!temp) return -1;
    HANDLE out = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        snprintf(temp, path_len + 32, "%s.tmp%lu.%u", d->path,
                 (unsigned long)GetCurrentProcessId(), attempt);
        out = CreateFileA(temp, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                          FILE_ATTRIBUTE_NORMAL, NULL);
        if (out != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS)
            break;
    }
    if (out == INVALID_HANDLE_VALUE) { free(temp); return -1; }
    size_t written = 0;
    int ok = 1;
    while (written < d->size && ok) {
        DWORD chunk = (DWORD)(d->size - written);
        DWORD count = 0;
        ok = WriteFile(out, image + written, chunk, &count, NULL) && count == chunk;
        written += count;
    }
    if (ok) ok = FlushFileBuffers(out) != 0;
    if (!CloseHandle(out)) ok = 0;
    if (ok) ok = MoveFileExA(temp, d->path,
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileA(temp);
    free(temp);
    return ok ? 0 : -1;
#else
    char *temp = malloc(path_len + sizeof(".tmpXXXXXX"));
    if (!temp) return -1;
    memcpy(temp, d->path, path_len);
    memcpy(temp + path_len, ".tmpXXXXXX", sizeof(".tmpXXXXXX"));
    int fd = mkstemp(temp);
    if (fd < 0) { free(temp); return -1; }
    int ok = fchmod(fd, st.st_mode & 0777) == 0;
    FILE *out = fdopen(fd, "wb");
    if (!out) { close(fd); unlink(temp); free(temp); return -1; }
    if (ok) ok = fwrite(image, 1, d->size, out) == d->size;
    if (ok) ok = fflush(out) == 0;
    if (ok) ok = fsync(fd) == 0;
    if (fclose(out) != 0) ok = 0;
    if (ok) ok = rename(temp, d->path) == 0;
    if (!ok) unlink(temp);
    free(temp);
    return ok ? 0 : -1;
#endif
}

DiskSaveResult disk_image_save_prg(DiskImage *d, const char *name, const u8 *data,
                          size_t length, bool replace) {
    if (!d || !d->data || !d->writable)
        return DISK_SAVE_WRITE_PROTECT;
    if (!data || !name) return DISK_SAVE_BAD_NAME;
    size_t name_len = strlen(name);
    if (!name_len || name_len > 16 || length < 2 ||
        strchr(name, '*') || strchr(name, '?') || strchr(name, '/') ||
        strchr(name, ':') || strchr(name, ',')) return DISK_SAVE_BAD_NAME;
    size_t total_sectors = (size_t)disk_image_track_offset(d, d->tracks + 1) /
                           DISK_SECTOR_BYTES;
    if (length > total_sectors * 254u) return DISK_SAVE_DISK_FULL;

    u8 *image = malloc(d->size);
    if (!image) return DISK_SAVE_IO_ERROR;
    memcpy(image, d->data, d->size);
    int dir_track = directory_track(d);
    int first_dir_sector = directory_start(d);
    u8 *header = sector_in(d, image, dir_track, 0);
    if (d->format != DISK_FORMAT_D64 &&
        header[2] != 0 &&
        header[2] != (d->format == DISK_FORMAT_D71 ? 'A' : 'D')) {
        free(image);
        return DISK_SAVE_DOS_MISMATCH;
    }
    for (int s = 0; s <= first_dir_sector; ++s) {
        if (bam_free(d, image, dir_track, s)) {
            free(image);
            return DISK_SAVE_DIR_ERROR;
        }
    }
    if (d->format == DISK_FORMAT_D71 && bam_free(d, image, 53, 0)) {
        free(image);
        return DISK_SAVE_DIR_ERROR;
    }
    u8 *slot = NULL, *tail = NULL;
    DiskSaveResult result = find_save_slot(d, image, name, replace, &slot, &tail);
    if (result != DISK_SAVE_OK) goto done;

    if (slot && slot[2] != 0 && slot[2] != 0xFF) {
        if (!free_file_chain(d, image, slot[3], slot[4])) {
            result = DISK_SAVE_DIR_ERROR;
            goto done;
        }
    }
    if (!slot) {
        for (int s = first_dir_sector + 1;
             s < disk_image_track_sectors(d, dir_track); ++s) {
            if (bam_free(d, image, dir_track, s)) {
                bam_mark(d, image, dir_track, s, false);
                sector_error_ok(d, image, dir_track, s);
                slot = sector_in(d, image, dir_track, s);
                memset(slot, 0, DISK_SECTOR_BYTES);
                slot[0] = 0; slot[1] = 0xFF;
                tail[0] = (u8)dir_track; tail[1] = (u8)s;
                break;
            }
        }
        if (!slot) { result = DISK_SAVE_DIR_ERROR; goto done; }
    }

    size_t blocks = (length + 253u) / 254u;
    if (blocks > 3200) { result = DISK_SAVE_DISK_FULL; goto done; }
    int tracks[3200], sectors[3200];
    size_t allocated = 0;
    for (int t = 1; t <= d->tracks && allocated < blocks; ++t) {
        if (t == dir_track || (d->format == DISK_FORMAT_D71 && t == 53))
            continue;
        for (int s = 0; s < disk_image_track_sectors(d, t) &&
             allocated < blocks; ++s) {
            if (!bam_free(d, image, t, s)) continue;
            tracks[allocated] = t;
            sectors[allocated++] = s;
            bam_mark(d, image, t, s, false);
            sector_error_ok(d, image, t, s);
        }
    }
    if (allocated != blocks) { result = DISK_SAVE_DISK_FULL; goto done; }

    for (size_t i = 0; i < blocks; ++i) {
        u8 *block = sector_in(d, image, tracks[i], sectors[i]);
        memset(block, 0, DISK_SECTOR_BYTES);
        size_t offset = i * 254u;
        size_t bytes = length - offset;
        if (bytes > 254) bytes = 254;
        block[0] = i + 1 < blocks ? (u8)tracks[i + 1] : 0;
        block[1] = i + 1 < blocks ? (u8)sectors[i + 1] : (u8)(bytes + 1);
        memcpy(block + 2, data + offset, bytes);
    }

    /* Slot zero overlaps the directory-sector link in bytes 0-1. */
    slot[2] = 0x82; /* closed PRG */
    slot[3] = (u8)tracks[0];
    slot[4] = (u8)sectors[0];
    memset(slot + 5, 0xA0, 16);
    for (size_t i = 0; i < name_len; ++i)
        slot[5 + i] = (u8)toupper((unsigned char)name[i]);
    memset(slot + 21, 0, 9);
    slot[30] = (u8)blocks;
    slot[31] = (u8)(blocks >> 8);
    sector_error_ok(d, image, dir_track, 0);
    if (d->format == DISK_FORMAT_D71) sector_error_ok(d, image, 53, 0);
    if (d->format == DISK_FORMAT_D81) {
        sector_error_ok(d, image, 40, 1);
        sector_error_ok(d, image, 40, 2);
    }
    u8 *dir_base = sector_in(d, image, dir_track, 0);
    sector_error_ok(d, image, dir_track,
                    (int)((slot - dir_base) / DISK_SECTOR_BYTES));
    sector_error_ok(d, image, dir_track,
                    (int)((tail - dir_base) / DISK_SECTOR_BYTES));

    if (persist_image(d, image) != 0) { result = DISK_SAVE_IO_ERROR; goto done; }
    free(d->data);
    d->data = image;
    return DISK_SAVE_OK;
done:
    free(image);
    return result;
}
