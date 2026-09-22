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

static unsigned led_pings[LED_COUNT];
static int led_unit[2];
void leds_ping(LedId id) { led_pings[id]++; }
void leds_set_drive_unit(LedId id, int unit) {
    if (id == LED_FDC_A || id == LED_FDC_B)
        led_unit[id == LED_FDC_B] = unit;
}

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

static bool pair_directory_contains(Drive *first, Drive *second,
                                    bool enabled, int unit, const char *name) {
    drive_pair_attention(first, second, enabled, (u8)(0x20 | unit));
    drive_pair_attention(first, second, enabled, 0xF0);
    drive_pair_send(first, second, enabled, '$');
    drive_pair_attention(first, second, enabled, 0x3F);
    drive_pair_attention(first, second, enabled, (u8)(0x40 | unit));
    drive_pair_attention(first, second, enabled, 0x60);
    u8 bytes[1024], byte;
    size_t length = 0;
    int status;
    while ((status = drive_pair_receive(first, second, enabled, &byte)) > 0 &&
           length < sizeof(bytes)) {
        bytes[length++] = byte;
        if (status == 2) break;
    }
    drive_pair_attention(first, second, enabled, 0x5F);
    size_t name_len = strlen(name);
    for (size_t i = 0; i + name_len <= length; ++i)
        if (memcmp(bytes + i, name, name_len) == 0) return true;
    return false;
}

static int block_media_change(void *ctx) {
    int *calls = ctx;
    ++*calls;
    return -1;
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
    CHECK(drive.media_generation == 1,
          "initial insert advances hardware media generation");
    CHECK(drive.disk_attached, "first DiskImage is attached");
    CHECK(drive.virtual_drive.disk == &drive.image,
          "virtual drive sees first DiskImage");
    CHECK(drive.image.data && drive.image.data[0] == 0x11,
          "first DiskImage contents are live");
    CHECK(directory_contains(&drive.virtual_drive, "FIRSTFILE"),
          "DIRECTORY reads first DiskImage");
    int blocked_calls = 0;
    drive_set_media_change_hook(&drive, block_media_change, &blocked_calls);
    CHECK(drive_attach_disk(&drive, second) == -2 &&
          blocked_calls == 1 && drive.media_generation == 1 &&
          drive.image.data && drive.image.data[0] == 0x11,
          "failed physical write flush keeps old media and generation");
    drive_set_media_change_hook(&drive, NULL, NULL);
    cfg.real_disk_drive = true;
    drive_reset(&drive);
    CHECK(directory_contains(&drive.virtual_drive, "FIRSTFILE"),
          "pending real-drive preference keeps virtual backend available");

    CHECK(drive_attach_disk(&drive, second) == 0, "replace DiskImage");
    CHECK(drive.media_generation == 2,
          "replacement advances hardware media generation");
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

    Drive pair_first, pair_second;
    drive_init(&pair_first, &cfg);
    drive_init(&pair_second, &cfg);
    drive_set_slot(&pair_second, 1);
    drive_set_unit(&pair_second, 9);
    CHECK(led_unit[0] == 8 && led_unit[1] == 9,
          "each LED shows its drive's IEC unit");
    CHECK(drive_attach_disk(&pair_first, first) == 0 &&
          drive_attach_disk(&pair_second, second) == 0,
          "attach independent images to both drives");
    memset(led_pings, 0, sizeof(led_pings));
    CHECK(pair_directory_contains(&pair_first, &pair_second, true, 8, "FIRSTFILE") &&
          led_pings[LED_FDC_A] > 0 && led_pings[LED_FDC_B] == 0,
          "Drive 1 traffic lights only Drive 1 LED");
    memset(led_pings, 0, sizeof(led_pings));
    CHECK(pair_directory_contains(&pair_first, &pair_second, true, 9, "SECONDFILE") &&
          led_pings[LED_FDC_B] > 0 && led_pings[LED_FDC_A] == 0,
          "Drive 2 traffic lights only Drive 2 LED");
    CHECK(pair_directory_contains(&pair_first, &pair_second, true, 8, "FIRSTFILE") &&
          !pair_directory_contains(&pair_first, &pair_second, true, 8, "SECONDFILE") &&
          pair_directory_contains(&pair_first, &pair_second, true, 9, "SECONDFILE"),
          "distinct IEC units route directory requests to the right image");
    CHECK(!pair_directory_contains(&pair_first, &pair_second, false, 9, "SECONDFILE") &&
          pair_directory_contains(&pair_first, &pair_second, false, 8, "FIRSTFILE"),
          "disabling the second drive removes only its IEC response");
    drive_set_unit(&pair_second, 10);
    CHECK(led_unit[1] == 10, "Drive 2 LED tracks Media device number");
    CHECK(!pair_directory_contains(&pair_first, &pair_second, true, 9, "SECONDFILE") &&
          pair_directory_contains(&pair_first, &pair_second, true, 10, "SECONDFILE"),
          "changing Drive 2 unit takes effect immediately");
    CHECK(drive_attach_disk(&pair_second, d71) == 0 &&
          pair_directory_contains(&pair_first, &pair_second, true, 10, "SEVENTYONE") &&
          pair_directory_contains(&pair_first, &pair_second, true, 8, "FIRSTFILE"),
          "Drive 2 media replacement does not disturb Drive 1");
    drive_attach_disk(&pair_first, NULL);
    drive_attach_disk(&pair_second, NULL);

    unsigned before_eject = drive.media_generation;
    CHECK(drive_attach_disk(&drive, NULL) == 0, "eject DiskImage");
    CHECK(drive.media_generation == before_eject + 1,
          "eject advances hardware media generation");
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
