#include "d64.h"
#include "virtual_drive.h"
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
    memset(d, 0, sizeof(*d));
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
    /* Three free sectors on track 1; track 18 is intentionally excluded. */
    bam[4] = 3;
    bam[5] = 0x07;
    bam[4 + 17 * 4] = 1;
    bam[5 + 17 * 4] = 0x01;

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

    /* HELLO is a one-sector PRG: load address $1C01 + three data bytes. The
     * final link byte stores data length + 1. */
    u8 *hello = image + d64_track_offset(17) + 1 * D64_SECTOR_BYTES;
    hello[0] = 0;
    hello[1] = 6;
    hello[2] = 0x01; hello[3] = 0x1C;
    hello[4] = 0xAA; hello[5] = 0xBB; hello[6] = 0xCC;

    /* DATA spans a full sector and a three-byte final sector. */
    u8 *data1 = image + d64_track_offset(17) + 2 * D64_SECTOR_BYTES;
    data1[0] = 17; data1[1] = 3;
    for (int i = 0; i < 254; i++) data1[2 + i] = (u8)i;
    u8 *data2 = image + d64_track_offset(17) + 3 * D64_SECTOR_BYTES;
    data2[0] = 0; data2[1] = 4;
    data2[2] = 0xDE; data2[3] = 0xAD; data2[4] = 0xBE;
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
    CHECK(ents[0].start_track == 17 && ents[0].start_sector == 1,
          "entry exposes first file sector");

    D64DirEntry found;
    CHECK(d64_find_file(&d, "hello", &found) == 0,
          "filename lookup is case insensitive");
    CHECK(strcmp(found.name, "HELLO") == 0, "found expected file");
    CHECK(d64_find_file(&d, "0:HE*", &found) == 0,
          "filename lookup supports drive prefix and wildcard");
    CHECK(d64_find_file(&d, "MISSING", &found) != 0,
          "missing filename is rejected");

    u8 file_data[300];
    int file_len = d64_read_file(&d, &ents[0], file_data, sizeof(file_data));
    CHECK(file_len == 5, "single-sector PRG length");
    CHECK(file_data[0] == 0x01 && file_data[1] == 0x1C &&
          file_data[2] == 0xAA && file_data[4] == 0xCC,
          "single-sector PRG includes load address and payload");
    file_len = d64_read_file(&d, &ents[1], file_data, sizeof(file_data));
    CHECK(file_len == 257, "multi-sector file length");
    CHECK(file_data[253] == 253 && file_data[254] == 0xDE &&
          file_data[256] == 0xBE, "multi-sector chain contents");
    CHECK(d64_read_file(&d, &ents[1], file_data, 256) < 0,
          "short file output buffer is rejected");

    char disk_name[17], id[2];
    int free_blocks = -1;
    CHECK(d64_read_bam(&d, disk_name, sizeof(disk_name), id, NULL,
                       &free_blocks) == 0, "BAM read");
    CHECK(free_blocks == 3, "free blocks use all bitmap bytes and exclude track 18");

    u8 listing[256];
    size_t listing_len = d64_build_directory_program(&d, listing, sizeof(listing));
    CHECK(listing_len == 32 + 2 * 32 + 31, "fixed-width directory stream length");
    CHECK(d64_build_directory_program(&d, listing, listing_len - 1) == 0,
          "short directory output buffer is rejected");
    CHECK(listing[0] == 0x01 && listing[1] == 0x04, "directory load address");
    CHECK(listing[31] == 0, "header ends at byte 31");
    CHECK(listing[32 + 2] == 5 && listing[32 + 3] == 0,
          "first record starts at byte 32");
    CHECK(listing[64 + 2] == 12 && listing[64 + 3] == 0,
          "second record starts at byte 64");
    CHECK(memcmp(listing + 96 + 4, "BLOCKS FREE.", 12) == 0,
          "trailing free-block record starts at byte 96");
    CHECK(listing[listing_len - 1] == 0, "directory stream has final terminator");

    /* Exercise the command-level IEC channel lifecycle used by ROM traps. */
    VirtualDrive vdrive;
    virtual_drive_init(&vdrive, 8);
    virtual_drive_attach(&vdrive, &d);
    virtual_drive_attention(&vdrive, 0x28); /* LISTEN 8 */
    virtual_drive_attention(&vdrive, 0xF2); /* OPEN channel 2 */
    virtual_drive_send(&vdrive, '$');
    virtual_drive_attention(&vdrive, 0x3F); /* UNLISTEN */
    virtual_drive_attention(&vdrive, 0x48); /* TALK 8 */
    virtual_drive_attention(&vdrive, 0x62); /* channel 2 */

    u8 received[256];
    size_t received_len = 0;
    int receive_status = 0;
    do {
        receive_status = virtual_drive_receive(&vdrive, &received[received_len]);
        if (receive_status) received_len++;
    } while (receive_status == 1 && received_len < sizeof(received));
    CHECK(receive_status == 2, "directory ends with EOI");
    CHECK(received_len == listing_len, "IEC directory length");
    CHECK(memcmp(received, listing, listing_len) == 0, "IEC directory bytes");

    virtual_drive_attention(&vdrive, 0x5F);
    virtual_drive_attention(&vdrive, 0x28);
    virtual_drive_attention(&vdrive, 0xFD); /* OPEN direct-access channel 13 */
    virtual_drive_send(&vdrive, '#');
    virtual_drive_attention(&vdrive, 0x3F);
    CHECK(virtual_drive_take_bus_status(&vdrive) == 0,
          "direct-access buffer name is not treated as a missing file");

    virtual_drive_attention(&vdrive, 0x28); /* LISTEN 8 */
    virtual_drive_attention(&vdrive, 0xE2); /* CLOSE channel 2 */
    virtual_drive_attention(&vdrive, 0x3F);
    virtual_drive_attention(&vdrive, 0x48);
    virtual_drive_attention(&vdrive, 0x62);
    CHECK(virtual_drive_receive(&vdrive, &received[0]) == 0,
          "closed channel has no data");

    /* Ordinary OPEN channels stream the raw PRG, including its load address. */
    virtual_drive_attention(&vdrive, 0x5F);
    virtual_drive_attention(&vdrive, 0x28);
    virtual_drive_attention(&vdrive, 0xF0); /* OPEN channel 0 */
    const char *hello_name = "hello";
    for (const char *p = hello_name; *p; p++) virtual_drive_send(&vdrive, (u8)*p);
    virtual_drive_attention(&vdrive, 0x3F);
    virtual_drive_attention(&vdrive, 0x48);
    virtual_drive_attention(&vdrive, 0x60);
    received_len = 0;
    do {
        receive_status = virtual_drive_receive(&vdrive, &received[received_len]);
        if (receive_status) received_len++;
    } while (receive_status == 1);
    CHECK(receive_status == 2 && received_len == 5, "virtual PRG ends with EOI");
    CHECK(memcmp(received, (u8[]){ 0x01, 0x1C, 0xAA, 0xBB, 0xCC }, 5) == 0,
          "virtual PRG bytes");

    /* A failed open is reported through the DOS status channel. */
    virtual_drive_attention(&vdrive, 0x5F);
    virtual_drive_attention(&vdrive, 0x28);
    virtual_drive_attention(&vdrive, 0xF0);
    const char *missing_name = "MISSING";
    for (const char *p = missing_name; *p; p++) virtual_drive_send(&vdrive, (u8)*p);
    virtual_drive_attention(&vdrive, 0x3F);
    CHECK(virtual_drive_take_bus_status(&vdrive) == 0x02,
          "failed open reports IEC error status");
    virtual_drive_attention(&vdrive, 0x48);
    virtual_drive_attention(&vdrive, 0x60);
    CHECK(virtual_drive_receive(&vdrive, &received[0]) == -1,
          "missing file returns a serial error");
    virtual_drive_attention(&vdrive, 0x5F);
    virtual_drive_attention(&vdrive, 0x48);
    virtual_drive_attention(&vdrive, 0x6F);
    received_len = 0;
    do {
        receive_status = virtual_drive_receive(&vdrive, &received[received_len]);
        if (receive_status) received_len++;
    } while (receive_status == 1);
    CHECK(receive_status == 2, "DOS error status ends with EOI");
    CHECK(received_len >= 2 && received[0] == '6' && received[1] == '2',
          "missing file reports DOS error 62");

    virtual_drive_reset(&vdrive);
    virtual_drive_attention(&vdrive, 0x29); /* wrong device */
    virtual_drive_attention(&vdrive, 0xF2);
    virtual_drive_send(&vdrive, '$');
    virtual_drive_attention(&vdrive, 0x3F);
    virtual_drive_attention(&vdrive, 0x48);
    virtual_drive_attention(&vdrive, 0x62);
    CHECK(virtual_drive_receive(&vdrive, &received[0]) == 0,
          "other IEC device address is ignored");

    /* Block-count is read from offset 30-31 (not 4-5, the old bug). */
    CHECK(ents[0].blocks != (image[d64_track_offset(18) + 1 * 256 + 4] |
                            (image[d64_track_offset(18) + 1 * 256 + 5] << 8)),
          "block count not from bytes 4-5");

    if (failures == 0) { printf("test-d64: OK\n"); return 0; }
    printf("test-d64: %d failure(s)\n", failures);
    return 1;
}
