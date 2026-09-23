#include "tape.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int failures, pulses;
#define CHECK(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; \
} } while (0)

static void pulse(void *ctx) { (*(int *)ctx)++; }

static void write_fixture(const char *path, const u8 *data, size_t len) {
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL, "open tape fixture");
    if (!file) return;
    CHECK(fwrite(data, 1, len, file) == len, "write tape fixture");
    fclose(file);
}

int main(void) {
    char path[] = "/tmp/1986-tape-test-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0, "create temporary tape fixture");
    if (fd < 0) return 1;
    close(fd);

    u8 tap[28] = {0};
    memcpy(tap, "C64-TAPE-RAW", 12);
    tap[12] = 1;
    tap[16] = 8;
    tap[20] = 0x30; /* 384 PAL cycles */
    tap[21] = 0;    /* v1 24-bit gap, 3000 cycles */
    tap[22] = 0xb8;
    tap[23] = 0x0b;
    tap[24] = 0;
    tap[25] = 0x60; /* 768 cycles */
    tap[16] = 6;
    write_fixture(path, tap, 26);
    Tape t;
    tape_init(&t);
    CHECK(tape_mount(&t, path) && t.kind == TAPE_TAP && t.position == 20,
          "mount version-1 TAP with short and extended gaps");
    CHECK(t.cycle_counter_total == 4152 && tape_counter(&t) == 0,
          "TAP mount measures recorded duration for the tape counter");
    tape_play(&t);
    tape_advance(&t, 1000, pulse, &pulses);
    CHECK(!pulses && t.position == 20, "motor-off tape does not advance");
    tape_set_motor(&t, true);
    tape_advance(&t, 383, pulse, &pulses);
    CHECK(!pulses && t.pulse_remaining == 1,
          "TAP byte represents eight-cycle units");
    s16 sound[8] = {0};
    tape_mix_audio(&t, sound, 8, true);
    CHECK(sound[0] != 0 && sound[7] == sound[0],
          "audio monitor plays the TAP signal itself");
    tape_advance(&t, 1, pulse, &pulses);
    CHECK(pulses == 1 && t.frame_edges == 1,
          "completed pulse drives CIA FLAG callback");
    tape_advance(&t, 1500, pulse, &pulses);
    CHECK(t.pulse_total == 3000 && t.pulse_remaining == 1500,
          "v1 extended gap retains its 24-bit cycle count");
    CHECK(t.cycle_counter == 1884,
          "tape counter advances by consumed recording cycles");
    tape_stop(&t);
    tape_advance(&t, 5000, pulse, &pulses);
    CHECK(pulses == 1 && t.pulse_remaining == 1500,
          "Stop freezes the tape without losing position");
    tape_play(&t);
    tape_advance(&t, 2268, pulse, &pulses);
    CHECK(pulses == 3 && !t.play_button,
          "play resumes, emits both remaining edges and stops at tape end");
    tape_rewind(&t);
    CHECK(t.position == 20 && !t.play_button && !t.scope_count &&
          t.cycle_counter == 0,
          "rewind returns TAP transport to the start");
    t.cycle_counter = 60u * 982800u;
    CHECK(tape_counter(&t) == 21,
          "mechanical counter follows VICE's reel-circumference model");
    t.position = 25;
    t.pulse_remaining = 1500;
    tape_restore_counter(&t);
    CHECK(t.cycle_counter == 1884,
          "snapshot transport restores the elapsed counter position");

    tap[16] = 5; /* cuts off the extension, must be rejected */
    write_fixture(path, tap, 23);
    CHECK(!tape_mount(&t, path) && t.kind == TAPE_TAP,
          "malformed TAP cannot replace the mounted tape");

    u8 t64[99] = {0};
    memcpy(t64, "C64 tape image file", 19);
    t64[32] = 0x00; t64[33] = 0x01;
    t64[34] = 1; t64[36] = 1;
    t64[64] = 1; t64[65] = 0x82;
    t64[66] = 0x00; t64[67] = 0x10; /* start $1000 */
    t64[68] = 0x03; t64[69] = 0x10; /* end $1003 */
    t64[72] = 96;
    memcpy(t64 + 80, "TEST FILE       ", 16);
    t64[96] = 0xaa; t64[97] = 0xbb; t64[98] = 0xcc;
    write_fixture(path, t64, sizeof(t64));
    CHECK(tape_mount(&t, path) && t.kind == TAPE_T64 && t.play_button &&
          t.file_count == 1, "mount T64 file container");
    u8 header[21] = {0};
    CHECK(tape_t64_next_header(&t, header) && header[0] == 3 &&
          header[1] == 0 && header[2] == 0x10 &&
          header[3] == 3 && header[4] == 0x10 &&
          !memcmp(header + 5, "TEST FILE", 9),
          "T64 exposes a C128 absolute-program cassette header");
    CHECK(tape_t64_read_byte(&t) == 0xaa &&
          tape_t64_read_byte(&t) == 0xbb &&
          tape_t64_read_byte(&t) == 0xcc &&
          tape_t64_read_byte(&t) == -1,
          "T64 data stream ends at the directory record boundary");
    memset(sound, 0, sizeof(sound));
    tape_mix_audio(&t, sound, 8, true);
    CHECK(sound[0] == 0, "T64 has no recorded waveform to monitor as audio");
    tape_rewind(&t);
    CHECK(tape_t64_next_header(&t, header) && tape_t64_read_byte(&t) == 0xaa,
          "T64 rewind selects the first file again");
    t64[34] = 0; /* Broken T64 headers with zero max entries are common. */
    write_fixture(path, t64, sizeof(t64));
    CHECK(tape_mount(&t, path) && t.file_count == 1,
          "zero-entry T64 header still exposes its first record");
    tape_eject(&t);
    CHECK(t.kind == TAPE_NONE && !t.data, "eject releases tape media");
    unlink(path);
    if (!failures) { puts("test-tape: OK"); return 0; }
    fprintf(stderr, "test-tape: %d failure(s)\n", failures);
    return 1;
}
