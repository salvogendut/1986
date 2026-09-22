#include "drive_monitor.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(ok, label) do { if (!(ok)) { \
    fprintf(stderr, "FAIL %s:%d: %s\\n", __FILE__, __LINE__, label); \
    failures++; \
} } while (0)

static long energy(const s16 *pcm, int count) {
    long total = 0;
    for (int i = 0; i < count; ++i)
        total += pcm[i] < 0 ? -(long)pcm[i] : pcm[i];
    return total;
}

int main(void) {
    DriveMonitor m;
    s16 pcm[882] = {0};
    DriveActivity history[DRIVE_MONITOR_HISTORY_FRAMES];
    drive_monitor_reset(&m);
    CHECK(!drive_monitor_update(&m, false, false, 2, 0, 0, 0),
          "idle mechanism leaves the activity LED dark");
    drive_monitor_mix(&m, pcm, 882, false);
    CHECK(energy(pcm, 882) == 0, "disabled audio is silent");
    drive_monitor_mix(&m, pcm, 882, true);
    CHECK(energy(pcm, 882) == 0, "idle drive is silent");

    CHECK(drive_monitor_update(&m, true, false, 2, 0, 0, 0),
          "motor start lights the activity LED");
    drive_monitor_mix(&m, pcm, 882, true);
    CHECK(energy(pcm, 882) > 0, "motor start plays mechanism recording");

    CHECK(!drive_monitor_update(&m, true, false, 2, 0, 0, 0),
          "motor alone does not invent periodic data activity");
    CHECK(drive_monitor_update(&m, true, false, 2, 0, 3, 0),
          "actual read bytes light the LED");
    CHECK(drive_monitor_update(&m, true, false, 2, 0, 3, 2),
          "actual written bytes light the LED");
    CHECK(!drive_monitor_update(&m, true, false, 2, 0, 3, 2),
          "idle transfer frame does not flash");
    CHECK(drive_monitor_update(&m, true, true, 2, 0, 3, 2),
          "physical drive LED is honored");
    CHECK(drive_monitor_update(&m, true, false, 38, 2, 3, 2),
          "head steps light the LED");
    memset(pcm, 0, sizeof(pcm));
    drive_monitor_mix(&m, pcm, 882, true);
    CHECK(energy(pcm, 882) > 0, "head step plays mechanism recording");

    size_t n = drive_monitor_history_copy(&m, history,
                                          DRIVE_MONITOR_HISTORY_FRAMES);
    CHECK(n == 8, "one history sample is recorded per video frame");
    CHECK(history[3].reads == 3 && history[3].writes == 0,
          "scope records actual read delta");
    CHECK(history[4].reads == 0 && history[4].writes == 2,
          "scope records actual write delta");
    CHECK(history[7].steps == 2 && history[7].reads == 0,
          "scope records head steps, not audio waveform");

    CHECK(drive_monitor_update(&m, false, false, 38, 2, 3, 2),
          "motor stop lights the LED once");
    for (int i = 0; i < 30; ++i) {
        memset(pcm, 0, sizeof(pcm));
        drive_monitor_mix(&m, pcm, 882, true);
    }
    CHECK(energy(pcm, 882) == 0, "spindown ends in silence");
    drive_monitor_update(&m, true, false, 38, 3, 4, 2);
    memset(pcm, 0, sizeof(pcm));
    drive_monitor_mix(&m, pcm, 882, false);
    CHECK(energy(pcm, 882) == 0, "turning audio off mutes queued sounds");
    CHECK(drive_monitor_history_copy(&m, history,
          DRIVE_MONITOR_HISTORY_FRAMES) > 0,
          "visual activity history works with audio off");

    if (!failures) puts("test-drive-monitor: OK");
    return failures ? 1 : 0;
}
