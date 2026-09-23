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

static int track_sectors(DiskFormat format, int track) {
    if (format == DISK_FORMAT_D81) return 40;
    return disk_image_d64_track_sectors(track > 35 ? track - 35 : track);
}

static size_t sector_offset(DiskFormat format, int track, int sector) {
    if (format == DISK_FORMAT_D81)
        return ((size_t)track - 1u) * 40u * 256u + (size_t)sector * 256u;
    if (track > 35)
        return 174848u + (size_t)disk_image_d64_track_offset(track - 35) +
               (size_t)sector * 256u;
    return (size_t)disk_image_d64_track_offset(track) + (size_t)sector * 256u;
}

/* Build BAM entries independently of the production BAM implementation. */
static void set_free(u8 *image, DiskFormat format, int track, int sector,
                     bool free_sector) {
    size_t count, bits;
    if (format == DISK_FORMAT_D71 && track > 35) {
        count = sector_offset(format, 18, 0) + 0xDDu + (size_t)(track - 36);
        bits = sector_offset(format, 53, 0) + (size_t)(track - 36) * 3u;
    } else if (format == DISK_FORMAT_D71) {
        count = sector_offset(format, 18, 0) + 4u + (size_t)(track - 1) * 4u;
        bits = count + 1;
    } else {
        count = sector_offset(format, 40, track <= 40 ? 1 : 2) +
                0x10u + (size_t)((track - 1) % 40) * 6u;
        bits = count + 1;
    }
    u8 *byte = &image[bits + (size_t)sector / 8];
    u8 bit = (u8)(1u << (sector & 7));
    bool was_free = (*byte & bit) != 0;
    if (was_free == free_sector) return;
    if (free_sector) { *byte |= bit; ++image[count]; }
    else { *byte &= (u8)~bit; --image[count]; }
}

static void add_prg(u8 *image, DiskFormat format, int slot,
                    int track, const char *name, u8 marker) {
    int dir_track = format == DISK_FORMAT_D81 ? 40 : 18;
    int dir_sector = format == DISK_FORMAT_D81 ? 3 : 1;
    u8 *entry = image + sector_offset(format, dir_track, dir_sector) +
                (size_t)slot * 32u;
    entry[2] = 0x82;
    entry[3] = (u8)track;
    entry[4] = 0;
    memset(entry + 5, 0xA0, 16);
    memcpy(entry + 5, name, strlen(name));
    entry[30] = 1;
    u8 *block = image + sector_offset(format, track, 0);
    block[0] = 0; block[1] = 4;
    block[2] = 1; block[3] = 0x1C; block[4] = marker;
    set_free(image, format, track, 0, false);
}

static int make_image(char *path, DiskFormat format, bool with_errors,
                      bool force_second_side) {
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    size_t base = format == DISK_FORMAT_D81 ? 819200u : 349696u;
    size_t errors = with_errors ? base / 256u : 0u;
    u8 *image = calloc(1, base + errors);
    if (!image) { close(fd); return -1; }
    if (with_errors) memset(image + base, 0x20, errors);

    int dir_track = format == DISK_FORMAT_D81 ? 40 : 18;
    int first_dir = format == DISK_FORMAT_D81 ? 3 : 1;
    int last_track = format == DISK_FORMAT_D81 ? 80 : 70;
    u8 *header = image + sector_offset(format, dir_track, 0);
    header[0] = (u8)dir_track;
    header[1] = (u8)first_dir;
    if (format == DISK_FORMAT_D71) {
        header[2] = 'A'; header[3] = 0x80;
        memset(header + 0x90, 0xA0, 16);
        memcpy(header + 0x90, "D71 TEST", 8);
        header[0xA2] = 'A'; header[0xA3] = 'B';
        header[0xA5] = '2'; header[0xA6] = 'A';
    } else {
        header[2] = 'D';
        memset(header + 4, 0xA0, 16);
        memcpy(header + 4, "D81 TEST", 8);
        header[0x16] = 'A'; header[0x17] = 'B';
        header[0x19] = '3'; header[0x1A] = 'D';
        u8 *bam1 = image + sector_offset(format, 40, 1);
        u8 *bam2 = image + sector_offset(format, 40, 2);
        bam1[0] = 40; bam1[1] = 2;
        bam1[2] = bam2[2] = 'D';
        bam1[3] = bam2[3] = (u8)~'D';
        bam2[1] = 0xFF;
    }

    for (int t = 1; t <= last_track; ++t) {
        if (t == dir_track || (format == DISK_FORMAT_D71 && t == 53)) {
            int start = t == dir_track ? first_dir + 1 : track_sectors(format, t);
            for (int s = start; s < track_sectors(format, t); ++s)
                set_free(image, format, t, s, true);
        } else if (!force_second_side ||
                   t > (format == DISK_FORMAT_D81 ? 40 : 35)) {
            for (int s = 0; s < track_sectors(format, t); ++s)
                set_free(image, format, t, s, true);
        }
    }

    u8 *dir = image + sector_offset(format, dir_track, first_dir);
    dir[1] = 0xFF;
    add_prg(image, format, 0, 1, "SIDE1", 0x11);
    add_prg(image, format, 1, format == DISK_FORMAT_D81 ? 41 : 36,
            "SIDE2", 0x22);

    size_t done = 0, size = base + errors;
    while (done < size) {
        ssize_t n = write(fd, image + done, size - done);
        if (n <= 0) { free(image); close(fd); return -1; }
        done += (size_t)n;
    }
    free(image);
    return close(fd);
}

static void test_format(DiskFormat format) {
    char path[] = "/tmp/1986-disk-format-XXXXXX";
    char full_side[] = "/tmp/1986-disk-side2-XXXXXX";
    CHECK(make_image(path, format, true, false) == 0, "make two-sided image");
    CHECK(make_image(full_side, format, false, true) == 0,
          "make image with first-side data tracks full");

    DiskImage d;
    CHECK(disk_image_open(&d, path) == 0 && d.format == format && d.has_errors,
          "detect image format and error-byte trailer");
    char name[17], id[2];
    int free_blocks = -1;
    int expected_free = format == DISK_FORMAT_D81 ? 3158 : 1326;
    CHECK(disk_image_read_bam(&d, name, sizeof(name), id, NULL,
                              &free_blocks) == 0 && free_blocks == expected_free,
          "free-block count combines both sides and excludes system track");
    CHECK(strcmp(name, format == DISK_FORMAT_D81 ? "D81 TEST" : "D71 TEST") == 0 &&
          id[0] == 'A' && id[1] == 'B', "read format-specific disk header");
    u8 *header = d.data + sector_offset(format,
                                         format == DISK_FORMAT_D81 ? 40 : 18, 0);
    u8 version = header[2];
    header[2] = 'X';
    CHECK(disk_image_save_prg(&d, "BADVER", (const u8[]){1, 0x1C}, 2, false) ==
          DISK_SAVE_DOS_MISMATCH, "unknown DOS version blocks writes");
    header[2] = version;

    DiskDirEntry entry;
    u8 bytes[512];
    CHECK(disk_image_find_file(&d, "SIDE1", &entry) == 0 &&
          disk_image_read_file(&d, &entry, bytes, sizeof(bytes)) == 3 &&
          bytes[2] == 0x11, "load first-side PRG");
    CHECK(disk_image_find_file(&d, "SIDE2", &entry) == 0 &&
          disk_image_read_file(&d, &entry, bytes, sizeof(bytes)) == 3 &&
          bytes[2] == 0x22, "load second-side PRG");
    u8 listing[1024];
    size_t listing_len = disk_image_build_directory_program(&d, listing,
                                                             sizeof(listing));
    CHECK(listing_len == 32u + 2u * 32u + 31u &&
          memcmp(listing + 8, name, strlen(name)) == 0 &&
          listing[96 + 2] == (u8)expected_free &&
          listing[96 + 3] == (u8)(expected_free >> 8),
          "directory stream has correct header and free blocks");

    u8 prg[300];
    prg[0] = 1; prg[1] = 0x1C;
    for (size_t i = 2; i < sizeof(prg); ++i) prg[i] = (u8)i;
    CHECK(disk_image_save_prg(&d, "NEW", prg, sizeof(prg), false) ==
          DISK_SAVE_OK, "save two-sector PRG");
    CHECK(disk_image_find_file(&d, "NEW", &entry) == 0 && entry.blocks == 2 &&
          disk_image_read_file(&d, &entry, bytes, sizeof(bytes)) ==
              (int)sizeof(prg) && memcmp(bytes, prg, sizeof(prg)) == 0,
          "saved PRG round-trips");
    CHECK(disk_image_read_bam(&d, name, sizeof(name), id, NULL, &free_blocks) == 0 &&
          free_blocks == expected_free - 2,
          "save updates free-block count");
    CHECK(disk_image_save_prg(&d, "NEW", prg, sizeof(prg), false) ==
          DISK_SAVE_EXISTS, "duplicate name is rejected");
    CHECK(disk_image_save_prg(&d, "NEW", prg, 100, true) == DISK_SAVE_OK &&
          disk_image_find_file(&d, "NEW", &entry) == 0 && entry.blocks == 1 &&
          disk_image_read_bam(&d, name, sizeof(name), id, NULL, &free_blocks) == 0 &&
          free_blocks == expected_free - 1,
          "@ replacement releases old sectors and updates BAM");
    CHECK(disk_image_rename(&d, "INNER", "NEW") == DISK_SAVE_OK &&
          disk_image_find_file(&d, "NEW", &entry) != 0 &&
          disk_image_find_file(&d, "INNER", &entry) == 0 &&
          disk_image_read_file(&d, &entry, bytes, sizeof(bytes)) == 100,
          "rename changes only the directory name and preserves file data");
    CHECK(disk_image_rename(&d, "SIDE2", "INNER") == DISK_SAVE_EXISTS &&
          disk_image_rename(&d, "MISSING", "ABSENT") == DISK_SAVE_NOT_FOUND,
          "rename rejects duplicate destinations and missing sources");
    int removed = -1;
    CHECK(disk_image_scratch(&d, "SIDE?", &removed) == DISK_SAVE_OK &&
          removed == 2 && disk_image_find_file(&d, "SIDE1", &entry) != 0 &&
          disk_image_find_file(&d, "SIDE2", &entry) != 0 &&
          disk_image_read_bam(&d, name, sizeof(name), id, NULL, &free_blocks) == 0 &&
          free_blocks == expected_free + 1,
          "wildcard scratch frees files on both sides and updates both BAMs");
    CHECK(disk_image_scratch(&d, "SIDE?", &removed) == DISK_SAVE_OK &&
          removed == 0, "scratch of missing files reports zero removed");
    CHECK(disk_image_save_prg(&d, "SIDE1", prg, 100, false) == DISK_SAVE_OK,
          "scratch releases its directory slot for reuse");
    if (format == DISK_FORMAT_D81) {
        u8 *slot = d.data + sector_offset(format, 40, 3);
        slot[2] = 0x85; /* model a CBM partition entry */
        CHECK(disk_image_save_prg(&d, "SIDE1", prg, 100, true) ==
              DISK_SAVE_TYPE_MISMATCH,
              "@ replacement never overwrites a D81 partition entry");
        CHECK(disk_image_scratch(&d, "SIDE1", &removed) ==
              DISK_SAVE_TYPE_MISMATCH &&
              disk_image_rename(&d, "OTHER", "SIDE1") ==
              DISK_SAVE_TYPE_MISMATCH,
              "partition entries are not modified by DOS commands");
        slot[2] = 0x82;
    }
    size_t base = format == DISK_FORMAT_D81 ? 819200u : 349696u;
    size_t bam1 = sector_offset(format, format == DISK_FORMAT_D81 ? 40 : 18,
                                format == DISK_FORMAT_D81 ? 1 : 0) / 256u;
    size_t bam2 = sector_offset(format, format == DISK_FORMAT_D81 ? 40 : 53,
                                format == DISK_FORMAT_D81 ? 2 : 0) / 256u;
    CHECK(d.data[base + bam1] == 1 && d.data[base + bam2] == 1,
          "error bytes for both BAM sectors are valid after saving");
    disk_image_close(&d);
    CHECK(disk_image_open(&d, path) == 0 &&
          disk_image_find_file(&d, "INNER", &entry) == 0 &&
          disk_image_find_file(&d, "SIDE2", &entry) != 0,
          "renamed and scratched files persist after reopening image");
    disk_image_close(&d);

    CHECK(disk_image_open(&d, full_side) == 0, "open first-side-full image");
    const u8 small[] = { 1, 0x1C, 0xAA };
    CHECK(disk_image_save_prg(&d, "CROSS", small, sizeof(small), false) ==
          DISK_SAVE_OK && disk_image_find_file(&d, "CROSS", &entry) == 0 &&
          entry.start_track == (format == DISK_FORMAT_D81 ? 41 : 36) &&
          entry.start_sector == 1, "allocation crosses to second side");
    for (int i = 1; i <= 6; ++i) {
        char filename[8];
        snprintf(filename, sizeof(filename), "EX%d", i);
        CHECK(disk_image_save_prg(&d, filename, small, sizeof(small), false) ==
              DISK_SAVE_OK, "save through directory expansion");
    }
    int dir_track = format == DISK_FORMAT_D81 ? 40 : 18;
    int first_dir = format == DISK_FORMAT_D81 ? 3 : 1;
    u8 dir[256];
    CHECK(disk_image_read_sector(&d, dir_track, first_dir, dir) == 0 &&
          dir[0] == dir_track && dir[1] == first_dir + 1,
          "full directory extends on the correct track");
    CHECK(disk_image_find_file(&d, "EX6", &entry) == 0,
          "file in extended directory is found");

    if (format == DISK_FORMAT_D81) {
        size_t size = 70000;
        u8 *large = malloc(size);
        CHECK(large != NULL, "allocate large D81 PRG");
        if (large) {
            for (size_t i = 0; i < size; ++i) large[i] = (u8)i;
            large[0] = 1; large[1] = 0x1C;
            CHECK(disk_image_save_prg(&d, "LARGE", large, size, false) ==
                  DISK_SAVE_OK, "save PRG larger than old 64K response cap");
            VirtualDrive v;
            virtual_drive_init(&v, 8);
            virtual_drive_attach(&v, &d);
            virtual_drive_attention(&v, 0x28);
            virtual_drive_attention(&v, 0xF0);
            for (const char *p = "LARGE"; *p; ++p) virtual_drive_send(&v, (u8)*p);
            virtual_drive_attention(&v, 0x3F);
            virtual_drive_attention(&v, 0x48);
            virtual_drive_attention(&v, 0x60);
            size_t got = 0;
            u8 byte = 0;
            int status;
            bool matches = true;
            while ((status = virtual_drive_receive(&v, &byte)) > 0) {
                if (got < size && byte != large[got]) matches = false;
                ++got;
                if (status == 2) break;
            }
            CHECK(got == size && status == 2 && matches,
                  "IEC channel streams full large PRG with EOI");
            virtual_drive_reset(&v);
            free(large);
        }
    }

    /* Exhaust every user-data bitmap bit while retaining directory space. */
    for (int t = format == DISK_FORMAT_D81 ? 41 : 36;
         t <= (format == DISK_FORMAT_D81 ? 80 : 70); ++t) {
        if (format == DISK_FORMAT_D71 && t == 53) continue;
        for (int s = 0; s < track_sectors(format, t); ++s)
            set_free(d.data, format, t, s, false);
    }
    CHECK(disk_image_save_prg(&d, "NOROOM", small, sizeof(small), false) ==
          DISK_SAVE_DISK_FULL && disk_image_find_file(&d, "NOROOM", &entry) != 0,
          "full D71/D81 data area rejects SAVE without a partial file");

    /* Now fill every root directory slot and reserve its remaining sectors. */
    for (int s = first_dir; s <= first_dir + 1; ++s) {
        u8 *sector = d.data + sector_offset(format, dir_track, s);
        for (int i = 0; i < 8; ++i) sector[i * 32 + 2] = 0x82;
    }
    for (int s = first_dir + 1; s < track_sectors(format, dir_track); ++s)
        set_free(d.data, format, dir_track, s, false);
    CHECK(disk_image_save_prg(&d, "NODIR", small, sizeof(small), false) ==
          DISK_SAVE_DIR_ERROR,
          "full D71/D81 root directory rejects SAVE");
    disk_image_close(&d);
    if (getenv("C128_TEST_KEEP_DISKS")) {
        printf("%s fixtures: %s %s\n",
               format == DISK_FORMAT_D81 ? "D81" : "D71", path, full_side);
    } else {
        unlink(path);
        unlink(full_side);
    }
}

static void test_blank_image(DiskFormat format, const char *extension,
                             int expected_free, u8 expected_dos_type) {
    char directory[] = "/tmp/1986-blank-disks-XXXXXX";
    if (!mkdtemp(directory)) {
        CHECK(false, "create blank-image test directory");
        return;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/My Blank%s", directory, extension);
    CHECK(disk_image_create_blank(path, format) == DISK_SAVE_OK,
          "create blank disk image");

    DiskImage disk;
    CHECK(disk_image_open(&disk, path) == 0, "open newly created disk image");
    if (disk.data) {
        char name[17], id[2];
        u8 dos_type = 0;
        int free_blocks = -1;
        DiskDirEntry entries[8];
        CHECK(disk.format == format && disk.writable,
              "blank disk has selected format and is writable");
        CHECK(disk_image_read_directory_entries(&disk, entries, 8) == 0,
              "blank disk directory is empty");
        CHECK(disk_image_read_bam(&disk, name, sizeof(name), id, &dos_type,
                                  &free_blocks) == 0 &&
              strcmp(name, "MY BLANK") == 0 && id[0] == '0' && id[1] == '0' &&
              dos_type == expected_dos_type && free_blocks == expected_free,
              "blank disk has a valid label, ID, DOS type, and BAM");
        if (format == DISK_FORMAT_D81) {
            const u8 *bam1 = disk.data + sector_offset(format, 40, 1);
            const u8 *bam2 = disk.data + sector_offset(format, 40, 2);
            CHECK(bam1[0] == 40 && bam1[1] == 2 && bam2[0] == 0 &&
                  bam2[1] == 0xFF && bam1[6] == 0xC0 && bam2[6] == 0xC0,
                  "D81 BAM chain and format flags match CBM DOS");
        }

        const u8 program[] = { 1, 0x1C, 0x42 };
        CHECK(disk_image_save_prg(&disk, "HELLO", program, sizeof(program),
                                  false) == DISK_SAVE_OK,
              "new blank disk accepts a PRG write");
        DiskDirEntry entry;
        u8 loaded[sizeof(program)];
        CHECK(disk_image_find_file(&disk, "HELLO", &entry) == 0 &&
              disk_image_read_file(&disk, &entry, loaded, sizeof(loaded)) ==
                  (int)sizeof(loaded) &&
              memcmp(loaded, program, sizeof(program)) == 0,
              "new blank disk reads back its first PRG");
        disk_image_close(&disk);
    }
    unlink(path);
    rmdir(directory);
}

int main(void) {
    test_format(DISK_FORMAT_D71);
    test_format(DISK_FORMAT_D81);
    test_blank_image(DISK_FORMAT_D64, ".d64", 664, '2');
    test_blank_image(DISK_FORMAT_D71, ".d71", 1328, '2');
    test_blank_image(DISK_FORMAT_D81, ".d81", 3160, '3');
    CHECK(disk_image_create_blank("/tmp/not-a-disk.prg", DISK_FORMAT_PRG) ==
          DISK_SAVE_TYPE_MISMATCH,
          "blank creator rejects non-disk formats");
    if (failures == 0) { puts("test-disk-formats: OK"); return 0; }
    printf("test-disk-formats: %d failure(s)\n", failures);
    return 1;
}
