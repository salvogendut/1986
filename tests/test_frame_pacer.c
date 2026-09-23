#include "frame_pacer.h"
#include <stdio.h>

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", message); \
        return 1; \
    } \
} while (0)

int main(void) {
    const u64 frame = 20000000;
    FramePacer pacer;
    frame_pacer_init(&pacer, 100000000);

    CHECK(frame_pacer_schedule(&pacer, 108000000, frame, true) == 12000000,
          "normal frame waits until its absolute deadline");
    CHECK(pacer.deadline_ns == 120000000,
          "normal frame advances the absolute deadline");

    CHECK(frame_pacer_schedule(&pacer, 145000000, frame, true) == 0,
          "small timing debt is allowed to catch up");
    CHECK(pacer.deadline_ns == 140000000,
          "small timing debt preserves cadence");

    CHECK(frame_pacer_schedule(&pacer, 225000000, frame, true) == 0,
          "large timing debt does not add a delay");
    CHECK(pacer.deadline_ns == 225000000,
          "large timing debt rebases to the current host time");
    CHECK(frame_pacer_schedule(&pacer, 230000000, frame, true) == 15000000,
          "frame after a rebase resumes normal pacing");

    CHECK(frame_pacer_schedule(&pacer, 240000000, frame, false) == 0,
          "disabled pacing never sleeps");
    CHECK(pacer.deadline_ns == 240000000,
          "disabled pacing continuously discards timing debt");
    CHECK(frame_pacer_schedule(&pacer, 241000000, frame, true) == 19000000,
          "re-enabled pacing starts from the rebased deadline");

    puts("test-frame-pacer: OK");
    return 0;
}
