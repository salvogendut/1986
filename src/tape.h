#pragma once

#include "types.h"
#include <stdbool.h>
#include <stddef.h>

#define TAPE_SCOPE_SAMPLES 256

typedef enum { TAPE_NONE, TAPE_TAP, TAPE_T64 } TapeKind;

typedef struct {
    u16 start, end;
    u32 offset;
    u8 name[16];
} TapeFile;

typedef struct {
    TapeKind kind;
    u8 *data;
    size_t size;
    size_t payload_end;
    size_t position;
    unsigned version;
    bool play_button;
    bool motor_on;
    u32 pulse_total, pulse_remaining;
    u64 cycle_counter, cycle_counter_total;
    unsigned frame_edges;
    u32 scope[TAPE_SCOPE_SAMPLES];
    size_t scope_head, scope_count;
    TapeFile *files;
    size_t file_count, next_file, current_file, file_position;
} Tape;

void tape_init(Tape *t);
void tape_eject(Tape *t);
bool tape_mount(Tape *t, const char *path);
void tape_play(Tape *t);
void tape_stop(Tape *t);
void tape_rewind(Tape *t);
void tape_set_motor(Tape *t, bool motor_on);
bool tape_running(const Tape *t);
/* Three-digit mechanical-style counter modelled after VICE's datasette. */
unsigned tape_counter(const Tape *t);
/* Reconstruct elapsed cycles after restoring the persisted transport fields. */
void tape_restore_counter(Tape *t);
void tape_advance(Tape *t, unsigned cycles, void (*pulse)(void *), void *ctx);
void tape_mix_audio(const Tape *t, s16 *samples, int count, bool enabled);
/* T64 uses KERNAL tape traps; it contains files, not a recorded waveform. */
bool tape_t64_next_header(Tape *t, u8 header[21]);
int tape_t64_read_byte(Tape *t);
