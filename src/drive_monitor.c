#include "drive_monitor.h"
#include "sid.h"
#include <math.h>
#include <string.h>

#define CLICK_SAMPLES 330u
#define TWO_PI 6.2831853071795864769f

void drive_monitor_reset(DriveMonitor *m) {
    memset(m, 0, sizeof(*m));
    m->noise = 0x1571c128u;
}

bool drive_monitor_update(DriveMonitor *m, bool motor,
                          unsigned step_events, unsigned read_events) {
    unsigned steps = step_events - m->last_steps;
    unsigned reads = read_events - m->last_reads;
    bool motor_changed = motor != m->motor;
    m->last_steps = step_events;
    m->last_reads = read_events;
    m->motor = motor;

    if (steps) {
        m->click_remaining = CLICK_SAMPLES;
        m->click_strength = steps > 4 ? 4 : steps;
    }
    if (reads) {
        if (!m->activity_frames) m->frame = 0;
        m->activity_frames = 18; /* tolerate short gaps in the ROM byte loop */
    }
    bool pulse = motor_changed || steps != 0;
    if (m->activity_frames) {
        /* A 120 ms footer glow with 300 ms pulses reads as disk activity,
         * not as an always-on lamp while the disk merely spins. */
        if (m->frame % 15 == 0) pulse = true;
        m->activity_frames--;
        m->frame++;
    }
    return pulse;
}

void drive_monitor_mix(DriveMonitor *m, s16 *pcm, int samples,
                       bool audible, bool visual) {
    if ((!audible && !visual) || !pcm || samples <= 0) {
        if (!audible && !visual) {
            m->motor_level = 0;
            m->click_remaining = 0;
            m->waveform_count = 0;
        }
        return;
    }
    for (int i = 0; i < samples; ++i) {
        /* Smooth motor on/off to avoid a digital pop. Approximate a quiet
         * rotating spindle with two harmonics, rather than VICE's samples. */
        float target = m->motor ? 1.0f : 0.0f;
        m->motor_level += (target - m->motor_level) * 0.0015f;
        m->hum_phase += 110.0f / (float)SID_SAMPLE_RATE;
        if (m->hum_phase >= 1.0f) m->hum_phase -= 1.0f;
        float phase = TWO_PI * m->hum_phase;
        float sound = m->motor_level *
            (560.0f * sinf(phase) + 130.0f * sinf(2.0f * phase));
        if (m->click_remaining) {
            m->noise ^= m->noise << 13;
            m->noise ^= m->noise >> 17;
            m->noise ^= m->noise << 5;
            float noise = (float)((int)(m->noise & 0xffff) - 32768) / 32768.0f;
            float envelope = (float)m->click_remaining / (float)CLICK_SAMPLES;
            sound += noise * envelope * (1600.0f + 300.0f * m->click_strength);
            m->click_remaining--;
        }
        if (visual) {
            m->waveform[m->waveform_head] = (s16)sound;
            m->waveform_head = (m->waveform_head + 1) %
                               DRIVE_MONITOR_WAVEFORM_SAMPLES;
            if (m->waveform_count < DRIVE_MONITOR_WAVEFORM_SAMPLES)
                m->waveform_count++;
        }
        if (audible) {
            int mixed = (int)pcm[i] + (int)sound;
            if (mixed > 32767) mixed = 32767;
            if (mixed < -32768) mixed = -32768;
            pcm[i] = (s16)mixed;
        }
    }
}

size_t drive_monitor_waveform_copy(const DriveMonitor *m, s16 *out,
                                   size_t capacity) {
    if (!m || !out || !capacity) return 0;
    size_t n = m->waveform_count < capacity ? m->waveform_count : capacity;
    size_t start = (m->waveform_head + DRIVE_MONITOR_WAVEFORM_SAMPLES - n) %
                   DRIVE_MONITOR_WAVEFORM_SAMPLES;
    for (size_t i = 0; i < n; ++i)
        out[i] = m->waveform[(start + i) % DRIVE_MONITOR_WAVEFORM_SAMPLES];
    return n;
}
