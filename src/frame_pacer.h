#pragma once

#include "types.h"

typedef struct {
    u64 deadline_ns;
} FramePacer;

void frame_pacer_init(FramePacer *pacer, u64 now_ns);

/* Advance by one emulated frame and return the host time left to wait.
 * Disabled pacing rebases the deadline so enabling it later never attempts
 * to make up time spent running unthrottled. */
u64 frame_pacer_schedule(FramePacer *pacer, u64 now_ns, u64 frame_ns,
                         bool enabled);
