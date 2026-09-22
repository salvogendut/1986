#pragma once
#include "types.h"
#include <stdbool.h>
#include <stddef.h>

#define DRIVE_MONITOR_HISTORY_FRAMES 256

typedef struct {
    unsigned reads, writes, steps;
    bool motor, led;
} DriveActivity;

/* Host-side presentation only; it must never affect emulated drive timing. */
typedef struct {
    bool motor, audible_last;
    unsigned last_steps, last_reads, last_writes;
    unsigned half_track;
    unsigned motor_stage;
    size_t motor_sample, step_sample;
    const signed char *step_clip;
    size_t step_length;
    DriveActivity history[DRIVE_MONITOR_HISTORY_FRAMES];
    size_t history_head, history_count;
} DriveMonitor;

void drive_monitor_reset(DriveMonitor *m);
/* Record one emulated video frame; return true while the real LED or actual
 * motor/byte/head activity should light the frontend drive lamp. */
bool drive_monitor_update(DriveMonitor *m, bool motor, bool led,
                          unsigned half_track, unsigned step_events,
                          unsigned read_events, unsigned write_events);
/* Mix VICE's GPL-licensed mechanism recordings into the SID PCM frame. */
void drive_monitor_mix(DriveMonitor *m, s16 *pcm, int samples, bool audible);
size_t drive_monitor_history_copy(const DriveMonitor *m, DriveActivity *out,
                                  size_t capacity);
