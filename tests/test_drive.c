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

static int make_image(char *path, u8 marker, const char *disk_name,
                      const char *file_name) {
    int fd = mkstemp(path);
    if (fd < 0) return -1;

    size_t size = (size_t)d64_track_offset(36);
    u8 *bytes = calloc(1, size);
    if (!bytes) {
        close(fd);
        return -1;
    }
    bytes[0] = marker;

    u8 *bam = bytes + d64_track_offset(18);
    size_t disk_name_len = strlen(disk_name);
    for (int i = 0; i < 16; i++)
        bam[0x90 + i] = (size_t)i < disk_name_len
                      ? (u8)disk_name[i] : 0xA0;
    bam[0xA2] = '0';
    bam[0xA3] = '1';
    bam[0xA5] = '2';
    bam[0xA6] = 'A';

    u8 *dir = bam + D64_SECTOR_BYTES;
    dir[0] = 0;
    dir[1] = 0xFF;
    dir[2] = 0x82;
    dir[3] = 17;
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
    CHECK(make_image(first, 0x11, "FIRST DISK", "FIRSTFILE") == 0,
          "create first D64");
    CHECK(make_image(second, 0x22, "SECOND DISK", "SECONDFILE") == 0,
          "create second D64");

    Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.drive_unit = 8;

    Drive drive;
    drive_init(&drive, &cfg);
    CHECK(drive_attach_disk(&drive, first) == 0, "insert first D64");
    CHECK(drive.disk_attached, "first D64 is attached");
    CHECK(drive.virtual_drive.disk == &drive.d64,
          "virtual drive sees first D64");
    CHECK(drive.d64.data && drive.d64.data[0] == 0x11,
          "first D64 contents are live");
    CHECK(directory_contains(&drive.virtual_drive, "FIRSTFILE"),
          "DIRECTORY reads first D64");

    CHECK(drive_attach_disk(&drive, second) == 0, "replace D64");
    CHECK(drive.disk_attached, "replacement D64 is attached");
    CHECK(drive.virtual_drive.disk == &drive.d64,
          "virtual drive sees replacement D64");
    CHECK(drive.d64.data && drive.d64.data[0] == 0x22,
          "replacement D64 contents are live");
    CHECK(!drive.virtual_drive.channel_open[0],
          "replacement closes channels from previous media");
    CHECK(drive.virtual_drive.response_len == 0,
          "replacement discards previous media response");
    CHECK(directory_contains(&drive.virtual_drive, "SECONDFILE"),
          "DIRECTORY reads replacement D64");
    CHECK(!directory_contains(&drive.virtual_drive, "FIRSTFILE"),
          "DIRECTORY no longer reads ejected D64");

    CHECK(drive_attach_disk(&drive, NULL) == 0, "eject D64");
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

    if (failures == 0) { printf("test-drive: OK\n"); return 0; }
    printf("test-drive: %d failure(s)\n", failures);
    return 1;
}
