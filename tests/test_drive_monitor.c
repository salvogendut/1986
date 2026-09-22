#include "drive_monitor.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(ok, label) do { if (!(ok)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, label); \
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
    drive_monitor_reset(&m);
    CHECK(!drive_monitor_update(&m, false, 0, 0),
          "idle mechanism leaves the activity LED dark");
    drive_monitor_mix(&m, pcm, 882, false, false);
    CHECK(energy(pcm, 882) == 0, "disabled audio is silent");

    CHECK(drive_monitor_update(&m, true, 0, 0),
          "motor start gives an activity pulse");
    drive_monitor_mix(&m, pcm, 882, true, false);
    CHECK(energy(pcm, 882) > 0, "motor produces audible PCM");

    int flashes = 0, dark = 0;
    for (unsigned frame = 1; frame <= 60; ++frame) {
        if (drive_monitor_update(&m, true, 0, frame)) flashes++;
        else dark++;
    }
    CHECK(flashes >= 3 && dark >= 30,
          "continuous disk reads pulse rather than pin the LED on");
    CHECK(drive_monitor_update(&m, true, 2, 61),
          "head steps flash the LED immediately");
    CHECK(m.click_remaining > 0, "head steps queue a short click");
    memset(pcm, 0, sizeof(pcm));
    drive_monitor_mix(&m, pcm, 882, true, false);
    CHECK(energy(pcm, 882) > 0 && m.click_remaining == 0,
          "head click mixes and ends within one audio frame");

    CHECK(drive_monitor_update(&m, false, 2, 61),
          "motor stop gives a final activity pulse");
    for (int i = 0; i < 50; ++i) {
        memset(pcm, 0, sizeof(pcm));
        drive_monitor_mix(&m, pcm, 882, true, false);
    }
    CHECK(energy(pcm, 882) == 0, "motor fade reaches silence");
    drive_monitor_update(&m, true, 3, 62);
    memset(pcm, 0, sizeof(pcm));
    drive_monitor_mix(&m, pcm, 882, false, false);
    CHECK(energy(pcm, 882) == 0 && m.click_remaining == 0,
          "turning the monitor off immediately mutes queued sounds");
    drive_monitor_update(&m, true, 3, 63);
    drive_monitor_mix(&m, pcm, 882, false, true);
    s16 waveform[DRIVE_MONITOR_WAVEFORM_SAMPLES];
    size_t count = drive_monitor_waveform_copy(&m, waveform,
                                               DRIVE_MONITOR_WAVEFORM_SAMPLES);
    CHECK(energy(pcm, 882) == 0 && count == 882 && energy(waveform, count) > 0,
          "visual-only monitor records a waveform without changing SID audio");
    drive_monitor_mix(&m, pcm, 882, false, false);
    CHECK(drive_monitor_waveform_copy(&m, waveform,
          DRIVE_MONITOR_WAVEFORM_SAMPLES) == 0,
          "turning the visual monitor off clears the old trace");

    if (!failures) puts("test-drive-monitor: OK");
    return failures ? 1 : 0;
}
