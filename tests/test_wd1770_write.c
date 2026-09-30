#define _POSIX_C_SOURCE 200809L
#include "wd1770.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(ok, msg) do { if (!(ok)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; \
} } while (0)

static bool wait_drq(Wd1770 *f) {
    for (unsigned i = 0; i < 70000; ++i) {
        u8 status = wd1770_read(f, 0);
        if (status & WD1770_DRQ) return true;
        if (!(status & WD1770_BUSY)) return false;
        wd1770_tick(f, WD1770_BYTE_CYCLES);
    }
    return false;
}

static bool put_bytes(Wd1770 *f, const u8 *bytes, unsigned n) {
    for (unsigned i = 0; i < n; ++i) {
        if (!wait_drq(f)) return false;
        wd1770_write(f, 3, bytes[i]);
        CHECK(!(f->status & WD1770_DRQ), "data write acknowledges DRQ");
    }
    return true;
}

static void start(Wd1770 *f, unsigned track, unsigned side, unsigned sector, u8 cmd) {
    wd1770_write(f, 0, 0xd0);
    f->head_track = track - 1;
    wd1770_set_side(f, side ^ 1);
    wd1770_set_motor(f, true);
    wd1770_write(f, 1, (u8)(track - 1));
    wd1770_write(f, 2, (u8)sector);
    wd1770_write(f, 0, cmd);
}

static size_t offset(unsigned track, unsigned side, unsigned sector) {
    return ((track - 1) * 40 + side * 20 + (sector - 1) * 2) * 256u;
}

static void assert_image(const DiskImage *image, const u8 *expected, const char *label) {
    CHECK(!memcmp(image->data, expected, image->size), label);
    DiskImage reopened = {0};
    bool ok = disk_image_open(&reopened, image->path) == 0 &&
              reopened.size == image->size && !memcmp(reopened.data, expected, image->size);
    CHECK(ok, "fresh host open matches expected complete disk image");
    disk_image_close(&reopened);
}

int main(void) {
    char path[] = "/tmp/1986-wd-write-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 1;
    close(fd);
    DiskImage image = {0};
    if (disk_image_create_blank(path, DISK_FORMAT_D81) != DISK_SAVE_OK ||
        disk_image_open(&image, path)) { unlink(path); return 1; }
    u8 *expected = malloc(image.size);
    if (!expected) { disk_image_close(&image); unlink(path); return 1; }
    memcpy(expected, image.data, image.size);
    Wd1770 f;
    wd1770_init(&f);
    CHECK(wd1770_attach(&f, &image), "writable D81 attaches");
    CHECK(!wd1770_write_protected(&f) && !(wd1770_read(&f, 0) & WD1770_WP),
          "Type I status and protection sensor match writable medium");
    u8 bytes[512];
    const unsigned tracks[] = {1, 40, 80};
    for (unsigned t = 0; t < 3; ++t) {
        for (unsigned side = 0; side < 2; ++side) {
            for (unsigned sector = 1; sector <= 10; sector += 9) {
                for (unsigned i = 0; i < 512; ++i)
                    bytes[i] = (u8)(i * 7 + tracks[t] + side * 23 + sector * 11 + (i >> 8));
                start(&f, tracks[t], side, sector, 0xa8);
                CHECK(put_bytes(&f, bytes, 512), "single-sector command accepts 512 bytes through DRQ");
                CHECK(!memcmp(image.data, expected, image.size), "uncompleted transfer does not mutate live media");
                wd1770_tick(&f, f.delay); /* last data byte */
                wd1770_tick(&f, f.delay - 1);
                CHECK(!memcmp(image.data, expected, image.size) && (f.status & WD1770_BUSY),
                      "write persists only after both CRC bytes and final gap");
                wd1770_tick(&f, 1);
                CHECK(f.irq && !(wd1770_read(&f, 0) & 0x7f), "completed write reports success and IRQ");
                memcpy(expected + offset(tracks[t], side, sector), bytes, 512);
                assert_image(&image, expected, "write changes only the addressed physical sector");
                start(&f, tracks[t], side, sector, 0x88);
                bool same = true;
                for (unsigned i = 0; i < 512; ++i) {
                    if (!wait_drq(&f) || wd1770_read(&f, 3) != bytes[i]) { same = false; break; }
                }
                wd1770_tick(&f, 256);
                CHECK(same, "read sector returns newly persisted data");
            }
        }
    }
    CHECK(f.sectors_written == 12 && f.write_bytes == 12 * 512,
          "write counters count physical bytes and completed sectors");

    memset(bytes, 0x96, sizeof(bytes));
    start(&f, 2, 0, 9, 0xb8);
    CHECK(put_bytes(&f, bytes, 512), "multi-write accepts sector 9");
    wd1770_tick(&f, 256);
    memcpy(expected + offset(2, 0, 9), bytes, 512);
    memset(bytes, 0x69, sizeof(bytes));
    CHECK(put_bytes(&f, bytes, 512), "multi-write advances to sector 10");
    wd1770_tick(&f, 2100000);
    memcpy(expected + offset(2, 0, 10), bytes, 512);
    CHECK(f.sector == 11 && (f.status & WD1770_RNF) && !(f.status & WD1770_BUSY),
          "multi-write stops at missing sector 11 without wrapping sides");
    assert_image(&image, expected, "multi-write commits both addressed sectors");

    /* None of these interrupted commands may persist even a 256-byte half. */
    for (unsigned abort = 0; abort < 8; ++abort) {
        start(&f, 3, 0, 1, 0xa8);
        CHECK(put_bytes(&f, bytes, abort == 7 ? 512 : 300), "sector staged for abort test");
        switch (abort) {
            case 0: wd1770_write(&f, 0, 0xd8); break;
            case 1: wd1770_set_motor(&f, false); break;
            case 2: wd1770_set_side(&f, 0); break;
            case 3: wd1770_reset(&f); break;
            case 4: wd1770_attach(&f, NULL); wd1770_attach(&f, &image); break;
            case 5: wd1770_tick(&f, 1000); CHECK(f.status & WD1770_LOST, "missed byte reports lost data"); break;
            case 6:
                image.writable = false;
                CHECK(put_bytes(&f, bytes, 212), "remaining bytes before protection change takes effect");
                wd1770_tick(&f, 256);
                CHECK(f.status & WD1770_WP, "protection is rechecked before persistence");
                image.writable = true;
                break;
            case 7:
                wd1770_tick(&f, f.delay); /* final data byte, before CRC commit */
                wd1770_write(&f, 0, 0xd8);
                wd1770_tick(&f, 1000);
                break;
        }
        CHECK(!(f.status & WD1770_BUSY), "abort terminates without hanging");
        assert_image(&image, expected, "aborted write leaves entire live/host image unchanged");
    }
    start(&f, 3, 0, 1, 0xa8);
    CHECK(wait_drq(&f), "first write DRQ is issued");
    wd1770_tick(&f, f.delay - 1);
    CHECK(f.status & WD1770_BUSY, "first byte has a nine-byte-time grace interval");
    wd1770_tick(&f, 1);
    CHECK((f.status & WD1770_LOST) && !(f.status & (WD1770_BUSY | WD1770_DRQ)),
          "missing first byte fails without changing the disk");

    start(&f, 3, 0, 1, 0xa8);
    CHECK(put_bytes(&f, bytes, 512), "staged write for latched-address test");
    wd1770_write(&f, 1, 79); wd1770_write(&f, 2, 10);
    wd1770_tick(&f, 256);
    memcpy(expected + offset(3, 0, 1), bytes, 512);
    assert_image(&image, expected, "register writes cannot redirect the sector already under the head");

    image.writable = false;
    start(&f, 3, 0, 1, 0xa8);
    wd1770_tick(&f, 100);
    CHECK((f.status & WD1770_WP) && !(f.status & (WD1770_BUSY | WD1770_DRQ)),
          "protected disk rejects write before requesting data");
    image.writable = true;
    const u8 unsupported[] = {0xf8, 0xa9};
    for (unsigned i = 0; i < sizeof(unsupported); ++i) {
        start(&f, 3, 0, 1, unsupported[i]);
        wd1770_tick(&f, 100);
        CHECK((f.status & WD1770_RNF) && !(f.status & WD1770_BUSY),
              "raw formatting/deleted-data writes explicitly fail on decoded D81");
    }
    CHECK(disk_image_write_d81_sector(&image, 81, 0, 1, bytes) == DISK_SAVE_IO_ERROR &&
          disk_image_write_d81_sector(&image, 1, 2, 1, bytes) == DISK_SAVE_IO_ERROR &&
          disk_image_write_d81_sector(&image, 1, 0, 0, bytes) == DISK_SAVE_IO_ERROR,
          "invalid physical coordinates never alias another sector");
    assert_image(&image, expected, "rejected commands do not modify data");

    /* Host changes after attachment, including same-value emulated writes. */
    FILE *host = fopen(path, "r+b");
    CHECK(host != NULL, "open disposable file for external edit");
    if (host) { fputc(expected[0] ^ 0xff, host); fclose(host); }
    for (unsigned identical = 0; identical < 2; ++identical) {
        if (identical) {
            CHECK(wd1770_write_protected(&f), "host failure inhibits subsequent writes");
            start(&f, 3, 0, 1, 0xa8);
            wd1770_tick(&f, 100);
            CHECK((f.status & WD1770_WP) && !(f.status & WD1770_BUSY) &&
                  f.write_error == DISK_SAVE_IO_ERROR, "write inhibit retains the original host failure");
            wd1770_reset(&f);
            memcpy(bytes, expected + offset(3, 0, 1), 512);
        }
        else memset(bytes, 0xc3, sizeof(bytes));
        start(&f, 3, 0, 1, 0xa8);
        CHECK(put_bytes(&f, bytes, 512), "transfer completes before host conflict is detected");
        wd1770_tick(&f, 256);
        CHECK((f.status & WD1770_WP) && !(f.status & WD1770_BUSY) &&
              f.write_error == DISK_SAVE_IO_ERROR, "external edit produces WD and host errors");
        CHECK(!memcmp(image.data, expected, image.size), "conflict never mutates live image");
        expected[0] ^= 0xff;
        DiskImage external = {0};
        CHECK(disk_image_open(&external, path) == 0 &&
              !memcmp(external.data, expected, image.size), "conflict preserves the external edit and both old sector halves");
        disk_image_close(&external);
        expected[0] ^= 0xff;
    }
    wd1770_attach(&f, NULL);
    disk_image_close(&image);
    CHECK(chmod(path, 0444) == 0 && disk_image_open(&image, path) == 0 && !image.writable,
          "host permission bits expose real write protection even under privileged tests");
    wd1770_attach(&f, &image);
    CHECK(wd1770_write_protected(&f), "read-only file reaches controller sensor");
    wd1770_attach(&f, NULL);
    disk_image_close(&image);
    chmod(path, 0600);
    unlink(path);
    free(expected);
    if (!failures) puts("WD1770 D81 write and persistence tests passed");
    return failures ? 1 : 0;
}
