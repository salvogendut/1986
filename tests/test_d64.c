#include "d64.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

static u8 image[174848];

/* Build a single-sided 35-track D64 with a BAM header and one directory entry. */
static void build_d64(D64 *d) {
    memset(image, 0, sizeof(image));
    d->data = image;
    d->size = sizeof(image);
    d->tracks = 35;
    d->has_errors = false;

    /* BAM at track 18, sector 0: disk name, ID, DOS version. */
    int bam_off = d64_track_offset(18);
    u8 *bam = image + bam_off;
    const char *dn = "DISK";
    for (int i = 0; i < 16; i++) bam[0x90 + i] = (i < 4) ? (u8)dn[i] : 0xA0;
    bam[0xA2] = '0'; bam[0xA3] = '1';
    bam[0xA4] = 0xA0;
    bam[0xA5] = '2'; bam[0xA6] = 'A';

    /* Directory at track 18, sector 1. Bytes 0-1 = next-sector link (end). */
    int dir_off = d64_track_offset(18) + 1 * D64_SECTOR_BYTES;
    u8 *dir = image + dir_off;
    dir[0] = 0x00; dir[1] = 0xFF;

    /* Slot 0: "HELLO" PRG, closed, 5 blocks, not locked. */
    u8 *e = dir + 0;
    e[2] = 0x82;                       /* type 2 (PRG) | closed (0x80) */
    e[3] = 17; e[4] = 1;
    const char *nm = "HELLO";
    for (int i = 0; i < 16; i++) e[5 + i] = (i < 5) ? (u8)nm[i] : 0xA0;
    e[30] = 5; e[31] = 0;              /* 5 blocks */

    /* Slot 1: "DATA" SEQ, closed, locked, 12 blocks. */
    u8 *e2 = dir + 32;
    e2[2] = 0xC1;                      /* type 1 (SEQ) | closed | locked (0x40) */
    e2[3] = 17; e2[4] = 2;
    const char *nm2 = "DATA";
    for (int i = 0; i < 16; i++) e2[5 + i] = (i < 4) ? (u8)nm2[i] : 0xA0;
    e2[30] = 12; e2[31] = 0;           /* 12 blocks */
}

int main(void) {
    D64 d;
    build_d64(&d);

    D64DirEntry ents[16];
    int n = d64_read_directory_entries(&d, ents, 16);
    CHECK(n == 2, "two entries read");

    CHECK(ents[0].blocks == 5, "entry 0 block count");
    CHECK(ents[0].type == 2, "entry 0 type PRG");
    CHECK(ents[0].closed == true, "entry 0 closed");
    CHECK(ents[0].locked == false, "entry 0 unlocked");
    CHECK(strcmp(ents[0].name, "HELLO") == 0, "entry 0 name");

    CHECK(ents[1].blocks == 12, "entry 1 block count");
    CHECK(ents[1].type == 1, "entry 1 type SEQ");
    CHECK(ents[1].closed == true, "entry 1 closed");
    CHECK(ents[1].locked == true, "entry 1 locked");
    CHECK(strcmp(ents[1].name, "DATA") == 0, "entry 1 name");

    /* Block-count is read from offset 30-31 (not 4-5, the old bug). */
    CHECK(ents[0].blocks != (image[d64_track_offset(18) + 1 * 256 + 4] |
                            (image[d64_track_offset(18) + 1 * 256 + 5] << 8)),
          "block count not from bytes 4-5");

    if (failures == 0) { printf("test-d64: OK\n"); return 0; }
    printf("test-d64: %d failure(s)\n", failures);
    return 1;
}
