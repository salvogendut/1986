#include "frame_pacer.h"

#define MAX_CATCHUP_FRAMES 3u

void frame_pacer_init(FramePacer *pacer, u64 now_ns) {
    pacer->deadline_ns = now_ns;
}

u64 frame_pacer_schedule(FramePacer *pacer, u64 now_ns, u64 frame_ns,
                         bool enabled) {
    if (!enabled || !frame_ns) {
        pacer->deadline_ns = now_ns;
        return 0;
    }

    pacer->deadline_ns += frame_ns;
    if (now_ns < pacer->deadline_ns)
        return pacer->deadline_ns - now_ns;

    /* Preserve cadence across an occasional late frame, but discard stale
     * timing debt after a debugger stop, file dialog, suspend, or slow host
     * frame. Otherwise the emulator can run flat-out for seconds trying to
     * reproduce frames the user could never have observed. */
    u64 lag_ns = now_ns - pacer->deadline_ns;
    if (lag_ns > frame_ns * MAX_CATCHUP_FRAMES)
        pacer->deadline_ns = now_ns;
    return 0;
}
