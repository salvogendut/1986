#include "drive.h"
#include "leds.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

/* drive.c only uses the UI LED when bytes are received. */
void leds_ping(LedId id) { (void)id; }

static int make_image(char *path, DiskFormat format, u8 marker,
                      const char *disk_name, const char *file_name) {
    int fd = mkstemp(path);
    if (fd < 0) return -1;

    size_t size = format == DISK_FORMAT_D64 ? 174848u :
                  format == DISK_FORMAT_D71 ? 349696u : 819200u;
    u8 *bytes = calloc(1, size);
    if (!bytes) {
        close(fd);
        return -1;
    }
    bytes[0] = marker;

    int header_offset = format == DISK_FORMAT_D81 ? 39 * 40 * 256 :
                        disk_image_d64_track_offset(18);
    int dir_sector = format == DISK_FORMAT_D81 ? 3 : 1;
    u8 *bam = bytes + header_offset;
    int name_offset = format == DISK_FORMAT_D81 ? 4 : 0x90;
    int id_offset = format == DISK_FORMAT_D81 ? 0x16 : 0xA2;
    size_t disk_name_len = strlen(disk_name);
    for (int i = 0; i < 16; i++)
        bam[name_offset + i] = (size_t)i < disk_name_len
                      ? (u8)disk_name[i] : 0xA0;
    bam[id_offset] = '0';
    bam[id_offset + 1] = '1';
    if (format == DISK_FORMAT_D81) {
        bam[0x19] = '3'; bam[0x1A] = 'D';
    } else {
        bam[0xA5] = '2'; bam[0xA6] = 'A';
    }

    u8 *dir = bam + dir_sector * DISK_SECTOR_BYTES;
    dir[0] = 0;
    dir[1] = 0xFF;
    dir[2] = 0x82;
    dir[3] = format == DISK_FORMAT_D81 ? 39 : 17;
    dir[4] = 0;
    size_t file_name_len = strlen(file_name);
    for (int i = 0; i < 16; i++)
        dir[5 + i] = (size_t)i < file_name_len
                   ? (u8)file_name[i] : 0xA0;
    dir[30] = 1;

    size_t done = 0;
    while (done < size) {
        ssize_t n = write(fd, bytes + done, size - done);
        if (n <= 0) {
            free(bytes);
            close(fd);
            return -1;
        }
        done += (size_t)n;
    }
    free(bytes);
    return close(fd);
}

static bool directory_contains(VirtualDrive *drive, const char *name) {
    virtual_drive_attention(drive, 0x28); /* LISTEN 8 */
    virtual_drive_attention(drive, 0xF0); /* OPEN channel 0 */
    virtual_drive_send(drive, '$');
    virtual_drive_attention(drive, 0x3F); /* UNLISTEN */
    virtual_drive_attention(drive, 0x48); /* TALK 8 */
    virtual_drive_attention(drive, 0x60); /* channel 0 */

    u8 bytes[1024];
    size_t length = 0;
    int status;
    do {
        status = virtual_drive_receive(drive, &bytes[length]);
        if (status > 0) length++;
    } while (status == 1 && length < sizeof(bytes));
    virtual_drive_attention(drive, 0x5F); /* UNTALK */

    size_t name_len = strlen(name);
    for (size_t i = 0; i + name_len <= length; i++) {
        if (memcmp(bytes + i, name, name_len) == 0) return true;
    }
    return false;
}

int main(void) {
    char first[] = "/tmp/1986-drive-first-XXXXXX";
    char second[] = "/tmp/1986-drive-second-XXXXXX";
    char d71[] = "/tmp/1986-drive-d71-XXXXXX";
    char d81[] = "/tmp/1986-drive-d81-XXXXXX";
    CHECK(make_image(first, DISK_FORMAT_D64, 0x11, "FIRST DISK", "FIRSTFILE") == 0,
          "create first D64");
    CHECK(make_image(second, DISK_FORMAT_D64, 0x22, "SECOND DISK", "SECONDFILE") == 0,
          "create second D64");
    CHECK(make_image(d71, DISK_FORMAT_D71, 0x33, "D71 DISK", "SEVENTYONE") == 0,
          "create D71 image");
    CHECK(make_image(d81, DISK_FORMAT_D81, 0x44, "D81 DISK", "EIGHTYONE") == 0,
          "create D81 image");

    Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.drive_unit = 8;

    Drive drive;
    drive_init(&drive, &cfg);
    CHECK(drive_attach_disk(&drive, first) == 0, "insert first DiskImage");
    CHECK(drive.disk_attached, "first DiskImage is attached");
    CHECK(drive.virtual_drive.disk == &drive.image,
          "virtual drive sees first DiskImage");
    CHECK(drive.image.data && drive.image.data[0] == 0x11,
          "first DiskImage contents are live");
    CHECK(directory_contains(&drive.virtual_drive, "FIRSTFILE"),
          "DIRECTORY reads first DiskImage");

    CHECK(drive_attach_disk(&drive, second) == 0, "replace DiskImage");
    CHECK(drive.disk_attached, "replacement DiskImage is attached");
    CHECK(drive.virtual_drive.disk == &drive.image,
          "virtual drive sees replacement DiskImage");
    CHECK(drive.image.data && drive.image.data[0] == 0x22,
          "replacement DiskImage contents are live");
    CHECK(!drive.virtual_drive.channel_open[0],
          "replacement closes channels from previous media");
    CHECK(drive.virtual_drive.response_len == 0,
          "replacement discards previous media response");
    CHECK(directory_contains(&drive.virtual_drive, "SECONDFILE"),
          "DIRECTORY reads replacement DiskImage");
    CHECK(!directory_contains(&drive.virtual_drive, "FIRSTFILE"),
          "DIRECTORY no longer reads ejected DiskImage");

    CHECK(drive_attach_disk(&drive, d71) == 0 &&
          drive.image.format == DISK_FORMAT_D71 &&
          directory_contains(&drive.virtual_drive, "SEVENTYONE"),
          "live swap to D71 updates DIRECTORY");
    CHECK(drive_attach_disk(&drive, d81) == 0 &&
          drive.image.format == DISK_FORMAT_D81 &&
          directory_contains(&drive.virtual_drive, "EIGHTYONE"),
          "live swap to D81 updates DIRECTORY");
    CHECK(!directory_contains(&drive.virtual_drive, "SEVENTYONE"),
          "D81 swap discards prior D71 directory");

    CHECK(drive_attach_disk(&drive, NULL) == 0, "eject DiskImage");
    CHECK(!drive.disk_attached, "eject clears attached state");
    CHECK(drive.virtual_drive.disk == NULL, "eject clears virtual media");

    CHECK(drive_attach_disk(&drive, first) == 0, "reinsert before failure");
    unlink(first);
    CHECK(drive_attach_disk(&drive, first) != 0,
          "invalid replacement is rejected");
    CHECK(!drive.disk_attached, "failed replacement leaves drive empty");
    CHECK(drive.virtual_drive.disk == NULL,
          "failed replacement does not retain stale virtual media");
    unlink(second);
    unlink(d71);
    unlink(d81);

    if (failures == 0) { printf("test-drive: OK\n"); return 0; }
    printf("test-drive: %d failure(s)\n", failures);
    return 1;
}
