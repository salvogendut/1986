#include "virtual_drive.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(ok, label) do { if (!(ok)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, label); \
    failures++; \
} } while (0)

static int make_image(char *path, DiskFormat format, int track, int sector) {
    size_t size = format == DISK_FORMAT_D64 ? 174848u :
                  format == DISK_FORMAT_D71 ? 349696u : 819200u;
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)size) != 0) { close(fd); return -1; }
    size_t offset = format == DISK_FORMAT_D81
        ? (size_t)(track - 1) * 40u * 256u + (size_t)sector * 256u
        : (format == DISK_FORMAT_D71 && track > 35 ? 174848u : 0u) +
          (size_t)disk_image_d64_track_offset(track > 35 ? track - 35 : track) +
          (size_t)sector * 256u;
    u8 block[256] = {4, 'V', 'I', 'C', 'E'};
    int ok = pwrite(fd, block, sizeof(block), (off_t)offset) == sizeof(block);
    if (close(fd) != 0) ok = 0;
    return ok ? 0 : -1;
}

static void open_buffer(VirtualDrive *v, unsigned channel) {
    virtual_drive_attention(v, (u8)(0x20 | v->unit));
    virtual_drive_attention(v, (u8)(0xf0 | channel));
    virtual_drive_send(v, '#');
    virtual_drive_attention(v, 0x3f);
}

static void send_command(VirtualDrive *v, const char *command) {
    virtual_drive_attention(v, (u8)(0x20 | v->unit));
    virtual_drive_attention(v, 0x6f);
    for (const char *p = command; *p; ++p) virtual_drive_send(v, (u8)*p);
    virtual_drive_attention(v, 0x3f);
}

static void write_buffer(VirtualDrive *v, unsigned channel,
                         const u8 *bytes, size_t count) {
    virtual_drive_attention(v, (u8)(0x20 | v->unit));
    virtual_drive_attention(v, (u8)(0x60 | channel));
    for (size_t i = 0; i < count; ++i) virtual_drive_send(v, bytes[i]);
    virtual_drive_attention(v, 0x3f);
}

static size_t read_buffer(VirtualDrive *v, unsigned channel, u8 *bytes,
                          size_t cap, int *last_status) {
    virtual_drive_attention(v, (u8)(0x40 | v->unit));
    virtual_drive_attention(v, (u8)(0x60 | channel));
    size_t n = 0;
    int status = 0;
    while (n < cap && (status = virtual_drive_receive(v, &bytes[n])) > 0) {
        ++n;
        if (status == 2) break;
    }
    virtual_drive_attention(v, 0x5f);
    if (last_status) *last_status = status;
    return n;
}

static void exercise_format(DiskFormat format, int track, int sector) {
    char path[] = "/tmp/1986-vdrive-block-XXXXXX";
    CHECK(make_image(path, format, track, sector) == 0,
          "create block-command disk image");
    DiskImage disk = {0};
    CHECK(disk_image_open(&disk, path) == 0, "open block-command disk image");
    VirtualDrive v;
    virtual_drive_init(&v, 8);
    virtual_drive_attach(&v, &disk);
    open_buffer(&v, 2);
    CHECK(v.channel_direct[2] && v.block_pos[2] == 1,
          "OPEN # allocates a direct buffer with pointer one");
    open_buffer(&v, 1);
    CHECK(v.channel_direct[1] && !v.channel_save[1],
          "OPEN # on channel one is a buffer, not a PRG save");

    char command[48];
    snprintf(command, sizeof(command), "U1:2,0,%d,%d", track, sector);
    send_command(&v, command);
    CHECK(strncmp(v.status, "00,", 3) == 0 && v.block_pos[2] == 0,
          "U1 reads a sector and sets pointer zero");
    open_buffer(&v, 3);
    snprintf(command, sizeof(command), "U1:3,0,%d,%d", track, sector + 1);
    send_command(&v, command);
    CHECK(v.block_buffer[3][0] == 0 && v.block_buffer[2][0] == 4,
          "direct-access channels retain independent sector buffers");
    u8 received[256] = {0};
    int result = 0;
    size_t n = read_buffer(&v, 2, received, sizeof(received), &result);
    CHECK(n == 256 && result == 2 && received[0] == 4 &&
          memcmp(received + 1, "VICE", 4) == 0,
          "U1 streams all 256 bytes with EOI");

    snprintf(command, sizeof(command), "B-R:2,0,%d,%d", track, sector);
    send_command(&v, command);
    memset(received, 0, sizeof(received));
    n = read_buffer(&v, 2, received, sizeof(received), &result);
    CHECK(n == 4 && result == 2 && memcmp(received, "VICE", 4) == 0,
          "B-R starts at byte one and uses byte zero as length");

    send_command(&v, "B-P:2,4");
    const u8 final_byte = 'X';
    write_buffer(&v, 2, &final_byte, 1);
    CHECK(v.block_pos[2] == 0,
          "direct writes wrap at the B-R buffer length");

    snprintf(command, sizeof(command), "U1:2,0,%d,%d", track, sector);
    send_command(&v, command); /* U1 restores a full 256-byte buffer. */
    send_command(&v, "B-P:2,0");
    u8 replacement[256];
    for (unsigned i = 0; i < 256; ++i) replacement[i] = (u8)(i ^ 0x5a);
    write_buffer(&v, 2, replacement, sizeof(replacement));
    snprintf(command, sizeof(command), "U2:2,0,%d,%d", track, sector);
    send_command(&v, command);
    u8 sector_data[256] = {0};
    CHECK(strncmp(v.status, "00,", 3) == 0 &&
          disk_image_read_sector(&disk, track, sector, sector_data) == 0 &&
          memcmp(sector_data, replacement, sizeof(replacement)) == 0,
          "U2 writes the direct buffer to the live disk");
    DiskImage reopened = {0};
    CHECK(disk_image_open(&reopened, path) == 0 &&
          disk_image_read_sector(&reopened, track, sector, sector_data) == 0 &&
          memcmp(sector_data, replacement, sizeof(replacement)) == 0,
          "U2 persists the sector atomically to the host image");
    disk_image_close(&reopened);

    send_command(&v, "B-P:2,1");
    const u8 short_block[] = {'B', 'L', 'K'};
    write_buffer(&v, 2, short_block, sizeof(short_block));
    snprintf(command, sizeof(command), "B-W:2,0,%d,%d", track, sector + 1);
    send_command(&v, command);
    CHECK(disk_image_read_sector(&disk, track, sector + 1, sector_data) == 0 &&
          sector_data[0] == 3 && memcmp(sector_data + 1, "BLK", 3) == 0,
          "B-W stores the buffer pointer as byte zero length");

    send_command(&v, "U1:4,0,18,0");
    CHECK(strncmp(v.status, "70,NO CHANNEL", 13) == 0,
          "block command requires an open direct channel");
    send_command(&v, "U1:2,1,18,0");
    CHECK(strncmp(v.status, "74,DRIVE NOT READY", 18) == 0,
          "block command rejects a nonexistent internal drive");
    send_command(&v, "U1:2,0,99,0");
    CHECK(strncmp(v.status, "66,ILLEGAL TRACK OR SECTOR", 26) == 0,
          "block command rejects an invalid track");
    send_command(&v, "U1:2,0,18");
    CHECK(strncmp(v.status, "31,SYNTAX ERROR", 15) == 0,
          "incomplete block command reports syntax error");
    send_command(&v, "U8");
    CHECK(strncmp(v.status, "74,DRIVE NOT READY", 18) == 0,
          "unsupported user command no longer reports success");

    disk.writable = false;
    send_command(&v, "U2:2,0,18,0");
    CHECK(strncmp(v.status, "26,WRITE PROTECT ON", 19) == 0,
          "U2 refuses a read-only image");
    disk.writable = true;

    int fd = open(path, O_WRONLY);
    u8 external = 0xc3;
    CHECK(fd >= 0 && pwrite(fd, &external, 1, 17) == 1,
          "simulate an external image edit");
    if (fd >= 0) close(fd);
    send_command(&v, "B-P:2,0");
    replacement[0] ^= 0xff;
    write_buffer(&v, 2, replacement, sizeof(replacement));
    snprintf(command, sizeof(command), "U2:2,0,%d,%d", track, sector);
    send_command(&v, command);
    CHECK(strncmp(v.status, "25,WRITE ERROR", 14) == 0 &&
          disk_image_read_sector(&disk, track, sector, sector_data) == 0 &&
          sector_data[0] != replacement[0],
          "external edits block U2 without changing the live image");

    virtual_drive_attach(&v, NULL);
    open_buffer(&v, 2);
    send_command(&v, "U1:2,0,18,0");
    CHECK(strncmp(v.status, "74,DRIVE NOT READY", 18) == 0,
          "direct-access read reports missing media");

    virtual_drive_reset(&v);
    disk_image_close(&disk);
    unlink(path);
}

int main(void) {
    exercise_format(DISK_FORMAT_D64, 18, 0);
    exercise_format(DISK_FORMAT_D71, 53, 2);
    exercise_format(DISK_FORMAT_D81, 40, 0);
    if (!failures) puts("test-vdrive-blocks: OK");
    return failures ? 1 : 0;
}
