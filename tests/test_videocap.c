#define _POSIX_C_SOURCE 200809L
#include "videocap.h"
#include "display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

static bool gif_dimensions(const char *path, int *width, int *height) {
    unsigned char header[10];
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    bool ok = fread(header, 1, sizeof(header), file) == sizeof(header) &&
              memcmp(header, "GIF89a", 6) == 0;
    fclose(file);
    if (!ok) return false;
    *width = header[6] | (header[7] << 8);
    *height = header[8] | (header[9] << 8);
    return true;
}

static bool file_contains_rgb(const char *path, unsigned char red,
                              unsigned char green, unsigned char blue) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    unsigned char previous[2] = { 0, 0 };
    int value;
    bool found = false;
    while ((value = fgetc(file)) != EOF) {
        if (previous[0] == red && previous[1] == green &&
            (unsigned char)value == blue) {
            found = true;
            break;
        }
        previous[0] = previous[1];
        previous[1] = (unsigned char)value;
    }
    fclose(file);
    return found;
}

int main(void) {
    char vic_path[256], vdc_path[256];
    CHECK(videocap_output_paths("demo.GIF", vic_path, sizeof(vic_path),
                                vdc_path, sizeof(vdc_path)) &&
          strcmp(vic_path, "demo-vic.gif") == 0 &&
          strcmp(vdc_path, "demo-vdc.gif") == 0,
          "terminal GIF extension becomes explicit VIC/VDC suffixes");
    CHECK(videocap_output_paths("captures/demo", vic_path, sizeof(vic_path),
                                vdc_path, sizeof(vdc_path)) &&
          strcmp(vic_path, "captures/demo-vic.gif") == 0 &&
          strcmp(vdc_path, "captures/demo-vdc.gif") == 0,
          "extensionless capture base gets two GIF names");
    char unified_path[256];
    CHECK(videocap_unified_output_path("demo.gif", unified_path,
                                       sizeof(unified_path)) &&
          strcmp(unified_path, "demo-unified.gif") == 0,
          "unified capture has a deterministic output name");
    CHECK(!videocap_output_paths("", vic_path, sizeof(vic_path),
                                 vdc_path, sizeof(vdc_path)),
          "empty capture path is rejected");

    char marker[] = "/tmp/1986-videocap-XXXXXX";
    int fd = mkstemp(marker);
    CHECK(fd >= 0, "reserve unique capture basename");
    if (fd < 0) return 1;
    close(fd);
    unlink(marker);
    char base[256];
    snprintf(base, sizeof(base), "%s", marker);

    uint32_t *vic_pixels = malloc(sizeof(*vic_pixels) *
                                  C128_SCREEN_W * C128_SCREEN_H);
    uint32_t *vdc_pixels = malloc(sizeof(*vdc_pixels) *
                                  VDC_SCREEN_W * VDC_SCREEN_H);
    CHECK(vic_pixels && vdc_pixels, "allocate independent display frames");
    if (!vic_pixels || !vdc_pixels) {
        free(vic_pixels);
        free(vdc_pixels);
        return 1;
    }
    for (size_t i = 0; i < (size_t)C128_SCREEN_W * C128_SCREEN_H; ++i)
        vic_pixels[i] = 0x00FF0000u;
    for (size_t i = 0; i < (size_t)VDC_SCREEN_W * VDC_SCREEN_H; ++i)
        vdc_pixels[i] = 0x000000FFu;

    VideoCapture capture = {0};
    CHECK(videocap_start(&capture, base, 16, 25, false),
          "start paired VIC/VDC capture");
    snprintf(vic_path, sizeof(vic_path), "%s", videocap_vic_path(&capture));
    snprintf(vdc_path, sizeof(vdc_path), "%s", videocap_vdc_path(&capture));
    CHECK(strstr(vic_path, "-vic.gif") && strstr(vdc_path, "-vdc.gif"),
          "live capture exposes deterministic output names");

    CHECK(videocap_frame(&capture, 20000000, vic_pixels, vdc_pixels) &&
          videocap_vic_frames(&capture) == 1 &&
          videocap_vdc_frames(&capture) == 1,
          "first submission records both displays");
    CHECK(videocap_frame(&capture, 20000000, vic_pixels, vdc_pixels) &&
          videocap_vic_frames(&capture) == 1 &&
          videocap_vdc_frames(&capture) == 1,
          "shared cadence skips both streams together");
    CHECK(videocap_frame(&capture, 20000000, vic_pixels, vdc_pixels) &&
          videocap_vic_frames(&capture) == 2 &&
          videocap_vdc_frames(&capture) == 2,
          "shared cadence advances both streams together");

    int vic_frames = 0, vdc_frames = 0;
    videocap_stop(&capture, &vic_frames, &vdc_frames, NULL);
    CHECK(vic_frames == 2 && vdc_frames == 2 && !videocap_active(&capture),
          "stopping finalizes both GIFs with matching frame counts");
    int width = 0, height = 0;
    CHECK(gif_dimensions(vic_path, &width, &height) &&
          width == 16 && height == 10,
          "VIC GIF uses configured width and VIC aspect ratio");
    CHECK(gif_dimensions(vdc_path, &width, &height) &&
          width == 16 && height == 12,
          "VDC GIF uses configured width and VDC aspect ratio");
    CHECK(file_contains_rgb(vic_path, 0xFF, 0x00, 0x00) &&
          file_contains_rgb(vdc_path, 0x00, 0x00, 0xFF),
          "each GIF contains pixels from its own display");

    CHECK(videocap_start(&capture, base, 16, 25, true),
          "start unified side-by-side capture");
    snprintf(unified_path, sizeof(unified_path), "%s",
             videocap_unified_path(&capture));
    CHECK(videocap_frame(&capture, 20000000, vic_pixels, vdc_pixels) &&
          videocap_unified_frames(&capture) == 1,
          "unified capture combines one VIC and VDC frame");
    int unified_frames = 0;
    videocap_stop(&capture, NULL, NULL, &unified_frames);
    CHECK(unified_frames == 1 &&
          gif_dimensions(unified_path, &width, &height) &&
          width == 32 && height == 12,
          "unified GIF places two configured-width panes side by side");
    CHECK(file_contains_rgb(unified_path, 0xFF, 0x00, 0x00) &&
          file_contains_rgb(unified_path, 0x00, 0x00, 0xFF),
          "unified GIF contains both display framebuffers");

    videocap_destroy(&capture);
    unlink(vic_path);
    unlink(vdc_path);
    unlink(unified_path);
    free(vic_pixels);
    free(vdc_pixels);

    if (failures == 0) { puts("test-videocap: OK"); return 0; }
    printf("test-videocap: %d failure(s)\n", failures);
    return 1;
}
