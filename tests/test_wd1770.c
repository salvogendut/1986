#include "wd1770.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c, msg) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; \
} } while (0)

static bool wait_drq(Wd1770 *f) {
    for (int n = 0; n < 70000; ++n) {
        u8 status = wd1770_read(f, 0);
        if (status & WD1770_DRQ) return true;
        if (!(status & WD1770_BUSY)) return false;
        wd1770_tick(f, 64);
    }
    return false;
}

static u8 pattern(int track, int block, int byte) {
    return (u8)(track * 17 + block * 3 + byte * 7 + (byte >> 4));
}

static void command(Wd1770 *f, u8 cmd, u8 track, u8 sector) {
    wd1770_write(f, 0, 0xd0);
    wd1770_write(f, 1, track);
    wd1770_write(f, 2, sector);
    wd1770_write(f, 0, cmd);
}

int main(void) {
    DiskImage image = { .format = DISK_FORMAT_D81, .tracks = 80, .size = 819200,
                        .writable = true }; /* still protected by this slice */
    image.data = malloc(image.size);
    if (!image.data) return 1;
    for (int t = 0; t < 80; ++t)
        for (int s = 0; s < 40; ++s)
            for (int b = 0; b < 256; ++b)
                image.data[(t * 40 + s) * 256 + b] = pattern(t, s, b);
    Wd1770 f;
    wd1770_init(&f);
    CHECK(wd1770_attach(&f, &image), "ordinary D81 attaches");
    CHECK(f.disk_changed, "insert latches disk-change");
    DiskImage bad = image;
    bad.format = DISK_FORMAT_D64;
    CHECK(!wd1770_attach(&f, &bad) && f.image == &image,
          "unsupported medium does not replace the current disk");
    bad = image; bad.size--;
    CHECK(!wd1770_attach(&f, &bad), "truncated D81 is rejected");
    wd1770_set_motor(&f, true);
    wd1770_write(&f, 0, 0x58); /* STEP IN, update, no spin-up wait */
    wd1770_tick(&f, 48 + 11999);
    CHECK(f.head_track == 0 && (wd1770_read(&f, 0) & WD1770_BUSY),
          "step does not complete before its 6 ms delay");
    wd1770_tick(&f, 1);
    CHECK(f.head_track == 1 && f.track == 1 && !f.disk_changed && f.irq,
          "step updates track, acknowledges change, signals completion");
    CHECK((wd1770_read(&f, 0) & WD1770_WP) && !f.irq,
          "Type I reports read-only medium and status read acknowledges IRQ");
    wd1770_write(&f, 0, 0x48); /* STEP IN without update */
    wd1770_tick(&f, 12048);
    CHECK(f.head_track == 2 && f.track == 1, "head and WD track register are distinct");
    wd1770_write(&f, 0, 0x08); /* RESTORE */
    wd1770_tick(&f, 24048);
    CHECK(f.head_track == 0 && f.track == 0, "restore homes the physical head");
    wd1770_write(&f, 3, 39);
    wd1770_write(&f, 0, 0x1c); /* SEEK with verify */
    wd1770_tick(&f, 48 + 39 * 12000 + 60000);
    CHECK(f.head_track == 39 && f.track == 39 && !(wd1770_read(&f, 0) & 0x11),
          "seek reaches the requested cylinder and verifies it");

    /* Every head/sector boundary, including both halves of each physical
     * sector. D81 block 0 is on physical head 1, not head 0. */
    const unsigned tracks[] = {0, 39, 79};
    for (unsigned t = 0; t < sizeof(tracks)/sizeof(tracks[0]); ++t) {
        f.head_track = tracks[t];
        for (unsigned side = 0; side < 2; ++side) {
            wd1770_set_side(&f, side);
            for (unsigned sector = 1; sector <= 10; ++sector) {
                command(&f, 0x88, (u8)tracks[t], (u8)sector);
                CHECK(wait_drq(&f), "read finds physical sector");
                bool match = true;
                for (unsigned byte = 0; byte < 512; ++byte) {
                    if (!(wd1770_read(&f, 0) & WD1770_DRQ)) match = false;
                    u8 actual = wd1770_read(&f, 3);
                    unsigned block = (side ^ 1) * 20 + (sector - 1) * 2 + byte / 256;
                    if (actual != pattern((int)tracks[t], (int)block, (int)(byte % 256)))
                        match = false;
                    CHECK(!(f.status & WD1770_DRQ), "data read acknowledges DRQ");
                    wd1770_tick(&f, 64);
                }
                wd1770_tick(&f, 64);
                CHECK(match, "D81 track/head/sector mapping and both 256-byte halves");
                CHECK(f.irq && !(wd1770_read(&f, 0) & 0x1f),
                      "512-byte read completes without busy/lost/CRC/RNF flags");
            }
        }
    }
    CHECK(f.sectors_read == 60 && f.read_bytes == 60 * 512,
          "monitor counters follow actual bytes and completed sectors");

    command(&f, 0x88, 79, 1);
    CHECK(wait_drq(&f), "lost-data test starts a transfer");
    u8 first = f.data;
    wd1770_tick(&f, 64);
    CHECK((wd1770_read(&f, 0) & WD1770_LOST) && f.data != first,
          "unserviced DRQ loses a byte instead of stalling the disk clock");
    wd1770_write(&f, 0, 0xd8);
    CHECK(f.irq && !(f.status & (WD1770_BUSY | WD1770_DRQ)),
          "force interrupt cancels transfer immediately");
    wd1770_read(&f, 0);
    CHECK(!f.irq, "status read acknowledges force IRQ");

    command(&f, 0x88, 78, 1);
    wd1770_tick(&f, 2100000);
    CHECK(!(f.status & WD1770_BUSY) && (f.status & WD1770_RNF),
          "wrong track register produces RNF after bounded search");
    command(&f, 0x88, 79, 11);
    wd1770_tick(&f, 2100000);
    CHECK(!(f.status & WD1770_BUSY) && (f.status & WD1770_RNF),
          "invalid sector does not alias into next D81 side/track");

    command(&f, 0x98, 79, 10); /* multiple-sector read cannot wrap to sector 1 */
    CHECK(wait_drq(&f), "multiple read starts at sector 10");
    for (int byte = 0; byte < 512; ++byte) {
        wd1770_read(&f, 3); wd1770_tick(&f, 64);
    }
    wd1770_tick(&f, 2100000);
    CHECK(f.sector == 11 && (f.status & WD1770_RNF) && !(f.status & WD1770_BUSY),
          "multi-sector increments ID and stops at missing sector 11");

    command(&f, 0xc8, 79, 0);
    CHECK(wait_drq(&f), "read-address locates an ID field");
    u8 id[6];
    for (int i = 0; i < 6; ++i) { id[i] = wd1770_read(&f, 3); wd1770_tick(&f, 64); }
    wd1770_tick(&f, 64);
    CHECK(id[0] == 79 && id[1] == 0 && id[2] >= 1 && id[2] <= 10 && id[3] == 2,
          "address ID reports cylinder, inverted side, sector and 512-byte size");
    CHECK(f.sector == 79 && !(f.status & WD1770_BUSY), "read-address updates sector register");
    u16 crc = 0xb230;
    for (int i = 0; i < 6; ++i) {
        crc ^= (u16)id[i] << 8;
        for (int j = 0; j < 8; ++j)
            crc = (u16)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    CHECK(crc == 0, "read-address supplies valid MFM ID CRC bytes");

    command(&f, 0x88, 79, 1);
    CHECK(wait_drq(&f), "read active before eject");
    wd1770_attach(&f, NULL);
    CHECK(f.disk_changed && !(f.status & (WD1770_BUSY | WD1770_DRQ)) &&
          (f.status & WD1770_RNF), "eject aborts an active buffered read");
    wd1770_attach(&f, &image);
    command(&f, 0x88, 79, 1);
    CHECK(wait_drq(&f), "replacement image is readable");
    wd1770_set_motor(&f, false);
    CHECK(!(f.status & WD1770_BUSY) && (f.status & WD1770_RNF),
          "motor off cannot keep delivering buffered disk data");
    wd1770_set_motor(&f, true);

    command(&f, 0xa8, 79, 1);
    wd1770_tick(&f, 100);
    CHECK((f.status & WD1770_WP) && !(f.status & WD1770_BUSY),
          "sector writes explicitly report write protection");
    command(&f, 0xf8, 79, 1);
    wd1770_tick(&f, 100);
    CHECK((f.status & WD1770_WP) && !(f.status & WD1770_BUSY),
          "format/write-track cannot modify media");
    bool unchanged = true;
    for (int t = 0; t < 80; ++t)
        for (int s = 0; s < 40; ++s)
            for (int b = 0; b < 256; ++b)
                if (image.data[(t * 40 + s) * 256 + b] != pattern(t, s, b)) unchanged = false;
    CHECK(unchanged, "all operations preserve the complete source image");
    command(&f, 0xe8, 79, 1);
    wd1770_tick(&f, 100);
    CHECK((f.status & WD1770_RNF) && !(f.status & WD1770_BUSY),
          "unsupported raw-track read fails instead of hanging or inventing data");

    /* Controller advancement must be invariant under CPU instruction slicing. */
    Wd1770 a, b;
    wd1770_init(&a); wd1770_attach(&a, &image); wd1770_set_motor(&a, true);
    b = a;
    command(&a, 0x88, 0, 4); command(&b, 0x88, 0, 4);
    wd1770_tick(&a, 456789);
    for (int i = 0; i < 456789; ++i) wd1770_tick(&b, 1);
    CHECK(a.status == b.status && a.data == b.data && a.rotation == b.rotation &&
          a.cycles == b.cycles && a.read_bytes == b.read_bytes && a.phase == b.phase,
          "batched and single-cycle advancement produce identical state");
    wd1770_reset(&f);
    CHECK(f.image == &image && f.head_track == 79 && !(f.status & WD1770_BUSY),
          "reset keeps medium/head position but cancels controller state");
    free(image.data);
    if (!failures) puts("WD1770 read-only D81 tests passed");
    return failures ? 1 : 0;
}
