#define _POSIX_C_SOURCE 200809L
#include "gcr_drive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(ok, label) do { if (!(ok)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, label); \
    failures++; \
} } while (0)

static const u8 codes[16] = {
    0x0a, 0x0b, 0x12, 0x13, 0x0e, 0x0f, 0x16, 0x17,
    0x09, 0x19, 0x1a, 0x1b, 0x0d, 0x1d, 0x1e, 0x15
};

static void encode4(const u8 *in, u8 *out) {
    u64 word = 0;
    for (int i = 0; i < 4; ++i) {
        word = (word << 5) | codes[in[i] >> 4];
        word = (word << 5) | codes[in[i] & 15];
    }
    for (int i = 4; i >= 0; --i) { out[i] = (u8)word; word >>= 8; }
}

static int decode_nibble(u8 code) {
    for (int i = 0; i < 16; ++i) if (codes[i] == code) return i;
    return -1;
}

static bool decode4(const u8 *in, u8 *out) {
    u64 word = 0;
    for (int i = 0; i < 5; ++i) word = (word << 8) | in[i];
    for (int i = 0; i < 4; ++i) {
        int hi = decode_nibble((u8)((word >> (35 - i * 10)) & 31));
        int lo = decode_nibble((u8)((word >> (30 - i * 10)) & 31));
        if (hi < 0 || lo < 0) return false;
        out[i] = (u8)((hi << 4) | lo);
    }
    return true;
}

static bool make_disk(char *path, bool d71, bool errors) {
    int fd = mkstemp(path);
    if (fd < 0) return false;
    size_t side_size = (size_t)disk_image_d64_track_offset(36);
    size_t size = side_size * (d71 ? 2u : 1u) +
                  (errors ? (d71 ? 1366u : 683u) : 0u);
    u8 *data = calloc(1, size);
    if (!data) { close(fd); unlink(path); return false; }
    if (errors) memset(data + side_size * (d71 ? 2u : 1u), 0x20,
                       d71 ? 1366u : 683u);
    u8 *bam = data + disk_image_d64_track_offset(18);
    bam[0xa2] = 'A'; bam[0xa3] = 'B';
    if (d71) bam[3] = 0x80;
    data[0] = 0x31;
    if (d71) data[side_size] = 0x41;
    size_t done = 0;
    while (done < size) {
        ssize_t n = write(fd, data + done, size - done);
        if (n <= 0) break;
        done += (size_t)n;
    }
    free(data);
    bool okay = done == size && close(fd) == 0;
    if (!okay) unlink(path);
    return okay;
}

static void write_sector_data_field(GcrDrive *g, Via6522 *via, int sector,
                                    int value, bool valid) {
    /* Feed a complete encoded data field through byte-clocked VIA2 writes. */
    unsigned sector_start = 0;
    if (sector) {
        while (sector_start < g->track_length &&
               g->sector_for_pos[sector_start] != sector) sector_start++;
    }
    CHECK(sector_start + 29 + 325 <= g->track_length,
          "selected sector data field fits the circular track");
    unsigned field_pos = sector_start + 29;
    u8 encoded[325];
    if (valid) {
        u8 block[260];
        for (int i = 0; i < 65; ++i)
            CHECK(decode4(g->track_data + field_pos + i * 5, block + i * 4),
                  "decode original field for write fixture");
        block[1] = (u8)value;
        block[257] = 0;
        for (int i = 1; i <= 256; ++i) block[257] ^= block[i];
        for (int i = 0; i < 65; ++i)
            encode4(block + i * 4, encoded + i * 5);
    } else memset(encoded, 0, sizeof(encoded));

    gcr_drive_set_write_mode(g, true);
    via->pcr = 0x02; /* CB2 write, CA1 byte-ready enabled */
    g->byte_pos = field_pos;
    g->bit_budget = 0;
    g->write_shift = encoded[0];
    for (unsigned i = 0; i < sizeof(encoded); ++i) {
        gcr_drive_write_byte(g, encoded[i + 1 < sizeof(encoded) ? i + 1 : i]);
        unsigned previous = g->byte_pos;
        int ticks = 0;
        while (g->byte_pos == previous && ticks++ < 50)
            gcr_drive_tick(g, via, 1, false);
        CHECK(g->byte_pos == previous + 1,
              "one physical GCR byte advances per write-ready event");
    }
    via->pcr = 0x22;
    gcr_drive_set_write_mode(g, false);
}

static u8 file_byte(const char *path, size_t offset) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0xff;
    if (fseek(f, (long)offset, SEEK_SET)) { fclose(f); return 0xff; }
    int value = fgetc(f);
    fclose(f);
    return (u8)value;
}

int main(void) {
    char d64_path[] = "/tmp/1986-gcr-write-d64-XXXXXX";
    char d71_path[] = "/tmp/1986-gcr-write-d71-XXXXXX";
    CHECK(make_disk(d64_path, false, true), "create private D64 test copy");
    CHECK(make_disk(d71_path, true, false), "create private D71 test copy");

    DiskImage image;
    GcrDrive g;
    Via6522 via;
    CHECK(disk_image_open(&image, d64_path) == 0 && image.writable,
          "open writable D64 copy");
    gcr_drive_init(&g);
    via6522_init(&via);
    gcr_drive_attach(&g, &image);
    gcr_drive_set_port_b(&g, 0x64);
    write_sector_data_field(&g, &via, 0, 0x77, true);
    CHECK(!g.dirty && g.write_error == DISK_SAVE_OK && g.write_events == 325,
          "valid sector write is decoded and flushed");
    CHECK(image.data[0] == 0x77 && file_byte(d64_path, 0) == 0x77,
          "D64 write updates live and host image");
    CHECK(file_byte(d64_path, (size_t)disk_image_d64_track_offset(36)) == 1,
          "GCR write clears the sector error marker");
    CHECK(file_byte(d64_path, (size_t)disk_image_d64_track_offset(36) + 1) == 0x20,
          "untouched error-map sectors retain their status");
    write_sector_data_field(&g, &via, 20, 0x5a, true);
    CHECK(file_byte(d64_path, 20u * 256u) == 0x5a,
          "last sector on a 21-sector track survives write-back");

    image.writable = false;
    gcr_drive_update_via(&g, &via);
    CHECK(!(via.input_b & 0x10), "write-protect sensor goes active");
    write_sector_data_field(&g, &via, 0, 0x22, true);
    CHECK(!g.dirty && image.data[0] == 0x77 && file_byte(d64_path, 0) == 0x77,
          "write-protected disk ignores head writes");
    image.writable = true;

    /* An external editor must not be overwritten by a stale mounted image.
     * Repairing the external change allows the pending GCR write to retry. */
    u8 original_second = image.data[1];
    FILE *external = fopen(d64_path, "rb+");
    CHECK(external != NULL, "open fixture for external modification");
    if (external) {
        fseek(external, 1, SEEK_SET);
        fputc(original_second ^ 0xff, external);
        fclose(external);
    }
    write_sector_data_field(&g, &via, 0, 0x66, true);
    CHECK(g.dirty && g.write_error == DISK_SAVE_IO_ERROR &&
          image.data[0] == 0x77 && file_byte(d64_path, 1) != original_second,
          "concurrent host edit rejects write without changing live media");
    external = fopen(d64_path, "rb+");
    CHECK(external != NULL, "reopen fixture to resolve external conflict");
    if (external) {
        fseek(external, 1, SEEK_SET);
        fputc(original_second, external);
        fclose(external);
    }
    CHECK(gcr_drive_flush(&g) == DISK_SAVE_OK &&
          !g.dirty && file_byte(d64_path, 0) == 0x66,
          "pending GCR write succeeds after host conflict is resolved");

    write_sector_data_field(&g, &via, 0, 0, false);
    CHECK(g.dirty && g.write_error == DISK_SAVE_IO_ERROR &&
          file_byte(d64_path, 0) == 0x66,
          "malformed GCR sector is not committed as corrupted D64 data");
    gcr_drive_set_port_b(&g, 0x60);
    CHECK(!g.motor && g.dirty,
          "motor can stop while an unrepresentable track remains pending");
    unsigned before_side = g.side;
    gcr_drive_set_side(&g, 1);
    CHECK(g.side == before_side, "failed flush keeps pending track on same head");
    /* The deliberately malformed fixture has no representable D64 write. */
    g.dirty = false;
    g.dirty_sector_mask = 0;
    gcr_drive_attach(&g, NULL);
    disk_image_close(&image);

    CHECK(disk_image_open(&image, d71_path) == 0 && image.writable,
          "open writable D71 copy");
    gcr_drive_attach(&g, &image);
    gcr_drive_set_side(&g, 1);
    gcr_drive_set_port_b(&g, 0x64);
    write_sector_data_field(&g, &via, 0, 0x88, true);
    size_t side_size = (size_t)disk_image_d64_track_offset(36);
    CHECK(image.data[side_size] == 0x88 &&
          file_byte(d71_path, side_size) == 0x88 &&
          file_byte(d71_path, 0) == 0x31,
          "side 1 write maps to D71 track 36 without altering side 0");
    gcr_drive_attach(&g, NULL);
    disk_image_close(&image);
    unlink(d64_path);
    unlink(d71_path);
    if (!failures) puts("test-gcr-write: OK");
    return failures ? 1 : 0;
}
