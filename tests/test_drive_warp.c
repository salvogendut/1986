#include "c128.h"
#include "frame_pacer.h"
#include <stdio.h>

#define CHECK(condition, message) do { if (!(condition)) { \
    fprintf(stderr, "FAIL: %s\n", message); return 1; } } while (0)

/* No ROMs or windows needed: only live mechanism state and host pacing. */
static C128 c;

int main(void) {
    Config cfg;
    config_set_defaults(&cfg);
    c.cfg = &cfg;
    c.drive_raw_iec = true;
    c.integrated_drive.gcr.motor = true;
    c.integrated_drive.gcr.led = true;
    CHECK(!c128_drive_warp_active(&c), "default off even with a busy real drive");
    cfg.unthrottled_drive = true;
    CHECK(c128_drive_warp_active(&c), "first real drive busy LED requests warp");
    c.integrated_drive.gcr.led = false;
    CHECK(!c128_drive_warp_active(&c), "motor spin-down alone must not warp");
    c.integrated_drive.gcr.write_mode = true;
    CHECK(c128_drive_warp_active(&c), "write gate accelerates even with LED off");
    c.integrated_drive.gcr.write_mode = false;
    c.integrated_drive.gcr.led = true;

    const u64 frame = 20000000;
    FramePacer pacer;
    frame_pacer_init(&pacer, 100000000);
    CHECK(frame_pacer_schedule(&pacer, 105000000, frame,
          !c128_drive_warp_active(&c)) == 0, "active drive bypasses host delay");

    c.integrated_drive.gcr.motor = false;
    c.integrated_drive.gcr.led = true;
    c.integrated_drive.gcr.read_events = 123;
    c.integrated_drive.gcr.write_events = 456;
    CHECK(!c128_drive_warp_active(&c), "idle error LED or old activity is not warp");
    CHECK(frame_pacer_schedule(&pacer, 107000000, frame,
          !c128_drive_warp_active(&c)) == 18000000,
          "motor stop restores pacing without accumulated timing debt");

    c.second_real_drive.gcr.motor = true;
    c.second_real_drive.gcr.led = true;
    CHECK(!c128_drive_warp_active(&c), "disconnected second drive ignored");
    c.drive2_raw_iec = true;
    CHECK(c128_drive_warp_active(&c), "second drive alone requests warp");
    c.integrated_drive.gcr.motor = true;
    c.second_real_drive.gcr.motor = false;
    CHECK(c128_drive_warp_active(&c), "one motor stopping cannot cancel the other");
    c.paused = true;
    CHECK(!c128_drive_warp_active(&c), "pause overrides a spinning drive");
    c.debug.step_pending = true;
    CHECK(!c128_drive_warp_active(&c), "debug single-step remains paced");
    c.paused = c.debug.step_pending = false;
    cfg.unthrottled_drive = false;
    CHECK(!c128_drive_warp_active(&c), "toggle off cancels warp immediately");

    cfg.unthrottled_drive = true;
    cfg.real_disk_drive = true;
    c.drive_raw_iec = c.drive2_raw_iec = false;
    c.second_real_drive.gcr.motor = true;
    CHECK(!c128_drive_warp_active(&c),
          "fast fallback or pending real-drive restart cannot trigger warp");
    cfg.real_disk_drive = false;
    c.drive_raw_iec = true;
    CHECK(c128_drive_warp_active(&c),
          "live real backend remains authoritative until restart");
    c.integrated_drive.gcr.motor = false;
    CHECK(!c128_drive_warp_active(&c), "reset/stopped drive restores normal speed");
    puts("test-drive-warp: OK");
    return 0;
}
