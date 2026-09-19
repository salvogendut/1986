#include "gifcap.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

int main(void) {
    const char *path = "/tmp/1986-test.gif";
    GifCap *g = gifcap_open(path, 16, 16, 16, 16, 4);
    CHECK(g != NULL, "gifcap_open");

    static uint32_t px[16 * 16];
    for (int i = 0; i < 16 * 16; i++) px[i] = 0x00FF0000; /* red */
    CHECK(gifcap_frame(g, px), "gifcap_frame");
    CHECK(gifcap_frame_count(g) == 1, "frame count");
    gifcap_close(g);

    /* Verify it is a GIF. */
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL, "file exists");
    if (f) {
        unsigned char sig[6];
        fread(sig, 1, 6, f);
        fclose(f);
        CHECK(memcmp(sig, "GIF89a", 6) == 0, "GIF signature");
    }
    remove(path);

    if (failures == 0) { printf("test-gifcap: OK\n"); return 0; }
    printf("test-gifcap: %d failure(s)\n", failures);
    return 1;
}
