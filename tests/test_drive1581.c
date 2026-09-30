#define _POSIX_C_SOURCE 200809L
#include "drive1581.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(c, msg) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; \
} } while (0)

static void install(Drive1581 *d) {
    memset(d->rom, 0xea, sizeof(d->rom));
    d->rom[0] = 0x4c; d->rom[1] = 0; d->rom[2] = 0x80; /* JMP $8000 */
    d->rom[0x1000] = 0x40; /* RTI at $9000 */
    d->rom[0x7ffc] = 0; d->rom[0x7ffd] = 0x80;
    d->rom[0x7ffe] = 0; d->rom[0x7fff] = 0x90;
    d->rom_loaded = true;
    drive1581_reset(d);
}

static void port_hook(void *ctx, u8 pins) { *(u8 *)ctx = pins; }

static void rom_size_test(Drive1581 *d) {
    char path[] = "/tmp/1986-1581-rom-size-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0, "create synthetic ROM fixture");
    if (fd < 0) return;
    FILE *file = fdopen(fd, "wb");
    CHECK(file != NULL, "open ROM fixture stream");
    if (!file) { close(fd); unlink(path); return; }
    CHECK(fwrite(d->rom, 1, sizeof(d->rom) - 1, file) == sizeof(d->rom) - 1 &&
          fflush(file) == 0, "write short synthetic ROM");
    CHECK(!drive1581_load_rom(d, path) && d->rom[0] == 0x4c,
          "short ROM rejected without destroying the existing ROM");
    CHECK(fputc(0x90, file) != EOF && fflush(file) == 0, "complete 32 KiB fixture");
    CHECK(drive1581_load_rom(d, path), "exactly 32 KiB ROM accepted");
    CHECK(fputc(0, file) != EOF && fflush(file) == 0, "extend synthetic ROM");
    CHECK(!drive1581_load_rom(d, path) && d->rom[0] == 0x4c,
          "oversized ROM rejected without replacing the existing ROM");
    fclose(file);
    unlink(path);
}

static bool contains(const u8 *bytes, size_t size, const char *text) {
    size_t length = strlen(text);
    for (size_t i = 0; i + length <= size; ++i)
        if (!memcmp(bytes + i, text, length)) return true;
    return false;
}

static void optional_rom_test(void) {
    const char *path = getenv("C128_1581_ROM");
    if (!path || !*path) return;
    Drive1581 d;
    drive1581_init(&d);
    CHECK(drive1581_load_rom(&d, path), "user-supplied 32 KiB 1581 DOS ROM loads");
    if (!d.rom_loaded) return;
    drive1581_reset(&d);
    for (int i = 0; i < 6000; ++i) drive1581_advance(&d, 1000);
    printf("1581 ROM: PC=$%04x cycles=%llu DDRA=$%02x DDRB=$%02x LED=%d\n",
           d.cpu.pc, (unsigned long long)d.cpu.cycles, d.cia.ddra, d.cia.ddrb, d.led);
    CHECK(!d.cpu.jammed && d.cpu.cycles >= 6000000,
          "real DOS ROM runs for three seconds without a CPU jam");
    CHECK(d.cia.ddra == 0x65 && d.cia.ddrb == 0x3a && !d.led,
          "real DOS ROM initializes the board and extinguishes startup LED");
    CHECK(contains(d.ram, sizeof(d.ram), "73,COPYRIGHT CBM DOS V10 1581,00,00"),
          "DOS startup generates the normal channel-15 identification string");

    char path_image[] = "/tmp/1986-1581-rom-test-XXXXXX";
    int fd = mkstemp(path_image);
    CHECK(fd >= 0, "create disposable ROM-test D81");
    if (fd < 0) return;
    close(fd);
    DiskImage image = {0};
    if (disk_image_create_blank(path_image, DISK_FORMAT_D81) != DISK_SAVE_OK ||
        disk_image_open(&image, path_image)) {
        CHECK(false, "format and open disposable D81");
        unlink(path_image);
        return;
    }
    CHECK(wd1770_attach(&d.fdc, &image), "DOS test medium attaches");
    /* DOS 318045-02: jobs $02..$0a, logical T/S pairs at $0b,
     * buffer page numbers at $01f1. Submit ordinary read jobs, leaving ROM
     * IRQs, seeking, media-change handling and WD data transfer untouched.
     * Job 0's buffer is separate from the ROM's physical-track cache. */
    static const u8 addresses[][2] = {{40, 0}, {1, 0}, {1, 19}, {1, 20}, {80, 39}};
    for (unsigned job = 0; job < sizeof(addresses)/sizeof(addresses[0]); ++job) {
        unsigned track = addresses[job][0], block = addresses[job][1];
        if (track != 40)
            for (unsigned b = 0; b < 256; ++b)
                image.data[((track - 1) * 40 + block) * 256 + b] = (u8)(b + block * 7);
    }
    for (unsigned job = 0; job < sizeof(addresses)/sizeof(addresses[0]); ++job) {
        unsigned track = addresses[job][0], block = addresses[job][1];
        d.ram[0x0b] = (u8)track;
        d.ram[0x0c] = (u8)block;
        d.ram[0x02] = 0x80;
        for (int i = 0; i < 10000 && (d.ram[2] & 0x80); ++i)
            drive1581_advance(&d, 1000);
        unsigned buffer = (unsigned)d.ram[0x1f1] << 8;
        u8 sector[256];
        CHECK(disk_image_read_sector(&image, (int)track, (int)block, sector) == 0,
              "read expected fixture sector");
        bool matched = d.ram[2] == 0 && buffer + 256 <= sizeof(d.ram) &&
                       !memcmp(d.ram + buffer, sector, 256);
        if (!matched) fprintf(stderr, "DOS job T%u S%u status=$%02x PC=$%04x buffer=$%04x\n",
                              track, block, d.ram[2], d.cpu.pc, buffer);
        CHECK(matched,
              "DOS read job returns exact logical sector through CPU/CIA/WD1770");
        /* Let the controller IRQ retire and observe the empty job slot
         * before submitting another job from this external test harness. */
        drive1581_advance(&d, 100000);
    }
    printf("1581 ROM: five DOS read jobs, %llu physical sectors transferred\n",
           (unsigned long long)d.fdc.sectors_read);
    wd1770_attach(&d.fdc, NULL);
    disk_image_close(&image);
    unlink(path_image);
}

int main(void) {
    Drive1581 d, other;
    drive1581_init(&d); drive1581_init(&other);
    CHECK(drive1581_step(&d) == 0 && drive1581_read(&d, 0x8000) == 0xff,
          "unloaded board does not execute garbage ROM");
    drive1581_write(&d, 0x07ff, 0x11);
    drive1581_write(&d, 0x0fff, 0x22);
    drive1581_write(&d, 0x1fff, 0x33);
    drive1581_write(&d, 0x3fff, 0x44);
    CHECK(drive1581_read(&d, 0x7ff) == 0x11 && drive1581_read(&d, 0xfff) == 0x22 &&
          drive1581_read(&d, 0x1fff) == 0x33 && drive1581_read(&d, 0x3fff) == 0xff,
          "all 8 KiB RAM are distinct; $2000-$3fff is open bus");
    install(&d); install(&other);
    CHECK(d.cpu.pc == 0x8000 && d.cpu.sp == 0xfd, "reset reads ROM vector");
    drive1581_write(&d, 0xffff, 0);
    CHECK(drive1581_read(&d, 0xffff) == 0x90, "ROM is read-only");
    CHECK(!drive1581_load_rom(&d, NULL) && d.rom_loaded && d.rom[0] == 0x4c,
          "failed ROM replacement retains the working ROM");
    rom_size_test(&d);
    drive1581_write(&d, 0x7ffd, 0x12);
    CHECK(drive1581_read(&d, 0x6001) == 0x12, "WD registers mirror across $6000-$7fff");
    drive1581_write(&d, 0x5ff2, 0x65);
    drive1581_write(&d, 0x4000, 0x40);
    CHECK(d.cia.ddra == 0x65 && d.fdc.motor && d.fdc.side == 1 && d.led,
          "mirrored CIA DDRA/PRA drive side, active-low motor, LED");
    drive1581_write(&d, 0x4000, 5);
    CHECK(!d.fdc.motor && d.fdc.side == 0 && !d.led, "PRA controls opposite pin levels");
    drive1581_write(&d, 0x4002, 0);
    CHECK(!d.fdc.motor && d.led, "input pins have output-side pullups");
    for (unsigned unit = 8; unit <= 11; ++unit) {
        CHECK(drive1581_set_unit(&d, unit), "valid hardware unit accepted");
        CHECK((drive1581_read(&d, 0x4000) & 0x18) == (unit - 8) * 8,
              "unit straps are CIA PA3/PA4");
    }
    CHECK(!drive1581_set_unit(&d, 12) && d.unit == 11, "invalid strap address rejected");
    CHECK(!(drive1581_read(&d, 0x4000) & 0x80), "disk-change pin is active-low");
    d.fdc.disk_changed = false;
    CHECK(drive1581_read(&d, 0x4000) & 0x80, "cleared change releases PA7");
    u8 output = 0;
    drive1581_set_port_hook(&d, port_hook, &output);
    drive1581_write(&d, 0x4003, 0x1a);
    drive1581_write(&d, 0x4001, 0x10);
    CHECK(output == 0xf5 && drive1581_clock_released(&d) && drive1581_data_released(&d),
          "IEC callback sees effective port pins; idle lines are released");
    drive1581_write(&d, 0x400d, 0x90); /* CIA FLAG interrupt */
    drive1581_set_iec(&d, false, false, false);
    CHECK(!drive1581_data_released(&d) && d.cpu.irq &&
          (drive1581_read(&d, 0x4001) & 0x85) == 0x85,
          "ATN gate acknowledges and inverted IEC inputs reach CIA/FLAG IRQ");
    CHECK((drive1581_read(&d, 0x400d) & 0x90) == 0x90 && !d.cpu.irq,
          "CIA ICR read clears CPU IRQ");
    drive1581_write(&d, 0x4001, 0);
    CHECK(drive1581_data_released(&d), "clearing PB4 releases ATN acknowledgement");
    drive1581_set_iec(&d, true, true, true);
    drive1581_write(&d, 0x4001, 0x0a);
    CHECK(!drive1581_data_released(&d) && !drive1581_clock_released(&d),
          "PB1/PB3 pull the shared lines low");
    CHECK(!(drive1581_read(&d, 0x4001) & 0x40), "no media asserts PB6 write protection");
    DiskImage pins_image = {.writable = true};
    d.fdc.image = &pins_image;
    CHECK(drive1581_read(&d, 0x4001) & 0x40, "writable medium releases PB6 protection");
    pins_image.writable = false;
    CHECK(!(drive1581_read(&d, 0x4001) & 0x40), "read-only medium asserts PB6 protection");
    d.fdc.image = NULL;

    drive1581_reset(&d);
    drive1581_write(&d, 0x400d, 0x81);
    drive1581_write(&d, 0x4004, 3);
    drive1581_write(&d, 0x4005, 0);
    drive1581_write(&d, 0x400e, 0x19); /* start, one-shot, force load */
    CHECK(drive1581_step(&d) == 3 && d.cpu.irq, "CIA timer clocks at drive CPU speed");
    d.cpu.p &= (u8)~4;
    CHECK(drive1581_step(&d) == 7 && d.cpu.pc == 0x9000 && d.cpu.sp == 0xfa,
          "CIA IRQ enters the ROM vector using the drive's own stack");
    drive1581_read(&d, 0x400d);
    CHECK(drive1581_step(&d) == 6 && d.cpu.pc == 0x8000 && d.cpu.sp == 0xfd,
          "RTI resumes ROM after timer interrupt");
    drive1581_reset(&d);
    for (int i = 0; i < 10000; ++i) drive1581_advance(&d, 1);
    CHECK(d.cpu.cycles >= 10000 && d.cpu.cycles <= 10002 &&
          d.fdc.cycles == d.cpu.cycles, "small clock slices carry instruction overshoot");
    CHECK(other.cpu.cycles == 0 && other.ram[0x1fff] == 0 && !other.fdc.motor,
          "second board has independent CPU, RAM and peripherals");
    CHECK(d.ram[0x1fff] == 0x33, "reset preserves RAM");
    drive1581_power_cycle(&d);
    CHECK(d.ram[0x1fff] == 0 && d.cpu.pc == 0x8000 && d.rom_loaded && d.unit == 11,
          "power cycle clears RAM but preserves ROM and unit straps");
    optional_rom_test();
    if (!failures) puts("1581 board tests passed");
    return failures ? 1 : 0;
}
