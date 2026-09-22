#ifdef __APPLE__
#define _DARWIN_C_SOURCE 1
#else
#define _POSIX_C_SOURCE 200809L
#endif
#include "drive.h"
#include "leds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

void leds_ping(LedId id) { (void)id; }
void leds_set_drive_unit(LedId id, int unit) { (void)id; (void)unit; }

static bool contains_bytes(const u8 *bytes, size_t size, const char *text) {
    size_t length = strlen(text);
    for (size_t i = 0; i + length <= size; ++i)
        if (memcmp(bytes + i, text, length) == 0) return true;
    return false;
}

static bool write_file(const char *path, const u8 *bytes, size_t size) {
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    bool ok = fwrite(bytes, 1, size, file) == size;
    return fclose(file) == 0 && ok;
}

static int read_channel(VirtualDrive *v, const char *name, u8 *out, size_t cap) {
    u8 unit = (u8)v->unit;
    virtual_drive_attention(v, (u8)(0x20 | unit));
    virtual_drive_attention(v, 0xF0);
    for (const char *p = name; *p; ++p) virtual_drive_send(v, (u8)*p);
    virtual_drive_attention(v, 0x3F);
    virtual_drive_attention(v, (u8)(0x40 | unit));
    virtual_drive_attention(v, 0x60);
    size_t length = 0;
    u8 byte;
    int status;
    while ((status = virtual_drive_receive(v, &byte)) > 0) {
        if (length == cap) { length = cap + 1; break; }
        out[length++] = byte;
        if (status == 2) break;
    }
    virtual_drive_attention(v, 0x5F);
    virtual_drive_attention(v, (u8)(0x20 | unit));
    virtual_drive_attention(v, 0xE0);
    virtual_drive_attention(v, 0x3F);
    return length > cap ? -1 : (int)length;
}

static void send_save(VirtualDrive *v, const char *name, const u8 *bytes, size_t size) {
    u8 unit = (u8)v->unit;
    virtual_drive_attention(v, (u8)(0x20 | unit));
    virtual_drive_attention(v, 0xF1);
    for (const char *p = name; *p; ++p) virtual_drive_send(v, (u8)*p);
    virtual_drive_attention(v, 0x3F);
    virtual_drive_attention(v, (u8)(0x20 | unit));
    virtual_drive_attention(v, 0x61);
    for (size_t i = 0; i < size; ++i) virtual_drive_send(v, bytes[i]);
    virtual_drive_attention(v, 0x3F);
    virtual_drive_attention(v, (u8)(0x20 | unit));
    virtual_drive_attention(v, 0xE1);
    virtual_drive_attention(v, 0x3F);
}

static void send_command(VirtualDrive *v, const char *command) {
    virtual_drive_attention(v, (u8)(0x20 | v->unit));
    virtual_drive_attention(v, 0xFF);
    for (const char *p = command; *p; ++p) virtual_drive_send(v, (u8)*p);
    virtual_drive_attention(v, 0x3F);
}

int main(void) {
    char directory[] = "/tmp/1986-prg-drive-XXXXXX";
    CHECK(mkdtemp(directory) != NULL, "create temporary PRG directory");
    char prg_path[256], short_path[256], empty_path[256], large_path[256],
         huge_path[256], d64_path[256];
    snprintf(prg_path, sizeof(prg_path), "%s/Solar System.PRG", directory);
    snprintf(short_path, sizeof(short_path), "%s/too-short.prg", directory);
    snprintf(empty_path, sizeof(empty_path), "%s/empty.prg", directory);
    snprintf(large_path, sizeof(large_path), "%s/big.prg", directory);
    snprintf(huge_path, sizeof(huge_path), "%s/oversize.prg", directory);
    snprintf(d64_path, sizeof(d64_path), "%s/disk.d64", directory);
    const u8 program[] = { 0x01, 0x1C, 0x0B, 0x1C, 0x0A, 0x00,
                           0x99, 0x20, '"', 'H', 'I', '"', 0x00, 0x00, 0x00 };
    CHECK(write_file(prg_path, program, sizeof(program)), "create standalone PRG");
    CHECK(write_file(short_path, program, 2), "create address-only PRG");
    CHECK(write_file(empty_path, program, 0), "create empty PRG");
    const size_t large_size = 100000;
    u8 *large = malloc(large_size), *large_loaded = malloc(large_size);
    CHECK(large && large_loaded, "allocate native C128-sized PRG fixture");
    if (large && large_loaded) {
        large[0] = 0x01; large[1] = 0x1C;
        for (size_t i = 2; i < large_size; ++i) large[i] = (u8)i;
        CHECK(write_file(large_path, large, large_size), "create >64 KiB PRG");
    }

    DiskImage image;
    CHECK(disk_image_open(&image, prg_path) == 0 &&
          image.format == DISK_FORMAT_PRG && !image.writable &&
          strcmp(disk_image_format_name(&image), "PRG") == 0,
          "recognize uppercase .PRG as read-only media");
    DiskDirEntry entries[2], found;
    CHECK(disk_image_read_directory_entries(&image, entries, 2) == 1 &&
          strcmp(entries[0].name, "SOLAR SYSTEM") == 0 &&
          entries[0].type == 2 && entries[0].blocks == 1,
          "single PRG appears under host basename without extension");
    CHECK(disk_image_find_file(&image, "solar system", &found) == 0 &&
          disk_image_find_file(&image, "0:SOL*", &found) == 0 &&
          disk_image_find_file(&image, "*", &found) == 0 &&
          disk_image_find_file(&image, "", &found) == 0 &&
          disk_image_find_file(&image, "MISSING", &found) != 0,
          "exact, wildcard, first-file, and missing lookups work");
    u8 loaded[256];
    CHECK(disk_image_find_file(&image, "*", &found) == 0 &&
          disk_image_read_file(&image, &found, loaded, sizeof(loaded)) ==
          (int)sizeof(program) && memcmp(loaded, program, sizeof(program)) == 0,
          "PRG read retains the two-byte load address and complete payload");
    CHECK(disk_image_read_file(&image, &found, loaded, 2) < 0 &&
          disk_image_read_sector(&image, 1, 0, loaded) < 0,
          "PRG is not exposed as sectors or silently truncated");
    u8 listing[128];
    size_t listing_len = disk_image_build_directory_program(&image, listing,
                                                             sizeof(listing));
    CHECK(listing_len == 95 &&
          contains_bytes(listing, listing_len, "SOLAR SYSTEM") &&
          contains_bytes(listing, listing_len, "PRG"),
          "DIRECTORY exposes one PRG entry in the normal BASIC listing");
    CHECK(disk_image_save_prg(&image, "NEW", program, sizeof(program), false) ==
          DISK_SAVE_WRITE_PROTECT &&
          disk_image_scratch(&image, "SOL*", NULL) == DISK_SAVE_WRITE_PROTECT &&
          disk_image_rename(&image, "NEW", "SOLAR SYSTEM") ==
          DISK_SAVE_WRITE_PROTECT,
          "all mutating disk-image operations reject standalone PRG media");
    disk_image_close(&image);

    Config cfg = { 0 };
    cfg.drive_unit = 8;
    Drive drive;
    drive_init(&drive, &cfg);
    CHECK(drive_attach_disk(&drive, prg_path) == 0 && drive.disk_attached &&
          drive.image.format == DISK_FORMAT_PRG,
          "Drive 1 attaches standalone PRG through the normal media path");
    int length = read_channel(&drive.virtual_drive, "$", loaded, sizeof(loaded));
    CHECK(length == 95 && contains_bytes(loaded, (size_t)length, "SOLAR SYSTEM"),
          "IEC DIRECTORY reads the pseudo-disk listing");
    length = read_channel(&drive.virtual_drive, "*", loaded, sizeof(loaded));
    CHECK(length == (int)sizeof(program) &&
          memcmp(loaded, program, sizeof(program)) == 0,
          "IEC LOAD first-file returns the original PRG bytes");
    length = read_channel(&drive.virtual_drive, "SOLAR SYSTEM", loaded,
                          sizeof(loaded));
    CHECK(length == (int)sizeof(program), "IEC LOAD by name succeeds");
    CHECK(read_channel(&drive.virtual_drive, "MISSING", loaded,
                       sizeof(loaded)) == 0 &&
          strncmp(drive.virtual_drive.status, "62,", 3) == 0,
          "unknown IEC name reports FILE NOT FOUND");
    send_save(&drive.virtual_drive, "NEW", program, sizeof(program));
    CHECK(strncmp(drive.virtual_drive.status, "26,", 3) == 0,
          "IEC SAVE reports WRITE PROTECT");
    send_command(&drive.virtual_drive, "S:SOLAR SYSTEM");
    CHECK(strncmp(drive.virtual_drive.status, "26,", 3) == 0,
          "IEC SCRATCH reports WRITE PROTECT");
    send_command(&drive.virtual_drive, "R:NEW=SOLAR SYSTEM");
    CHECK(strncmp(drive.virtual_drive.status, "26,", 3) == 0,
          "IEC RENAME reports WRITE PROTECT");
    FILE *verify = fopen(prg_path, "rb");
    u8 unchanged[sizeof(program)];
    CHECK(verify && fread(unchanged, 1, sizeof(unchanged), verify) ==
          sizeof(unchanged) && memcmp(unchanged, program, sizeof(program)) == 0,
          "failed SAVE does not modify the host PRG");
    if (verify) fclose(verify);

    Drive second;
    drive_init(&second, &cfg);
    drive_set_unit(&second, 9);
    CHECK(drive_attach_disk(&second, prg_path) == 0 &&
          read_channel(&second.virtual_drive, "*", loaded, sizeof(loaded)) ==
          (int)sizeof(program),
          "Drive 2 can serve a standalone PRG on its own IEC unit");
    drive_attach_disk(&second, NULL);

    if (large && large_loaded) {
        CHECK(drive_attach_disk(&drive, large_path) == 0 &&
              drive.image.format == DISK_FORMAT_PRG,
              "native C128 PRG larger than 64 KiB attaches");
        DiskDirEntry big_entry;
        CHECK(disk_image_find_file(&drive.image, "BIG", &big_entry) == 0 &&
              big_entry.blocks == (int)((large_size + 253u) / 254u),
              "large PRG directory block count is accurate");
        CHECK(read_channel(&drive.virtual_drive, "BIG", large_loaded,
                           large_size) == (int)large_size &&
              memcmp(large_loaded, large, large_size) == 0,
              "IEC LOAD transfers the complete >64 KiB PRG");
    }
    free(large_loaded);
    free(large);

    FILE *blank = fopen(d64_path, "wb");
    CHECK(blank && fseek(blank, 174848 - 1, SEEK_SET) == 0 &&
          fputc(0, blank) != EOF, "create exact-size D64 for media swap");
    if (blank) fclose(blank);
    CHECK(drive_attach_disk(&drive, d64_path) == 0 &&
          drive.image.format == DISK_FORMAT_D64 &&
          !drive.virtual_drive.channel_open[0],
          "switching PRG to D64 closes the old channel and selects disk media");
    CHECK(drive_attach_disk(&drive, prg_path) == 0 &&
          drive.image.format == DISK_FORMAT_PRG,
          "switching D64 back to PRG works");
    CHECK(drive_attach_disk(&drive, short_path) != 0 && !drive.disk_attached &&
          drive.virtual_drive.disk == NULL,
          "too-short replacement fails and leaves drive ejected");
    CHECK(drive_attach_disk(&drive, empty_path) != 0,
          "empty PRG is rejected");
    FILE *huge = fopen(huge_path, "wb");
    CHECK(huge && fseek(huge, 3200 * 254, SEEK_SET) == 0 &&
          fputc(0, huge) != EOF, "create oversized PRG fixture");
    if (huge) fclose(huge);
    CHECK(drive_attach_disk(&drive, huge_path) != 0,
          "PRG exceeding virtual drive transfer limit is rejected");
    CHECK(drive_attach_disk(&drive, NULL) == 0, "eject after failed insert");

    unlink(prg_path);
    unlink(short_path);
    unlink(empty_path);
    unlink(large_path);
    unlink(huge_path);
    unlink(d64_path);
    rmdir(directory);
    if (!failures) puts("test-prg-drive: OK");
    return failures ? 1 : 0;
}
