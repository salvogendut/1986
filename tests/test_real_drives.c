#define _POSIX_C_SOURCE 200809L
#include "c128.h"
#include "paste.h"
#include "leds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Run the machine and real ROMs without creating SDL windows/audio. */
int g_debug_enabled;
void display_set_vdc_active(Display *d, bool active) { d->vdc_active = active; }
void leds_ping(LedId id) { (void)id; }
void leds_set_drive_unit(LedId id, int unit) { (void)id; (void)unit; }
void leds_set_cpu_frequency(unsigned n) { (void)n; }
void leds_set_z80_frequency(unsigned n) { (void)n; }
void notify_post(const char *fmt, ...) { (void)fmt; }
static C128 c;
static int failures;
#define CHECK(c, msg) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; \
} } while (0)

static void release_machine(void) {
    drive_attach_disk(&c.drive, NULL);
    drive_attach_disk(&c.drive2, NULL);
    free(c.vdc.fb); free(c.vdc.display_fb);
}

static void loop_rom(u8 *rom) {
    memset(rom, 0xea, 32768);
    rom[0] = 0x4c; rom[1] = 0; rom[2] = 0x80;
    rom[0x7ffc] = 0; rom[0x7ffd] = 0x80;
}

static void synthetic_tests(void) {
    Config cfg;
    config_set_defaults(&cfg);
    cfg.real_disk_drive = true;
    cfg.second_drive = true;
    cfg.drive_type = 1581;
    cfg.drive2_type = 1571;
    c128_init(&c, &cfg);
    c.real1581[0].rom_loaded = true;
    loop_rom(c.real1581[0].rom);
    CHECK(!c128_configure_real_drives(&c), "missing second ROM falls back as a pair");
    CHECK(!c.drive_raw_iec && !c.drive2_raw_iec, "no physical/trapped mixed bus");
    c.second_real_drive.rom_loaded = true;
    loop_rom(c.second_real_drive.rom);
    CHECK(c128_configure_real_drives(&c), "1581 plus 1571 can use the shared physical bus");
    CHECK(c.drive2_raw_iec && c.iec_bus.input_hook[0] && c.iec_bus.drive2_via,
          "CIA and VIA attach to different slots of one bus");
    c128_reset(&c);
    /* Keep the host in all-RAM 8502 mode so a frame needs no machine ROM. */
    c.mem.mmu.mcr = 0x3e;
    mmu_write(&c.mem.mmu, 0xd505, 1);
    c.mem.ram[0xfffc] = 0; c.mem.ram[0xfffd] = 0x10;
    c.mem.ram[0x1000] = 0x4c; c.mem.ram[0x1001] = 0; c.mem.ram[0x1002] = 0x10;
    cpu_reset(&c.cpu);
    vic_write(&c.vic, 0xd011, 0);
    /* Synthetic idle outputs: 1581 PB4 high, 1571 PB4 low. */
    drive1581_write(&c.real1581[0], 0x4003, 0x1a);
    drive1581_write(&c.real1581[0], 0x4001, 0x10);
    via6522_write(&c.second_real_drive.via1, 2, 0x1a);
    via6522_write(&c.second_real_drive.via1, 0, 0);
    iec_bus_set_host(&c.iec_bus, 0, 0x38);
    CHECK(c.iec_bus.data_high && c.iec_bus.clock_high, "different ATN gates both release idle bus");
    drive1581_write(&c.real1581[0], 0x400d, 0x90);
    via6522_write(&c.second_real_drive.via1, 12, 1);
    iec_bus_set_host(&c.iec_bus, 8, 0x38);
    CHECK(c.real1581[0].cpu.irq && (c.second_real_drive.via1.ifr & 2) && !c.iec_bus.data_high,
          "shared ATN reaches 1581 FLAG and 1571 CA1");
    drive1581_write(&c.real1581[0], 0x4001, 0);
    CHECK(!c.iec_bus.data_high, "one drive releasing ATN ACK cannot release the other");
    via6522_write(&c.second_real_drive.via1, 0, 0x10);
    CHECK(c.iec_bus.data_high, "both acknowledgements must release DATA");
    c128_frame(&c);
    CHECK(c.real1581[0].cpu.cycles >= 40000 && c.real1581[0].cpu.cycles < 40030,
          "1581 receives 2 MHz drive clocks per PAL frame");
    CHECK(c.second_real_drive.cpu.cycles >= 20000 && c.second_real_drive.cpu.cycles < 20020,
          "1571 retains its independent 1 MHz clock on the same bus");
    CHECK(c.integrated_drive.cpu.cycles == 0 && c.real1581[1].cpu.cycles == 0,
          "inactive hardware models never execute");
    cfg.drive_type = 1571; cfg.drive2_type = 1581;
    CHECK(c.real_drive_type[0] == 1581 && c.real_drive_type[1] == 1571,
          "pending preference changes cannot change running hardware");
    CHECK(c128_enable_second_real_drive(&c, false) && !c.drive2_raw_iec,
          "second physical drive can disconnect");
    u64 before = c.second_real_drive.cpu.cycles;
    c128_frame(&c);
    CHECK(c.second_real_drive.cpu.cycles == before, "disconnected drive no longer consumes clocks");
    CHECK(c128_enable_second_real_drive(&c, true) && c.drive2_raw_iec &&
          c.real_drive_type[1] == 1571, "reconnection preserves the running model until restart");
    release_machine();
}

static bool screen_contains(const char *text) {
    char screen[1001];
    for (unsigned i = 0; i < 1000; ++i) {
        unsigned value = c.mem.ram[0x400 + i] & 0x7f;
        screen[i] = (char)(value < 32 ? value + 64 : value);
    }
    screen[1000] = 0;
    return strstr(screen, text) != NULL;
}

static void dump_screen(void) {
    for (int row = 0; row < 25; ++row) {
        for (int col = 0; col < 40; ++col) {
            unsigned v = c.mem.ram[0x400 + row * 40 + col] & 0x7f;
            fputc(v < 32 ? v + 64 : v, stderr);
        }
        fputc('\n', stderr);
    }
}

static void run_command(const char *text, unsigned frames) {
    Paste p;
    paste_init(&p);
    if (text) paste_text(&p, text);
    for (unsigned i = 0; i < frames; ++i) {
        paste_tick(&p, &c.kbd);
        c128_frame(&c);
    }
    CHECK(p.pos == p.len && !p.held, "complete keyboard paste before the next command");
    paste_free(&p);
}

static void host_case(const char *dir, const char *rom1581, const char *rom1571,
                      int first, int second) {
    Config cfg;
    config_set_defaults(&cfg);
    cfg.real_disk_drive = true;
    cfg.drive_type = first;
    cfg.drive2_type = second ? second : 1581;
    cfg.second_drive = second != 0;
    if (first == 1581 && second == 1581) {
        cfg.drive_unit = 10;
        cfg.drive2_unit = 11;
    }
    cfg.col_mode_80 = false;
    c128_init(&c, &cfg);
    CHECK(mem_load_c128_roms(&c.mem, dir) >= 3, "load private C128 ROM set");
    CHECK(drive1581_load_rom(&c.real1581[0], rom1581) &&
          drive1581_load_rom(&c.real1581[1], rom1581), "load private 1581 DOS ROM");
    if (first == 1571 || second == 1571)
        CHECK(rom1571 && drive1571cr_load_rom(&c.integrated_drive, rom1571) &&
              drive1571cr_load_rom(&c.second_real_drive, rom1571), "load private 1571 DOS ROM");
    CHECK(c128_configure_real_drives(&c), "enable unpatched KERNAL/drive IEC path");
    c128_power_cycle(&c);
    char paths[2][64] = {"/tmp/1986-real-drive-a-XXXXXX", "/tmp/1986-real-drive-b-XXXXXX"};
    u8 prg[2][258];
    unsigned count = second ? 2 : 1;
    for (unsigned slot = 0; slot < count; ++slot) {
        int fd = mkstemp(paths[slot]);
        CHECK(fd >= 0, "temporary host-test media");
        if (fd < 0) { release_machine(); return; }
        close(fd);
        int type = slot ? second : first;
        CHECK(disk_image_create_blank(paths[slot], type == 1581 ? DISK_FORMAT_D81 :
                                                                 DISK_FORMAT_D64) == DISK_SAVE_OK,
              "format test medium for selected hardware");
        DiskImage image = {0};
        CHECK(disk_image_open(&image, paths[slot]) == 0, "open test medium");
        prg[slot][0] = 0; prg[slot][1] = (u8)(0x40 + slot);
        for (unsigned i = 2; i < sizeof(prg[slot]); ++i)
            prg[slot][i] = (u8)((i ^ 0x5a) + slot * 0x31);
        CHECK(disk_image_save_prg(&image, slot ? "PROBE2" : "PROBE1", prg[slot],
                                  sizeof(prg[slot]), false) == DISK_SAVE_OK,
              "create distinct PRG fixture before hardware attachment");
        disk_image_close(&image);
        CHECK(drive_attach_disk(slot ? &c.drive2 : &c.drive, paths[slot]) == 0,
              "insert test disk through media holder");
    }
    for (unsigned i = 0; i < 1000 && !screen_contains("READY."); ++i) c128_frame(&c);
    run_command(NULL, 20);
    CHECK(screen_contains("READY."), "C128 reaches BASIC with real drives attached");
    for (unsigned slot = 0; slot < count; ++slot) {
        char command[100];
        unsigned unit = slot ? (unsigned)cfg.drive2_unit : (unsigned)cfg.drive_unit;
        snprintf(command, sizeof(command), "DIRECTORY U%u", unit);
        run_command(command, 650);
        bool directory_ok = screen_contains(slot ? "PROBE2" : "PROBE1") &&
                            screen_contains("BLOCKS FREE");
        if (!directory_ok) dump_screen();
        CHECK(directory_ok, "DIRECTORY traverses native KERNAL, shared IEC wires and selected DOS ROM");
        snprintf(command, sizeof(command), "BLOAD\"PROBE%u\",U%u,B0,P%u",
                 slot + 1, unit, 16384 + 256 * slot);
        run_command(command, 650);
        bool load_ok = !memcmp(c.mem.ram + 0x4000 + 256 * slot, prg[slot] + 2, 256);
        if (!load_ok) {
            dump_screen();
            fprintf(stderr, "models=%d/%d unit=%u hostPC=%04x drivePC=%04x lines=%d%d%d\n",
                    first, second, unit, c.cpu.pc,
                    c.real_drive_type[slot] == 1581 ? c.real1581[slot].cpu.pc :
                      (slot ? c.second_real_drive.cpu.pc : c.integrated_drive.cpu.pc),
                    c.iec_bus.atn_high, c.iec_bus.clock_high, c.iec_bus.data_high);
        }
        CHECK(load_ok, "BLOAD returns this device's exact PRG bytes to host RAM");
        DriveMonitor *monitor = slot ? &c.drive2_monitor : &c.drive_monitor;
        CHECK(monitor->last_reads && monitor->history_count, "physical reads reach per-drive monitor");
        if ((slot ? second : first) == 1581) {
            u8 saved[1538], actual[1538];
            saved[0] = 0; saved[1] = 0x60;
            for (unsigned i = 2; i < sizeof(saved); ++i)
                saved[i] = (u8)(i * 17 + unit * 3 + (i >> 8));
            memcpy(c.mem.ram + 0x6000, saved + 2, sizeof(saved) - 2);
            snprintf(command, sizeof(command),
                     "BSAVE\"SAVED%u\",U%u,B0,P24576 TO P26112", slot + 1, unit);
            run_command(command, 2000);
            run_command("PRINT DS$", 150);
            bool ok = screen_contains("00, OK") || screen_contains("00,OK");
            if (!ok) dump_screen();
            CHECK(ok, "native BSAVE finishes with DOS OK");
            DiskImage persisted = {0};
            DiskDirEntry entry;
            snprintf(command, sizeof(command), "SAVED%u", slot + 1);
            bool stored = disk_image_open(&persisted, paths[slot]) == 0 &&
                disk_image_find_file(&persisted, command, &entry) == 0 && entry.closed &&
                disk_image_read_file(&persisted, &entry, actual, sizeof(actual)) == sizeof(actual) &&
                !memcmp(actual, saved, sizeof(saved));
            CHECK(stored, "fresh host open sees closed saved file and exact bytes across physical sectors");
            disk_image_close(&persisted);
            CHECK(monitor->last_writes && c.real1581[slot].fdc.sectors_written,
                  "1581 physical writes reach monitor and committed-sector counters");
            memset(c.mem.ram + 0x6000, 0, sizeof(saved) - 2);
            snprintf(command, sizeof(command), "BLOAD\"SAVED%u\",U%u,B0,P24576", slot + 1, unit);
            run_command(command, 1500);
            CHECK(!memcmp(c.mem.ram + 0x6000, saved + 2, sizeof(saved) - 2),
                  "native BLOAD reads back the saved data");
        }
    }
    if (!second) {
        run_command("NEW", 150);
        run_command("10 PRINT\"1581 ROUNDTRIP PASSED\"", 300);
        run_command("DSAVE\"HELLO\",U8", 2000);
        run_command("PRINT DS$", 150);
        DiskImage basic_disk = {0};
        DiskDirEntry basic_entry;
        bool basic_saved = disk_image_open(&basic_disk, paths[0]) == 0 &&
              disk_image_find_file(&basic_disk, "HELLO", &basic_entry) == 0 && basic_entry.closed;
        if (!basic_saved) dump_screen();
        CHECK(basic_saved,
              "native DSAVE creates a closed BASIC program on the host image");
        disk_image_close(&basic_disk);
        c128_power_cycle(&c);
        for (unsigned i = 0; i < 1000 && !screen_contains("READY."); ++i) c128_frame(&c);
        run_command(NULL, 20);
        run_command("DLOAD\"HELLO\",U8", 1500);
        run_command("PRINT CHR$(147)", 100);
        run_command("RUN", 150);
        if (!screen_contains("1581 ROUNDTRIP PASSED")) dump_screen();
        CHECK(screen_contains("1581 ROUNDTRIP PASSED"),
              "BASIC program reloads and runs after full machine/drive power cycle");
        /* This must be a DOS write-protect error, never a virtual-drive write. */
        DiskImage before = {0};
        CHECK(disk_image_open(&before, paths[0]) == 0, "capture original disk before denied write");
        c.drive.image.writable = false;
        run_command("BSAVE\"DENIED\",U8,B0,P16384 TO P16640", 650);
        run_command("PRINT DS$", 150);
        bool protected = screen_contains("WRITE PROTECT");
        if (!protected) dump_screen();
        CHECK(protected, "native BSAVE reports DOS write protection on the real 1581");
        DiskImage after = {0};
        CHECK(disk_image_open(&after, paths[0]) == 0 && before.size == after.size &&
              !memcmp(before.data, after.data, before.size), "denied write leaves entire host image unchanged");
        disk_image_close(&before); disk_image_close(&after);
        c.drive.image.writable = true;
        int replacement = mkstemp(paths[1]);
        CHECK(replacement >= 0, "replacement media fixture");
        if (replacement >= 0) {
            close(replacement);
            CHECK(disk_image_create_blank(paths[1], DISK_FORMAT_D81) == DISK_SAVE_OK,
                  "format replacement D81");
            DiskImage fresh = {0};
            CHECK(disk_image_open(&fresh, paths[1]) == 0, "open replacement disk");
            CHECK(disk_image_save_prg(&fresh, "NEW-DISK", prg[0], sizeof(prg[0]), false) == DISK_SAVE_OK,
                  "distinct directory on replacement disk");
            disk_image_close(&fresh);
            CHECK(drive_attach_disk(&c.drive, paths[1]) == 0 && !c.real1581[0].fdc.image,
                  "media holder disconnects WD before freeing the old image");
            run_command("PRINT CHR$(147)", 150);
            run_command("DIRECTORY U8", 650);
            bool changed = screen_contains("NEW-DISK") && !screen_contains("PROBE1");
            if (!changed) dump_screen();
            CHECK(changed, "DOS invalidates cached tracks after disk replacement");
            DiskImage external = {0};
            CHECK(disk_image_open(&external, paths[1]) == 0, "capture replacement before external conflict");
            if (external.data) {
                FILE *edited = fopen(paths[1], "r+b");
                CHECK(edited != NULL, "open disposable D81 for simulated external edit");
                if (edited) {
                    external.data[0] ^= 0xff;
                    CHECK(fputc(external.data[0], edited) != EOF && fclose(edited) == 0,
                          "modify host image behind the controller");
                    run_command("PRINT CHR$(147)", 150);
                    run_command("BSAVE\"CONFLICT\",U8,B0,P16384 TO P16640", 3000);
                    run_command("PRINT DS$", 150);
                    bool failed = screen_contains("WRITE PROTECT");
                    if (!failed) {
                        dump_screen();
                        fprintf(stderr, "conflict: hostPC=%04x drivePC=%04x WD=%02x CMD=%02x T%u S%u written=%llu jobs=%02x/%02x/%02x/%02x\n",
                            c.cpu.pc, c.real1581[0].cpu.pc, c.real1581[0].fdc.status,
                            c.real1581[0].fdc.command, c.real1581[0].fdc.track,
                            c.real1581[0].fdc.sector, (unsigned long long)c.real1581[0].fdc.sectors_written,
                            c.real1581[0].ram[2], c.real1581[0].ram[3],
                            c.real1581[0].ram[4], c.real1581[0].ram[5]);
                    }
                    CHECK(failed && c.real1581[0].fdc.write_error == DISK_SAVE_IO_ERROR,
                          "host persistence failure propagates through native DOS, never reports successful SAVE");
                    DiskImage unchanged = {0};
                    CHECK(disk_image_open(&unchanged, paths[1]) == 0 &&
                          !memcmp(unchanged.data, external.data, external.size),
                          "failed native SAVE preserves external edit and all original host sectors");
                    disk_image_close(&unchanged);
                }
            }
            disk_image_close(&external);
            CHECK(drive_attach_disk(&c.drive, NULL) == 0 && !c.real1581[0].fdc.image &&
                  c.real1581[0].fdc.disk_changed,
                  "eject immediately clears controller media, even before the next frame");
            unlink(paths[1]);
        }
    }
    printf("host IEC %d/%d: DIRECTORY, BLOAD and 1581 BSAVE verified for %u device(s)\n", first, second, count);
    release_machine();
    for (unsigned slot = 0; slot < count; ++slot) unlink(paths[slot]);
}

static void optional_host_test(void) {
    const char *dir = getenv("C128_TEST_ROM_DIR");
    const char *rom1581 = getenv("C128_1581_ROM");
    const char *rom1571 = getenv("C128_TEST_1571_ROM");
    if (!dir || !rom1581) return;
    host_case(dir, rom1581, rom1571, 1581, 0);
    host_case(dir, rom1581, rom1571, 1581, 1581);
    if (rom1571) {
        host_case(dir, rom1581, rom1571, 1571, 1581);
        host_case(dir, rom1581, rom1571, 1581, 1571);
    }
}

int main(void) {
    synthetic_tests();
    optional_host_test();
    if (!failures) puts("real-drive integration tests passed");
    return failures ? 1 : 0;
}
