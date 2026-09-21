#define _POSIX_C_SOURCE 200809L
#include "disk_image.h"
#include "virtual_drive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

static int make_disk(char *path, bool full, bool with_errors) {
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    size_t base = (size_t)disk_image_d64_track_offset(36);
    size_t size = base + (with_errors ? 683u : 0u);
    u8 *image = calloc(1, size);
    if (!image) { close(fd); return -1; }
    if (with_errors) memset(image + base, 0x20, 683);
    u8 *bam = image + disk_image_d64_track_offset(18);
    bam[0] = 18; bam[1] = 1;
    memcpy(bam + 0x90, "SAVE TEST", 9);
    memset(bam + 0x99, 0xA0, 7);
    bam[0xA2] = '0'; bam[0xA3] = '1';
    bam[0xA5] = '2'; bam[0xA6] = 'A';
    for (int t = 1; t <= 35; ++t) {
        u8 *entry = bam + 4 + (t - 1) * 4;
        for (int s = 0; s < disk_image_d64_track_sectors(t); ++s) {
            if (full && t != 18) continue;
            if (t == 18 && s < 2) continue;
            entry[1 + s / 8] |= (u8)(1u << (s & 7));
            ++entry[0];
        }
    }
    u8 *dir = bam + 256;
    dir[0] = 0; dir[1] = 0xFF;
    size_t done = 0;
    while (done < size) {
        ssize_t n = write(fd, image + done, size - done);
        if (n <= 0) { free(image); close(fd); return -1; }
        done += (size_t)n;
    }
    free(image);
    return close(fd);
}

static void send_open(VirtualDrive *v, const char *name) {
    virtual_drive_attention(v, 0x28);
    virtual_drive_attention(v, 0xF1);
    for (const char *p = name; *p; ++p) virtual_drive_send(v, (u8)*p);
    virtual_drive_attention(v, 0x3F);
}

static void send_data(VirtualDrive *v, const u8 *data, size_t size) {
    virtual_drive_attention(v, 0x28);
    virtual_drive_attention(v, 0x61);
    for (size_t i = 0; i < size; ++i) virtual_drive_send(v, data[i]);
    virtual_drive_attention(v, 0x3F);
}

static void send_close(VirtualDrive *v) {
    virtual_drive_attention(v, 0x28);
    virtual_drive_attention(v, 0xE1);
    virtual_drive_attention(v, 0x3F);
}

static void send_command(VirtualDrive *v, const char *command, bool via_open) {
    virtual_drive_attention(v, 0x28);
    virtual_drive_attention(v, via_open ? 0xFF : 0x6F);
    for (const char *p = command; *p; ++p) virtual_drive_send(v, (u8)*p);
    virtual_drive_attention(v, 0x3F);
}

int main(void) {
    char path[] = "/tmp/1986-d64-save-test-XXXXXX";
    char full_path[] = "/tmp/1986-d64-save-full-XXXXXX";
    char dir_path[] = "/tmp/1986-d64-save-dir-XXXXXX";
    char errors_path[] = "/tmp/1986-d64-save-errors-XXXXXX";
    CHECK(make_disk(path, false, false) == 0, "make writable D64 fixture");
    CHECK(make_disk(full_path, true, false) == 0, "make full D64 fixture");
    CHECK(make_disk(dir_path, false, false) == 0, "make directory-full fixture");
    CHECK(make_disk(errors_path, false, true) == 0,
          "make D64 fixture with error-information trailer");

    DiskImage d;
    CHECK(disk_image_open(&d, path) == 0 && d.writable, "open writable image");
    const u8 small[] = { 0x01, 0x1C, 0x0A, 0x00, 0x00 };
    CHECK(disk_image_save_prg(&d, "TEST", small, sizeof(small), false) == DISK_SAVE_OK,
          "save one-sector PRG");
    DiskDirEntry found;
    u8 bytes[512];
    CHECK(disk_image_find_file(&d, "TEST", &found) == 0 && found.blocks == 1,
          "saved file appears in live directory");
    CHECK(disk_image_read_file(&d, &found, bytes, sizeof(bytes)) == (int)sizeof(small) &&
          memcmp(bytes, small, sizeof(small)) == 0,
          "saved PRG loads with its two-byte address");
    int free_blocks = -1;
    char disk_name[17], id[2];
    CHECK(disk_image_read_bam(&d, disk_name, sizeof(disk_name), id, NULL,
                       &free_blocks) == 0 && free_blocks == 663,
          "BAM free-block count drops by one");

    u8 *snapshot = malloc(d.size);
    CHECK(snapshot != NULL, "allocate image snapshot");
    if (snapshot) memcpy(snapshot, d.data, d.size);
    CHECK(disk_image_save_prg(&d, "TEST", small, sizeof(small), false) == DISK_SAVE_EXISTS,
          "duplicate name is rejected without @ replacement");
    if (snapshot) CHECK(memcmp(snapshot, d.data, d.size) == 0,
                        "duplicate error leaves live image unchanged");
    int dir_offset = disk_image_d64_track_offset(18) + 256;
    d.data[dir_offset + 2] |= 0x40; /* lock the existing PRG entry */
    CHECK(disk_image_save_prg(&d, "TEST", small, sizeof(small), true) ==
          DISK_SAVE_WRITE_PROTECT, "locked file rejects @ replacement");
    d.data[dir_offset + 2] &= (u8)~0x40;
    CHECK(disk_image_save_prg(&d, "BAD/NAME", small, sizeof(small), false) ==
          DISK_SAVE_BAD_NAME, "invalid DOS filename is rejected");
    CHECK(disk_image_save_prg(&d, "HUGE", small, (size_t)-1, false) ==
          DISK_SAVE_DISK_FULL, "oversized length cannot wrap block count");

    u8 larger[300];
    larger[0] = 0x01; larger[1] = 0x1C;
    for (size_t i = 2; i < sizeof(larger); ++i) larger[i] = (u8)i;
    CHECK(disk_image_save_prg(&d, "TEST", larger, sizeof(larger), true) == DISK_SAVE_OK,
          "replace file with a two-sector PRG");
    CHECK(disk_image_find_file(&d, "TEST", &found) == 0 && found.blocks == 2,
          "replacement has two blocks");
    CHECK(disk_image_read_file(&d, &found, bytes, sizeof(bytes)) == (int)sizeof(larger) &&
          memcmp(bytes, larger, sizeof(larger)) == 0,
          "replacement PRG round-trips across sectors");
    CHECK(disk_image_read_bam(&d, disk_name, sizeof(disk_name), id, NULL,
                       &free_blocks) == 0 && free_blocks == 662,
          "replacing a one-block file with two blocks updates BAM");

    VirtualDrive v;
    virtual_drive_init(&v, 8);
    virtual_drive_attach(&v, &d);
    send_open(&v, "TEST");
    send_data(&v, small, sizeof(small));
    send_close(&v);
    CHECK(strncmp(v.status, "63,", 3) == 0 &&
          virtual_drive_take_bus_status(&v) == 0x02,
          "IEC SAVE reports FILE EXISTS and bus error");
    send_open(&v, "@:TEST");
    send_data(&v, small, 2);
    send_data(&v, small + 2, sizeof(small) - 2);
    send_close(&v);
    CHECK(strncmp(v.status, "00,", 3) == 0,
          "@ replacement succeeds across multiple LISTEN segments");
    CHECK(disk_image_find_file(&d, "TEST", &found) == 0 && found.blocks == 1 &&
          disk_image_read_file(&d, &found, bytes, sizeof(bytes)) == (int)sizeof(small),
          "IEC replacement is visible immediately");

    send_open(&v, "ABORT");
    send_data(&v, small, sizeof(small));
    virtual_drive_attach(&v, &d); /* media swap discards pending write */
    CHECK(disk_image_find_file(&d, "ABORT", &found) != 0,
          "media swap discards an unclosed SAVE channel");

    for (int i = 1; i <= 8; ++i) {
        char name[8];
        snprintf(name, sizeof(name), "NEW%d", i);
        CHECK(disk_image_save_prg(&d, name, small, sizeof(small), false) == DISK_SAVE_OK,
              "save through directory-sector expansion");
    }
    u8 dir[256], bam[256];
    CHECK(disk_image_read_sector(&d, 18, 1, dir) == 0 && dir[0] == 18 && dir[1] >= 2,
          "ninth file extends the directory chain");
    CHECK(disk_image_read_sector(&d, 18, 0, bam) == 0 && bam[4 + 17 * 4] == 16,
          "directory expansion consumes one track-18 BAM sector");

    send_command(&v, "R:RENAMED=NEW1", true);
    CHECK(strncmp(v.status, "00,", 3) == 0 &&
          disk_image_find_file(&d, "NEW1", &found) != 0 &&
          disk_image_find_file(&d, "RENAMED", &found) == 0,
          "OPEN command channel renames a PRG");
    send_command(&v, "R:NEW8=RENAMED", false);
    CHECK(strncmp(v.status, "63,", 3) == 0,
          "PRINT command channel reports duplicate destination");
    virtual_drive_take_bus_status(&v);
    send_command(&v, "R:NOPE=ABSENT", false);
    CHECK(strncmp(v.status, "62,", 3) == 0,
          "rename reports missing source");
    virtual_drive_take_bus_status(&v);
    send_command(&v, "S:NEW?", false);
    CHECK(strncmp(v.status, "01,FILES SCRATCHED,07,00", 24) == 0 &&
          disk_image_find_file(&d, "NEW8", &found) != 0 &&
          disk_image_find_file(&d, "NEW2", &found) != 0,
          "wildcard scratch reports count and removes matching files");
    send_command(&v, "S:RENAMED", true);
    CHECK(strncmp(v.status, "01,FILES SCRATCHED,01,00", 24) == 0 &&
          disk_image_find_file(&d, "RENAMED", &found) != 0,
          "OPEN command channel scratches a named file");
    send_command(&v, "S:", false);
    CHECK(strncmp(v.status, "33,", 3) == 0,
          "empty scratch name reports syntax error");
    virtual_drive_take_bus_status(&v);
    send_command(&v, "R:MISSING", false);
    CHECK(strncmp(v.status, "33,", 3) == 0,
          "rename without equals reports syntax error");
    virtual_drive_take_bus_status(&v);

    int renamed_slot = disk_image_d64_track_offset(18) + 256 + 2;
    d.data[renamed_slot] |= 0x40; /* lock TEST */
    int removed = -1;
    CHECK(disk_image_scratch(&d, "TEST", &removed) ==
          DISK_SAVE_WRITE_PROTECT && removed == 0 &&
          disk_image_rename(&d, "LOCKED", "TEST") == DISK_SAVE_WRITE_PROTECT,
          "locked files reject scratch and rename");
    d.data[renamed_slot] &= (u8)~0x40;
    int file_offset = disk_image_d64_track_offset(d.data[dir_offset + 3]) +
                      (int)d.data[dir_offset + 4] * DISK_SECTOR_BYTES;
    u8 next_track = d.data[file_offset], next_sector = d.data[file_offset + 1];
    d.data[file_offset] = d.data[dir_offset + 3];
    d.data[file_offset + 1] = d.data[dir_offset + 4]; /* chain loop */
    CHECK(disk_image_scratch(&d, "TEST", &removed) == DISK_SAVE_DIR_ERROR &&
          removed == 0 && disk_image_find_file(&d, "TEST", &found) == 0,
          "corrupt chain cannot partially scratch a file");
    d.data[file_offset] = next_track;
    d.data[file_offset + 1] = next_sector;

    d.writable = false;
    CHECK(disk_image_save_prg(&d, "PROTECTED", small, sizeof(small), false) ==
          DISK_SAVE_WRITE_PROTECT, "read-only media rejects SAVE");
    d.writable = true;
    disk_image_close(&d);
    CHECK(disk_image_open(&d, path) == 0 &&
          disk_image_find_file(&d, "NEW8", &found) != 0 &&
          disk_image_find_file(&d, "TEST", &found) == 0,
          "scratch and surviving files persist after reopening D64");

    /* A disk changed outside the emulator must not be overwritten by a
     * stale attached copy, even when the size remains the same. */
    FILE *external = fopen(path, "rb+");
    CHECK(external != NULL, "open fixture for external edit");
    if (external) {
        CHECK(fseek(external, (long)d.size - 1, SEEK_SET) == 0 &&
              fputc(0x5A, external) != EOF && fclose(external) == 0,
              "modify attached image externally");
    }
    CHECK(disk_image_save_prg(&d, "STALE", small, sizeof(small), false) ==
          DISK_SAVE_IO_ERROR, "stale attached image refuses write-back");
    CHECK(disk_image_scratch(&d, "TEST", &removed) == DISK_SAVE_IO_ERROR &&
          disk_image_rename(&d, "STALE", "TEST") == DISK_SAVE_IO_ERROR,
          "external edits also block scratch and rename write-back");
    CHECK(disk_image_find_file(&d, "STALE", &found) != 0,
          "external-change error leaves live directory untouched");
    disk_image_close(&d);

    char link_path[sizeof(path) + 6];
    snprintf(link_path, sizeof(link_path), "%s.link", path);
    CHECK(symlink(path, link_path) == 0, "make symlinked media fixture");
    DiskImage link_disk;
    CHECK(disk_image_open(&link_disk, link_path) == 0 && !link_disk.writable,
          "symlinked D64 opens read-only");
    CHECK(disk_image_save_prg(&link_disk, "LINK", small, sizeof(small), false) ==
          DISK_SAVE_WRITE_PROTECT, "symlinked D64 rejects SAVE");
    disk_image_close(&link_disk);
    unlink(link_path);

    DiskImage full;
    CHECK(disk_image_open(&full, full_path) == 0, "open full fixture");
    CHECK(disk_image_save_prg(&full, "NO ROOM", small, sizeof(small), false) ==
          DISK_SAVE_DISK_FULL, "full disk reports DISK FULL");
    CHECK(disk_image_find_file(&full, "NO ROOM", &found) != 0,
          "full disk leaves directory unchanged");
    disk_image_close(&full);

    FILE *blocked = fopen(dir_path, "rb+");
    CHECK(blocked != NULL, "open directory-full fixture for setup");
    if (blocked) {
        u8 no_dir_space[4] = { 0, 0, 0, 0 };
        u8 occupied[256] = { 0 };
        occupied[1] = 0xFF;
        for (int i = 0; i < 8; ++i) occupied[i * 32 + 2] = 0x82;
        long bam_offset = disk_image_d64_track_offset(18);
        CHECK(fseek(blocked, bam_offset + 4 + 17 * 4, SEEK_SET) == 0 &&
              fwrite(no_dir_space, 1, sizeof(no_dir_space), blocked) ==
                  sizeof(no_dir_space), "occupy all spare directory sectors");
        CHECK(fseek(blocked, bam_offset + 256, SEEK_SET) == 0 &&
              fwrite(occupied, 1, sizeof(occupied), blocked) == sizeof(occupied),
              "fill all directory slots");
        fclose(blocked);
    }
    DiskImage dir_full;
    CHECK(disk_image_open(&dir_full, dir_path) == 0, "open directory-full fixture");
    CHECK(disk_image_save_prg(&dir_full, "NO SLOT", small, sizeof(small), false) ==
          DISK_SAVE_DIR_ERROR, "full directory reports DIR ERROR");
    disk_image_close(&dir_full);

    DiskImage with_errors;
    CHECK(disk_image_open(&with_errors, errors_path) == 0 && with_errors.has_errors,
          "open image with error-information trailer");
    CHECK(disk_image_save_prg(&with_errors, "ERRORS", small, sizeof(small), false) ==
          DISK_SAVE_OK, "save PRG to image with error-information trailer");
    size_t error_base = (size_t)disk_image_d64_track_offset(36);
    size_t bam_index = (size_t)disk_image_d64_track_offset(18) / DISK_SECTOR_BYTES;
    CHECK(with_errors.data[error_base] == 1 &&
          with_errors.data[error_base + bam_index] == 1 &&
          with_errors.data[error_base + bam_index + 1] == 1 &&
          with_errors.data[error_base + 1] == 0x20,
          "touched sectors have valid error bytes; untouched bytes remain");
    disk_image_close(&with_errors);

    free(snapshot);
    unlink(path);
    unlink(full_path);
    unlink(dir_path);
    unlink(errors_path);
    if (failures == 0) { puts("test-d64-save: OK"); return 0; }
    printf("test-d64-save: %d failure(s)\n", failures);
    return 1;
}
