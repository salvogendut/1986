#include "sid.h"
#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

static long energy(const s16 *samples, int count) {
    long sum = 0;
    for (int i = 0; i < count; ++i) sum += abs(samples[i]);
    return sum;
}

static void tone(Sid *sid, int voice, u16 frequency, u8 waveform) {
    u16 base = (u16)(0xD400 + voice * 7);
    sid_write(sid, base, (u8)frequency);
    sid_write(sid, (u16)(base + 1), (u8)(frequency >> 8));
    sid_write(sid, (u16)(base + 2), 0x00);
    sid_write(sid, (u16)(base + 3), 0x08); /* 50% pulse width */
    sid_write(sid, (u16)(base + 5), 0x00); /* fastest attack/decay */
    sid_write(sid, (u16)(base + 6), 0xF0); /* full sustain, fastest release */
    sid_write(sid, (u16)(base + 4), (u8)(waveform | 1));
}

int main(void) {
    Sid sid;
    s16 samples[1024];
    sid_init(&sid);

    int count = sid_clock(&sid, 19656, samples, 1024);
    CHECK(count == 882, "one PAL frame generates 882 samples at 44.1 kHz");
    CHECK(energy(samples, count) == 0, "reset SID is silent");

    sid_write(&sid, 0xD418, 0x0F);
    tone(&sid, 0, 0x1D45, 0x20); /* sawtooth */
    count = sid_clock(&sid, 19656, samples, 1024);
    CHECK(count == 882 && energy(samples, count) > 100000,
          "gated sawtooth produces audible PCM");
    CHECK(sid.voice[0].env == 255, "attack reaches full sustain");

    tone(&sid, 1, 0x1200, 0x10); /* triangle */
    tone(&sid, 2, 0x1900, 0x40); /* pulse */
    count = sid_clock(&sid, 19656, samples, 1024);
    CHECK(energy(samples, count) > 100000, "three voices mix to PCM");
    CHECK(sid_read(&sid, 0xD41C) == 255, "ENV3 exposes voice-three envelope");
    CHECK(sid_read(&sid, 0xD41B) != 0, "OSC3 exposes voice-three oscillator");

    sid_write(&sid, 0xD418, 0);
    count = sid_clock(&sid, 19656, samples, 1024);
    CHECK(energy(samples, count) == 0, "master volume zero silences output");

    sid_write(&sid, 0xD418, 0x8F); /* voice-three off, unfiltered */
    sid_write(&sid, 0xD404, 0);    /* voice one off */
    sid_write(&sid, 0xD40B, 0);    /* voice two off */
    count = sid_clock(&sid, 19656, samples, 1024);
    CHECK(energy(samples, count) == 0, "voice-three-off mutes unfiltered voice");
    sid_write(&sid, 0xD417, 0x04); /* route voice three through filter */
    sid_write(&sid, 0xD416, 0x80);
    sid_write(&sid, 0xD418, 0x9F); /* low-pass + voice-three-off */
    count = sid_clock(&sid, 19656, samples, 1024);
    CHECK(energy(samples, count) > 0, "filtered voice three remains audible");

    sid_write(&sid, 0xD412, 0x40); /* release voice three */
    sid_clock(&sid, 19656, NULL, 0);
    CHECK(sid_read(&sid, 0xD41C) == 0, "voice-three envelope releases to zero");

    sid_write(&sid, 0xD498, 0x07); /* $D400-$D41F mirror */
    CHECK(sid_read(&sid, 0xD418) == 0x07, "SID register mirrors through $D4FF");

    sid_reset(&sid);
    sid_write(&sid, 0xD418, 0x0F);
    tone(&sid, 2, 0x4000, 0x80); /* noise */
    count = sid_clock(&sid, 19656, samples, 1024);
    CHECK(energy(samples, count) > 0 && sid.voice[2].noise != 0x7FFFF8,
          "noise waveform advances the SID shift register and produces PCM");

    sid_reset(&sid);
    sid_write(&sid, 0xD400, 0xFF);
    sid_write(&sid, 0xD401, 0xFF);
    sid_write(&sid, 0xD40E, 0xFF);
    sid_write(&sid, 0xD40F, 0xFF);
    sid_write(&sid, 0xD404, 0x22); /* voice one saw + hard sync to voice three */
    sid_clock(&sid, 129, NULL, 0);
    CHECK(sid.voice[0].phase == 0, "source MSB rise hard-syncs the target oscillator");

    sid_reset(&sid);
    CHECK(sid_read(&sid, 0xD41C) == 0 && sid.regs[0x18] == 0,
          "reset clears registers and voice state");

    if (failures == 0) { puts("test-sid: OK"); return 0; }
    printf("test-sid: %d failure(s)\n", failures);
    return 1;
}
