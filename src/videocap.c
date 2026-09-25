#include "videocap.h"
#include "display.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool terminal_gif_extension(const char *name, const char *dot) {
    return dot && dot > name && strlen(dot) == 4 && dot[0] == '.' &&
           tolower((unsigned char)dot[1]) == 'g' &&
           tolower((unsigned char)dot[2]) == 'i' &&
           tolower((unsigned char)dot[3]) == 'f';
}

static bool make_output_path(const char *base, const char *suffix,
                             char *out, size_t size) {
    if (!base || !base[0] || !suffix || !out || size == 0) return false;
    const char *name = base;
    for (const char *p = base; *p; ++p)
        if (*p == '/' || *p == '\\') name = p + 1;
    const char *dot = strrchr(name, '.');
    size_t stem = terminal_gif_extension(name, dot)
        ? (size_t)(dot - base) : strlen(base);
    if (stem == 0 || (size_t)snprintf(out, size, "%.*s-%s.gif",
                                      (int)stem, base, suffix) >= size)
        return false;
    return true;
}

bool videocap_output_paths(const char *base,
                           char *vic, size_t vic_size,
                           char *vdc, size_t vdc_size) {
    return make_output_path(base, "vic", vic, vic_size) &&
           make_output_path(base, "vdc", vdc, vdc_size);
}

bool videocap_unified_output_path(const char *base, char *path, size_t size) {
    return make_output_path(base, "unified", path, size);
}

static void free_paths(VideoCapture *capture) {
    free(capture->vic_path);
    free(capture->vdc_path);
    free(capture->unified_path);
    capture->vic_path = NULL;
    capture->vdc_path = NULL;
    capture->unified_path = NULL;
}

bool videocap_active(const VideoCapture *capture) {
    return capture && (capture->unified || (capture->vic && capture->vdc));
}

bool videocap_start(VideoCapture *capture, const char *base_path,
                    int gif_width, int gif_fps, bool unified) {
    if (!capture || !base_path || !base_path[0] || videocap_active(capture))
        return false;
    if (gif_width < 1 || gif_width > 1024) return false;
    if (gif_fps < 1) gif_fps = 25;
    size_t capacity = strlen(base_path) + sizeof("-unified.gif");
    int delay_cs = 100 / gif_fps;
    if (unified) {
        char *path = malloc(capacity);
        int width = gif_width * 2;
        int height = (gif_width * 3) / 4;
        size_t pixels = (size_t)width * (size_t)height;
        uint32_t *canvas = malloc(sizeof(*canvas) * pixels);
        if (!path || !canvas ||
            !videocap_unified_output_path(base_path, path, capacity)) {
            free(path);
            free(canvas);
            return false;
        }
        GifCap *encoder = gifcap_open(path, width, height, width, height,
                                      delay_cs);
        if (!encoder) {
            free(path);
            free(canvas);
            return false;
        }
        free_paths(capture);
        free(capture->unified_pixels);
        capture->unified = encoder;
        capture->unified_path = path;
        capture->unified_pixels = canvas;
        capture->unified_width = width;
        capture->unified_height = height;
        capture->interval_ns = 1000000000ULL / (uint64_t)gif_fps;
        capture->elapsed_ns = 0;
        capture->first_frame = true;
        return true;
    }

    char *vic_path = malloc(capacity);
    char *vdc_path = malloc(capacity);
    if (!vic_path || !vdc_path ||
        !videocap_output_paths(base_path, vic_path, capacity,
                               vdc_path, capacity)) {
        free(vic_path);
        free(vdc_path);
        return false;
    }
    GifCap *vic = gifcap_open(vic_path, C128_SCREEN_W, C128_SCREEN_H,
                              gif_width, (gif_width * 5) / 8, delay_cs);
    if (!vic) {
        free(vic_path);
        free(vdc_path);
        return false;
    }
    GifCap *vdc = gifcap_open(vdc_path, VDC_SCREEN_W, VDC_SCREEN_H,
                              gif_width, (gif_width * 3) / 4, delay_cs);
    if (!vdc) {
        gifcap_close(vic);
        remove(vic_path);
        free(vic_path);
        free(vdc_path);
        return false;
    }

    free_paths(capture);
    free(capture->unified_pixels);
    capture->unified_pixels = NULL;
    capture->vic = vic;
    capture->vdc = vdc;
    capture->vic_path = vic_path;
    capture->vdc_path = vdc_path;
    capture->interval_ns = 1000000000ULL / (uint64_t)gif_fps;
    capture->elapsed_ns = 0;
    capture->first_frame = true;
    return true;
}

static void compose_unified(VideoCapture *capture,
                            const uint32_t *vic_pixels,
                            const uint32_t *vdc_pixels) {
    int pane_width = capture->unified_width / 2;
    int vic_height = (pane_width * 5) / 8;
    int vic_y = (capture->unified_height - vic_height) / 2;
    memset(capture->unified_pixels, 0,
           sizeof(*capture->unified_pixels) *
           (size_t)capture->unified_width * (size_t)capture->unified_height);
    for (int y = 0; y < vic_height; ++y) {
        int source_y = (y * C128_SCREEN_H) / vic_height;
        uint32_t *out = capture->unified_pixels +
            (size_t)(y + vic_y) * capture->unified_width;
        const uint32_t *in = vic_pixels + (size_t)source_y * C128_SCREEN_W;
        for (int x = 0; x < pane_width; ++x)
            out[x] = in[(x * C128_SCREEN_W) / pane_width];
    }
    for (int y = 0; y < capture->unified_height; ++y) {
        int source_y = (y * VDC_SCREEN_H) / capture->unified_height;
        uint32_t *out = capture->unified_pixels +
            (size_t)y * capture->unified_width + pane_width;
        const uint32_t *in = vdc_pixels + (size_t)source_y * VDC_SCREEN_W;
        for (int x = 0; x < pane_width; ++x)
            out[x] = in[(x * VDC_SCREEN_W) / pane_width];
    }
}

static bool frame_due(VideoCapture *capture, uint64_t emulated_frame_ns) {
    if (capture->first_frame) {
        capture->first_frame = false;
        return true;
    }
    capture->elapsed_ns += emulated_frame_ns;
    if (capture->elapsed_ns < capture->interval_ns) return false;
    capture->elapsed_ns %= capture->interval_ns;
    return true;
}

bool videocap_frame(VideoCapture *capture, uint64_t emulated_frame_ns,
                    const uint32_t *vic_pixels,
                    const uint32_t *vdc_pixels) {
    if (!videocap_active(capture) || !vic_pixels || !vdc_pixels) return false;
    if (!frame_due(capture, emulated_frame_ns)) return true;
    if (capture->unified) {
        compose_unified(capture, vic_pixels, vdc_pixels);
        return gifcap_frame(capture->unified, capture->unified_pixels);
    }
    bool vic_ok = gifcap_frame(capture->vic, vic_pixels);
    bool vdc_ok = gifcap_frame(capture->vdc, vdc_pixels);
    return vic_ok && vdc_ok;
}

int videocap_vic_frames(const VideoCapture *capture) {
    return capture ? gifcap_frame_count(capture->vic) : 0;
}

int videocap_vdc_frames(const VideoCapture *capture) {
    return capture ? gifcap_frame_count(capture->vdc) : 0;
}

int videocap_unified_frames(const VideoCapture *capture) {
    return capture ? gifcap_frame_count(capture->unified) : 0;
}

void videocap_stop(VideoCapture *capture, int *vic_frames, int *vdc_frames,
                   int *unified_frames) {
    if (vic_frames) *vic_frames = videocap_vic_frames(capture);
    if (vdc_frames) *vdc_frames = videocap_vdc_frames(capture);
    if (unified_frames) *unified_frames = videocap_unified_frames(capture);
    if (!capture) return;
    gifcap_close(capture->vic);
    gifcap_close(capture->vdc);
    gifcap_close(capture->unified);
    capture->vic = NULL;
    capture->vdc = NULL;
    capture->unified = NULL;
    capture->interval_ns = 0;
    capture->elapsed_ns = 0;
    capture->first_frame = false;
}

void videocap_destroy(VideoCapture *capture) {
    if (!capture) return;
    videocap_stop(capture, NULL, NULL, NULL);
    free_paths(capture);
    free(capture->unified_pixels);
    capture->unified_pixels = NULL;
    capture->unified_width = 0;
    capture->unified_height = 0;
}

const char *videocap_vic_path(const VideoCapture *capture) {
    return capture && capture->vic_path ? capture->vic_path : "";
}

const char *videocap_vdc_path(const VideoCapture *capture) {
    return capture && capture->vdc_path ? capture->vdc_path : "";
}

const char *videocap_unified_path(const VideoCapture *capture) {
    return capture && capture->unified_path ? capture->unified_path : "";
}
