#include "gcr_drive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(ok, label) do { if (!(ok)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, label); \
    failures++; \
} } while (0)

static int nibble(u8 code) {
    static const u8 table[16] = {
        0x0a, 0x0b, 0x12, 0x13, 0x0e, 0x0f, 0x16, 0x17,
        0x09, 0x19, 0x1a, 0x1b, 0x0d, 0x1d, 0x1e, 0x15
    };
    for (int i = 0; i < 16; ++i) if (table[i] == code) return i;
    return -1;
}

static int decode4(const u8 encoded[5], u8 decoded[4]) {
    u64 bits = 0;
    for (int i = 0; i < 5; ++i) bits = (bits << 8) | encoded[i];
    for (int i = 0; i < 4; ++i) {
        int hi = nibble((u8)((bits >> (35 - i * 10)) & 31));
        int lo = nibble((u8)((bits >> (30 - i * 10)) & 31));
        if (hi < 0 || lo < 0) return -1;
        decoded[i] = (u8)(hi * 16 + lo);
    }
    return 0;
}

static void inspect_sector(const GcrDrive *g, unsigned track,
                           u8 first, u8 id0, u8 id1) {
    u8 hdr[8], block[260];
    for (int i = 0; i < 5; ++i)
        CHECK(g->track_data[i] == 0xff && g->track_sync[i],
              "header sync mark");
    CHECK(decode4(g->track_data + 5, hdr) == 0 &&
          decode4(g->track_data + 10, hdr + 4) == 0,
          "header GCR decodes");
    CHECK(hdr[0] == 8 && hdr[1] == (u8)(track ^ id0 ^ id1) &&
          hdr[2] == 0 && hdr[3] == track &&
          hdr[4] == id1 && hdr[5] == id0,
          "header has disk ID, track, sector and checksum");
    for (int i = 0; i < 65; ++i)
        CHECK(decode4(g->track_data + 29 + i * 5, block + i * 4) == 0,
              "data GCR decodes");
    CHECK(block[0] == 7 && block[1] == first &&
          block[257] == first && block[258] == 0 && block[259] == 0,
          "data payload and XOR checksum");
}

int main(void) {
    size_t side_size = (size_t)disk_image_d64_track_offset(36);
    DiskImage image = {0};
    image.data = calloc(2, side_size);
    if (!image.data) return 1;
    image.size = 2 * side_size;
    image.format = DISK_FORMAT_D71;
    image.tracks = 70;
    int bam = disk_image_d64_track_offset(18);
    image.data[bam + 0xa2] = 'A';
    image.data[bam + 0xa3] = 'B';
    image.data[bam + 3] = 0x80; /* native double-sided 1571 format */
    image.data[0] = 0x5a;
    image.data[side_size] = 0xa5;

    GcrDrive g;
    Via6522 via;
    gcr_drive_init(&g);
    via6522_init(&via);
    gcr_drive_attach(&g, &image);
    CHECK(g.track_length == 7692 && g.half_track == 2,
          "track 1 has zone-3 length");
    inspect_sector(&g, 1, 0x5a, 'A', 'B');

    gcr_drive_update_via(&g, &via);
    CHECK(!(via.input_b & 0x10), "read-only image senses write protection");
    image.writable = true;
    gcr_drive_update_via(&g, &via);
    CHECK(via.input_b & 0x10, "writable image clears write protection");

    gcr_drive_set_side(&g, 1);
    inspect_sector(&g, 36, 0xa5, 'A', 'B');
    image.data[bam + 3] = 0;
    image.data[side_size + bam + 0xa2] = 'C';
    image.data[side_size + bam + 0xa3] = 'D';
    gcr_drive_attach(&g, &image);
    inspect_sector(&g, 1, 0xa5, 'C', 'D');
    image.data[bam + 3] = 0x80;
    gcr_drive_set_side(&g, 0);
    gcr_drive_set_port_b(&g, 0x65); /* motor + phase 1 + zone 3 */
    CHECK(g.half_track == 3, "stepper advances one half-track");
    gcr_drive_set_port_b(&g, 0x66);
    CHECK(g.half_track == 4 && g.track_length == 7692,
          "second step reaches physical track 2");
    gcr_drive_set_port_b(&g, 0x65);
    CHECK(g.half_track == 3, "reverse phase backs up one half-track");

    gcr_drive_attach(&g, NULL);
    gcr_drive_update_via(&g, &via);
    CHECK(via.input_b & 0x10, "no disk is not write-protected");
    gcr_drive_attach(&g, &image);
    gcr_drive_reset(&g);
    via6522_reset(&via);
    via.pcr = 0x22; /* read mode and byte-ready enabled */
    gcr_drive_set_port_b(&g, 0x64); /* motor + phase 0 + zone 3 */
    CHECK(!gcr_drive_tick(&g, &via, 20, false),
          "not yet a complete disk byte");
    CHECK(!gcr_drive_tick(&g, &via, 120, false),
          "sync run suppresses byte-ready");
    CHECK(gcr_drive_tick(&g, &via, 40, false) && g.byte_ready &&
          (via.ifr & 2), "data byte pulses CA1 and CPU SO request");
    CHECK(gcr_drive_read_byte(&g) != 0xff && !g.byte_ready,
          "port-A read acknowledges byte-ready");
    gcr_drive_set_port_b(&g, 0x60);
    CHECK(!gcr_drive_tick(&g, &via, 1000, false),
          "motor off stops byte stream");

    image.format = DISK_FORMAT_D64;
    image.tracks = 35;
    gcr_drive_attach(&g, &image);
    gcr_drive_set_side(&g, 1);
    CHECK(g.track_data[0] == 0x55 && !g.track_sync[0],
          "D64 has no formatted second side");
    free(image.data);
    if (failures) return 1;
    puts("test-gcr-drive: OK");
    return 0;
}
