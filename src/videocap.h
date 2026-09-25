#pragma once

#include "gifcap.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A C128 capture session owns either two independent GIF encoders (one for
 * VIC-IIe and one for VDC) or one encoder for a side-by-side composition.
 * Both modes use the same emulated clock, so changing the active/focused
 * display cannot affect capture timing or content. */
typedef struct {
    GifCap *vic;
    GifCap *vdc;
    GifCap *unified;
    char *vic_path;
    char *vdc_path;
    char *unified_path;
    uint32_t *unified_pixels;
    int unified_width;
    int unified_height;
    uint64_t interval_ns;
    uint64_t elapsed_ns;
    bool first_frame;
} VideoCapture;

/* Expand a requested base path into explicit per-display names. A terminal
 * .gif extension is replaced, case-insensitively:
 *   capture.gif -> capture-vic.gif + capture-vdc.gif
 * Paths without .gif receive the same suffixes and extension. */
bool videocap_output_paths(const char *base,
                           char *vic, size_t vic_size,
                           char *vdc, size_t vdc_size);
bool videocap_unified_output_path(const char *base, char *path, size_t size);

/* Open both streams atomically from the caller's perspective. gif_width is
 * shared; height follows each display's native aspect (VIC 8:5, VDC 4:3). */
bool videocap_start(VideoCapture *capture, const char *base_path,
                    int gif_width, int gif_fps, bool unified);

bool videocap_active(const VideoCapture *capture);

/* Submit both raw display framebuffers when their common emulated-time
 * cadence is due. Returns false only for an encoder failure. */
bool videocap_frame(VideoCapture *capture, uint64_t emulated_frame_ns,
                    const uint32_t *vic_pixels,
                    const uint32_t *vdc_pixels);

/* Finalize both streams. Optional counters receive each stream's frame count.
 * Output paths remain available until the next start or videocap_destroy. */
void videocap_stop(VideoCapture *capture, int *vic_frames, int *vdc_frames,
                   int *unified_frames);
void videocap_destroy(VideoCapture *capture);

const char *videocap_vic_path(const VideoCapture *capture);
const char *videocap_vdc_path(const VideoCapture *capture);
const char *videocap_unified_path(const VideoCapture *capture);
int videocap_vic_frames(const VideoCapture *capture);
int videocap_vdc_frames(const VideoCapture *capture);
int videocap_unified_frames(const VideoCapture *capture);
