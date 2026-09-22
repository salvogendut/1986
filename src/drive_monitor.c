#include "drive_monitor.h"
#include "drive_sound_samples.h"
#include <string.h>

enum { MOTOR_SILENT, MOTOR_SPINUP, MOTOR_HUM, MOTOR_SPINDOWN };

void drive_monitor_reset(DriveMonitor *m) {
    memset(m, 0, sizeof(*m));
}

bool drive_monitor_update(DriveMonitor *m, bool motor, bool led,
                          unsigned half_track, unsigned step_events,
                          unsigned read_events, unsigned write_events) {
    unsigned steps = step_events - m->last_steps;
    unsigned reads = read_events - m->last_reads;
    unsigned writes = write_events - m->last_writes;
    bool motor_changed = motor != m->motor;
    m->last_steps = step_events;
    m->last_reads = read_events;
    m->last_writes = write_events;
    m->half_track = half_track;
    m->motor = motor;

    if (motor_changed) {
        m->motor_stage = motor ? MOTOR_SPINUP : MOTOR_SPINDOWN;
        m->motor_sample = 0;
    }
    if (steps) {
        if (half_track <= 2) {
            m->step_clip = bump;
            m->step_length = sizeof(bump);
        } else if (half_track < 36) {
            m->step_clip = stepping;
            m->step_length = sizeof(stepping);
        } else {
            m->step_clip = stepping2;
            m->step_length = sizeof(stepping2);
        }
        m->step_sample = 0;
    }

    m->history[m->history_head] = (DriveActivity){
        .reads = reads, .writes = writes, .steps = steps,
        .motor = motor, .led = led
    };
    m->history_head = (m->history_head + 1) % DRIVE_MONITOR_HISTORY_FRAMES;
    if (m->history_count < DRIVE_MONITOR_HISTORY_FRAMES) m->history_count++;
    return led || motor_changed || steps || reads || writes;
}

static int next_motor_sample(DriveMonitor *m) {
    int sample = 0;
    switch (m->motor_stage) {
    case MOTOR_SPINUP:
        sample = spinup[m->motor_sample++];
        if (m->motor_sample == sizeof(spinup)) {
            m->motor_stage = MOTOR_HUM;
            m->motor_sample = 0;
        }
        break;
    case MOTOR_HUM:
        sample = hum[m->motor_sample++];
        if (m->motor_sample == sizeof(hum)) m->motor_sample = 0;
        break;
    case MOTOR_SPINDOWN:
        sample = spindown[m->motor_sample++];
        if (m->motor_sample == sizeof(spindown)) {
            m->motor_stage = MOTOR_SILENT;
            m->motor_sample = 0;
        }
        break;
    default:
        break;
    }
    return sample;
}

void drive_monitor_mix(DriveMonitor *m, s16 *pcm, int samples, bool audible) {
    if (!audible || !pcm || samples <= 0) {
        if (!audible) {
            m->audible_last = false;
            m->motor_stage = MOTOR_SILENT;
            m->step_clip = NULL;
        }
        return;
    }
    if (!m->audible_last) {
        /* Enabling the monitor on a drive already spinning must not replay
         * spin-up, but it should start with the current motor sound. */
        m->motor_stage = m->motor ? MOTOR_HUM : MOTOR_SILENT;
        m->motor_sample = 0;
        m->step_clip = NULL;
        m->audible_last = true;
    }
    for (int i = 0; i < samples; ++i) {
        int sound = next_motor_sample(m) * 24;
        if (m->step_clip) {
            sound += m->step_clip[m->step_sample++] * 72;
            if (m->step_sample == m->step_length) m->step_clip = NULL;
        }
        int mixed = (int)pcm[i] + sound;
        if (mixed > 32767) mixed = 32767;
        if (mixed < -32768) mixed = -32768;
        pcm[i] = (s16)mixed;
    }
}

size_t drive_monitor_history_copy(const DriveMonitor *m, DriveActivity *out,
                                  size_t capacity) {
    if (!m || !out || !capacity) return 0;
    size_t n = m->history_count < capacity ? m->history_count : capacity;
    size_t start = (m->history_head + DRIVE_MONITOR_HISTORY_FRAMES - n) %
                   DRIVE_MONITOR_HISTORY_FRAMES;
    for (size_t i = 0; i < n; ++i)
        out[i] = m->history[(start + i) % DRIVE_MONITOR_HISTORY_FRAMES];
    return n;
}
