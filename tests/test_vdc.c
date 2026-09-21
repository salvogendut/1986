#include "vdc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

static void reg_write(Vdc *v, u8 reg, u8 value) {
    vdc_write_index(v, reg);
    vdc_write_data(v, value);
}

int main(void) {
    Vdc *v = calloc(1, sizeof(*v));
    u32 *pixels = calloc(640 * 200, sizeof(*pixels));
    CHECK(v && pixels, "allocate VDC test fixtures");
    if (!v || !pixels) { free(v); free(pixels); return 1; }
    vdc_init(v);
    CHECK(v->fb != NULL, "allocate VDC framebuffer");
    if (!v->fb) { free(v); free(pixels); return 1; }

    memset(v->ram, 0, sizeof(v->ram));
    reg_write(v, 1, 80);
    reg_write(v, 6, 25);
    reg_write(v, 9, 7);
    vdc_write_index(v, 28);
    CHECK((vdc_read_data(v) & 0x10) != 0,
          "C128DCR VDC reports fitted 64 KiB video RAM");
    reg_write(v, 25, 0x80); /* standard bitmap, register colours */
    reg_write(v, 26, 0xF0); /* white on black */
    v->ram[0] = 0x81;
    v->ram[80] = 0x40;
    vdc_render(v, pixels, 640, 200);
    CHECK(pixels[0] == 0xFFFFFF && pixels[1] == 0x000000 &&
          pixels[7] == 0xFFFFFF && pixels[8] == 0x000000,
          "bitmap bytes render as MSB-first pixels, not character codes");
    CHECK(pixels[640] == 0x000000 && pixels[641] == 0xFFFFFF,
          "bitmap address advances to the next byte row every raster");

    reg_write(v, 24, 0x40); /* whole-screen reverse */
    vdc_render(v, pixels, 640, 200);
    CHECK(pixels[0] == 0x000000 && pixels[1] == 0xFFFFFF,
          "reverse screen flips bitmap bits");
    reg_write(v, 24, 0);

    memset(v->ram, 0, sizeof(v->ram));
    reg_write(v, 20, 0x40);
    reg_write(v, 21, 0x00);
    reg_write(v, 25, 0xC0); /* bitmap with attribute colours */
    v->ram[0] = 0x80;
    v->ram[80] = 0x80;
    v->ram[640] = 0x80;
    v->ram[0x4000] = 0xA4;      /* purple foreground, green background */
    v->ram[0x4000 + 80] = 0xF0; /* next character row changes colour */
    vdc_render(v, pixels, 640, 200);
    CHECK(pixels[0] == 0xAA00AA && pixels[1] == 0x00AA00 &&
          pixels[640] == 0xAA00AA,
          "bitmap attributes use high nibble foreground and low nibble background");
    CHECK(pixels[8 * 640] == 0xFFFFFF,
          "bitmap attributes advance once per character row");

    memset(v->ram, 0, sizeof(v->ram));
    reg_write(v, 25, 0x80);
    reg_write(v, 27, 2); /* R27 skips two bytes after each raster */
    v->ram[82] = 0x80;
    vdc_render(v, pixels, 640, 200);
    CHECK(pixels[0] == 0x000000 && pixels[640] == 0xFFFFFF,
          "bitmap row stride includes R27 address increment");

    memset(v->ram, 0, sizeof(v->ram));
    reg_write(v, 27, 0);
    reg_write(v, 1, 40);
    reg_write(v, 25, 0x90); /* bitmap, double-width pixels */
    v->ram[0] = 0x80;
    vdc_render(v, pixels, 640, 200);
    CHECK(pixels[0] == 0xFFFFFF && pixels[1] == 0xFFFFFF &&
          pixels[2] == 0x000000,
          "double-pixel bitmap mode repeats each dot horizontally");

    memset(v->ram, 0, sizeof(v->ram));
    reg_write(v, 1, 80);
    reg_write(v, 25, 0); /* return to text mode */
    reg_write(v, 28, 0x20);
    v->ram[0] = 1;
    v->ram[0x2010] = 0x80;
    vdc_render(v, pixels, 640, 200);
    CHECK(pixels[0] == 0xFFFFFF && pixels[1] == 0x000000,
          "text rendering still uses glyph RAM after leaving bitmap mode");

    /* Block-copy source pointer and data latch advance along with target. */
    reg_write(v, 24, 0x80);
    reg_write(v, 18, 0x30);
    reg_write(v, 19, 0x00);
    reg_write(v, 32, 0x31);
    reg_write(v, 33, 0x00);
    v->ram[0x3100] = 0xA5;
    v->ram[0x3101] = 0x5A;
    reg_write(v, 30, 2);
    CHECK(v->ram[0x3000] == 0xA5 && v->ram[0x3001] == 0x5A &&
          v->regs[18] == 0x30 && v->regs[19] == 2 &&
          v->regs[32] == 0x31 && v->regs[33] == 2 && v->regs[31] == 0x5A,
          "block copy advances source, target, and data latch");

    free(v->fb);
    free(v);
    free(pixels);
    if (failures == 0) { puts("test-vdc: OK"); return 0; }
    printf("test-vdc: %d failure(s)\n", failures);
    return 1;
}
