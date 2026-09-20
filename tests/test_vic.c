#include "vic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

/* vic.c uses the CPU cycle count only for raster-register reads. Rendering
 * tests do not advance the raster, so a fixed value is sufficient here. */
u64 cpu_cycles(void) { return 0; }

static u32 pixel(const Display *d, int x, int y) {
    return d->pixels[(VIC_TEXT_Y + y) * C128_SCREEN_W + VIC_TEXT_X + x];
}

int main(void) {
    Vic vic;
    Mem *mem = calloc(1, sizeof(*mem));
    Display *display = calloc(1, sizeof(*display));
    CHECK(mem != NULL && display != NULL, "allocate renderer fixtures");
    if (!mem || !display) {
        free(mem);
        free(display);
        return 1;
    }

    vic_init(&vic);
    vic_write(&vic, 0xD011, 0x3B); /* display on, bitmap mode */
    vic_write(&vic, 0xD018, 0x18); /* screen $0400, bitmap $2000 */
    vic.bg_color[0] = 0x02;        /* must not replace hires cell background */

    /* Hires: screen high nibble colours set bits, low nibble clear bits. */
    mem->ram[0x0400] = 0xA3;
    mem->ram[0x2000] = 0x80;
    vic_write(&vic, 0xD016, 0x08);
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0xC46C71, "hires set bit uses screen high nibble");
    CHECK(pixel(display, 1, 0) == 0x75CEC8, "hires clear bit uses screen low nibble");

    /* Multicolor: 00=$D021, 01=screen high, 10=screen low, 11=colour RAM. */
    mem->ram[0x0400] = 0xB4;
    mem->ram[0x2000] = 0x1B;       /* 00, 01, 10, 11 */
    mem->color_ram[0] = 0x0E;
    vic_write(&vic, 0xD016, 0x18);
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0x813338 && pixel(display, 1, 0) == 0x813338,
          "multicolor 00 uses background register");
    CHECK(pixel(display, 2, 0) == 0x4A4A4A && pixel(display, 3, 0) == 0x4A4A4A,
          "multicolor 01 uses screen high nibble");
    CHECK(pixel(display, 4, 0) == 0x8E3C97 && pixel(display, 5, 0) == 0x8E3C97,
          "multicolor 10 uses screen low nibble");
    CHECK(pixel(display, 6, 0) == 0x706DEB && pixel(display, 7, 0) == 0x706DEB,
          "multicolor 11 uses colour RAM");

    free(display);
    free(mem);
    if (failures == 0) { printf("test-vic: OK\n"); return 0; }
    printf("test-vic: %d failure(s)\n", failures);
    return 1;
}
