#pragma once
#include "types.h"
#include <stdbool.h>
#include <stddef.h>

#define DRIVE_MONITOR_WAVEFORM_SAMPLES 2048

/* Host-side presentation of the real 1571 mechanism. This is deliberately
 * separate from the emulated drive: disabling sound never changes its timing. */
typedef struct {
    bool motor;
    unsigned last_steps, last_reads;
    unsigned frame, activity_frames;
    unsigned click_remaining, click_strength;
    float motor_level, hum_phase;
    u32 noise;
    s16 waveform[DRIVE_MONITOR_WAVEFORM_SAMPLES];
    size_t waveform_head, waveform_count;
} DriveMonitor;

void drive_monitor_reset(DriveMonitor *m);
/* Returns true for a visible activity-LED pulse at the current video frame. */
bool drive_monitor_update(DriveMonitor *m, bool motor,
                          unsigned step_events, unsigned read_events);
/* Mix original, synthetic motor hum and head clicks into the SID PCM frame. */
void drive_monitor_mix(DriveMonitor *m, s16 *pcm, int samples,
                       bool audible, bool visual);
size_t drive_monitor_waveform_copy(const DriveMonitor *m, s16 *out,
                                   size_t capacity);
