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

static u8 reg_read(Vdc *v, u8 reg) {
    vdc_write_index(v, reg);
    return vdc_read_data(v);
}

static void ram_write(Vdc *v, u16 address, u8 value) {
    reg_write(v, 18, (u8)(address >> 8));
    reg_write(v, 19, (u8)address);
    reg_write(v, 31, value);
}

static u8 ram_read(Vdc *v, u16 address) {
    reg_write(v, 18, (u8)(address >> 8));
    reg_write(v, 19, (u8)address);
    return reg_read(v, 31);
}

int main(void) {
    Vdc *v = calloc(1, sizeof(*v));
    u32 *pixels = calloc(640 * 400, sizeof(*pixels));
    CHECK(v && pixels, "allocate VDC test fixtures");
    if (!v || !pixels) { free(v); free(pixels); return 1; }
    vdc_init(v);
    CHECK(v->fb != NULL, "allocate VDC framebuffer");
    if (!v->fb) { free(v); free(pixels); return 1; }

    memset(v->ram, 0, sizeof(v->ram));
    reg_write(v, 1, 80);
    reg_write(v, 6, 25);
    reg_write(v, 9, 7);
    reg_write(v, 23, 7);
    vdc_write_index(v, 28);
    CHECK((vdc_read_data(v) & 0x1F) == 0x0F,
          "R28 reads the current addressing mode, not fitted RAM size");
    reg_write(v, 28, 0x10); /* linear 64K addressing on the 64K DCR */
    CHECK((reg_read(v, 28) & 0x1F) == 0x1F,
          "R28 bit 4 selects 64K addressing");
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

    memset(v->ram, 0, sizeof(v->ram));
    reg_write(v, 6, 0xFE);
    reg_write(v, 9, 0); /* one raster per row, beyond the text-mode 50-row cap */
    v->ram[253 * 80] = 0x80;
    vdc_render(v, pixels, 640, 254);
    CHECK(pixels[253 * 640] == 0xFFFFFF && pixels[252 * 640] == 0x000000,
          "bitmap mode renders R6 rows beyond the text-mode row limit");
    reg_write(v, 6, 25);
    reg_write(v, 9, 7);
    memset(v->ram, 0, sizeof(v->ram));
    v->ram[0] = 0x81;
    v->ram[80] = 0x40;

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
    reg_write(v, 28, 0x30);
    v->ram[0] = 1;
    v->ram[0x2010] = 0x80;
    vdc_render(v, pixels, 640, 200);
    CHECK(pixels[0] == 0xFFFFFF && pixels[1] == 0x000000,
          "text rendering still uses glyph RAM after leaving bitmap mode");

    reg_write(v, 20, 0x10); /* attribute table at $1000 */
    reg_write(v, 21, 0x00);
    reg_write(v, 25, 0x40); /* attribute mode */
    v->ram[0x1000] = 0x0f;
    v->ram[0x3010] = 0x40; /* distinct glyph in alternate charset */
    vdc_render(v, pixels, 640, 200);
    CHECK(pixels[0] == 0xFFFFFF && pixels[1] == 0x000000,
          "normal VDC attribute keeps the upper/graphics glyph");
    v->ram[0x1000] = VDC_ATTR_ALTCHARSET | 0x0f;
    vdc_render(v, pixels, 640, 200);
    CHECK(pixels[0] == 0x000000 && pixels[1] == 0xFFFFFF,
          "alternate VDC attribute selects the upper/lowercase glyph");

    /* Text row stride includes R27, just as bitmap raster stride does. */
    memset(v->ram, 0, sizeof(v->ram));
    reg_write(v, 25, 0);
    reg_write(v, 27, 2);
    reg_write(v, 26, 0xF0);
    v->ram[82] = 1;
    v->ram[0x2010] = 0x80;
    vdc_render(v, pixels, 640, 200);
    CHECK(pixels[0] == 0x000000 && pixels[8 * 640] == 0xFFFFFF,
          "text row stride includes R27 address increment");
    reg_write(v, 27, 0);

    /* Flash, underline, semigraphics, and reverse are glyph-raster effects. */
    memset(v->ram, 0, sizeof(v->ram));
    v->ram[0] = 1;
    v->ram[0x2010] = 0x80;
    reg_write(v, 20, 0x10);
    reg_write(v, 21, 0);
    reg_write(v, 25, 0x40);
    v->ram[0x1000] = VDC_ATTR_FLASH | 0x0F;
    v->frame_counter = 7;
    vdc_render(v, pixels, 640, 200);
    CHECK(pixels[0] == 0x000000, "flash attribute blanks its glyph in blink phase");
    v->ram[0x1000] = VDC_ATTR_UNDERLINE | 0x0F;
    reg_write(v, 29, 3);
    vdc_render(v, pixels, 640, 200);
    CHECK(pixels[3 * 640] == 0xFFFFFF, "underline uses the R29 raster");
    v->ram[0x1000] = VDC_ATTR_REVERSE | 0x0F;
    reg_write(v, 24, 0x40);
    vdc_render(v, pixels, 640, 200);
    CHECK(pixels[0] == 0xFFFFFF && pixels[1] == 0x000000,
          "screen and character reverse cancel each other");
    reg_write(v, 24, 0);
    reg_write(v, 25, 0x20);
    reg_write(v, 22, 0x70);
    vdc_render(v, pixels, 640, 200);
    CHECK(pixels[1] == 0xFFFFFF,
          "semigraphics extends the rightmost set glyph pixel");
    reg_write(v, 25, 0);
    reg_write(v, 22, 0x78);

    /* R9 and R23 control raster height; R6 can display more than 25 rows. */
    memset(v->ram, 0, sizeof(v->ram));
    reg_write(v, 9, 9);
    reg_write(v, 23, 7);
    reg_write(v, 6, 25);
    v->ram[0] = 1;
    v->ram[0x2010 + 8] = 0xFF;
    vdc_render(v, pixels, 640, 250);
    CHECK(pixels[8 * 640] == 0x000000,
          "R23 blanks glyph rasters past the vertical pixel size");
    reg_write(v, 9, 7);
    reg_write(v, 6, 50);
    v->ram[49 * 80] = 1;
    v->ram[0x2010] = 0x80;
    vdc_render(v, pixels, 640, 400);
    CHECK(pixels[392 * 640] == 0xFFFFFF,
          "R6 can render a 50-row text layout");
    reg_write(v, 6, 25);

    reg_write(v, 4, 39);
    reg_write(v, 7, 32);
    reg_write(v, 3, 0);
    reg_write(v, 8, 3);
    vdc_set_raster_line(v, 0);
    CHECK((vdc_read_status(v) & 0x83) == 0x82 && (vdc_read_status(v) & 0x20),
          "8568 revision is stable and top border sets VBLANK");
    vdc_set_raster_line(v, 100);
    CHECK(!(vdc_read_status(v) & 0x20), "active display clears VBLANK");
    vdc_set_raster_line(v, 300);
    CHECK(vdc_read_status(v) & 0x20, "bottom border sets VBLANK");
    vdc_set_raster_line(v, 100);
    vdc_set_bus_clock(v, 1000, false);
    reg_write(v, 18, 0);
    CHECK(!(vdc_read_status(v) & 0x80),
          "VDC is temporarily busy after an update-address write");
    vdc_set_bus_clock(v, 1044, false);
    CHECK(vdc_read_status(v) & 0x80,
          "VDC becomes ready after the active-display transfer interval");
    Vdc *timing = calloc(1, sizeof(*timing));
    CHECK(timing != NULL, "allocate independent VDC timing fixture");
    if (timing) {
        vdc_init(timing);
        reg_write(timing, 4, 0xFF);
        reg_write(timing, 6, 0xFE);
        for (int frame = 0; frame < 6; ++frame) {
            vdc_set_raster_line(timing, 311);
            vdc_set_raster_line(timing, 0);
        }
        vdc_set_raster_line(timing, 100);
        CHECK(!(vdc_read_status(timing) & 0x20),
              "long VDC frame stays active across PAL frame boundaries");
        vdc_set_raster_line(timing, 168);
        CHECK(vdc_read_status(timing) & 0x20,
              "long VDC frame reaches its own bottom border");
        vdc_set_raster_line(timing, 184);
        CHECK(!(vdc_read_status(timing) & 0x20),
              "long VDC frame restarts its active display");
        reg_write(timing, 9, 0xE0); /* One raster per row: 256-line VDC frame. */
        bool short_blank_seen = false;
        bool short_active_again = false;
        unsigned host_line = 184;
        for (int i = 0; i < 400; ++i) {
            host_line = (host_line + 1u) % 312u;
            vdc_set_raster_line(timing, host_line);
            if (vdc_read_status(timing) & 0x20) short_blank_seen = true;
            else if (short_blank_seen) { short_active_again = true; break; }
        }
        CHECK(short_blank_seen,
              "short R4=$ff VDC frame has its own VBLANK pulse");
        CHECK(short_active_again,
              "short R4=$ff VDC frame resumes active display");
        vdc_reset(timing);
        reg_write(timing, 4, 0xFF);
        reg_write(timing, 6, 0xFE);
        vdc_set_raster_line(timing, 5);
        reg_write(timing, 9, 0xE0);
        vdc_set_raster_line(timing, 32);
        CHECK(vdc_read_status(timing) & 0x20,
              "mid-row R9 change waits for 5-bit raster counter equality");
        vdc_set_raster_line(timing, 33);
        CHECK(!(vdc_read_status(timing) & 0x20),
              "latched R9 match advances to the next visible row");
        free(timing->fb);
        free(timing);
    }
    reg_write(v, 16, 0xAA);
    CHECK(reg_read(v, 16) == 0, "light-pen registers are read-only");

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

    /* Software can probe physical VRAM via R28 and through the indexed data
     * port. $4000 must be independent with 64K and alias $0000 with 16K. */
    vdc_reset(v);
    CHECK(v->address_mask == 0xFFFF && !(reg_read(v, 28) & 0x10),
          "reset preserves fitted RAM but resets R28 addressing mode");
    CHECK(v->ram[0x2010] == 0x80,
          "VDC chip reset preserves RAM rather than repeating power-up fill");
    reg_write(v, 28, 0x10);
    ram_write(v, 0x0000, 0x55);
    ram_write(v, 0x4000, 0xAA);
    CHECK(ram_read(v, 0x0000) == 0x55 && ram_read(v, 0x4000) == 0xAA,
          "64K VDC keeps $0000 and $4000 independent through data port");
    reg_write(v, 28, 0);
    ram_write(v, 0x2000, 0x6C);
    CHECK(v->ram[0x4000] == 0x6C,
          "16K addressing rearranges address lines on 64K-fitted VDC");

    vdc_set_ram_size_kb(v, 16);
    vdc_reset(v);
    CHECK(v->address_mask == 0x3FFF && !(reg_read(v, 28) & 0x10),
          "16K fitted VDC and R28 addressing mode survive reset");
    ram_write(v, 0x0000, 0x55);
    ram_write(v, 0x4000, 0xAA);
    CHECK(ram_read(v, 0x0000) == 0xAA && ram_read(v, 0x4000) == 0xAA,
          "16K VDC mirrors $4000 onto $0000 through data port");
    reg_write(v, 18, 0x40);
    reg_write(v, 19, 0x00);
    reg_write(v, 30, 2); /* fill at $4000-$4001, mirrored to $0000-$0001 */
    CHECK(v->ram[0] == 0xAA && v->ram[1] == 0xAA,
          "16K VDC block fill follows the physical RAM mirror");
    v->ram[0x3FFF] = 0x6C;
    reg_write(v, 18, 0x3F);
    reg_write(v, 19, 0xFF);
    CHECK(reg_read(v, 31) == 0x6C && v->regs[18] == 0x40 && v->regs[19] == 0,
          "16K VDC data reads alias RAM but retain the 16-bit update pointer");

    reg_write(v, 28, 0x10);
    ram_write(v, 0x4000, 0xBC);
    CHECK(v->ram[0x2000] == 0xBC,
          "64K addressing on 16K-fitted VDC rearranges address lines");

    reg_write(v, 12, 0x40); /* display start $4000 aliases $0000 */
    reg_write(v, 13, 0);
    reg_write(v, 28, 0x20);
    reg_write(v, 23, 7);
    reg_write(v, 26, 0xF0);
    v->ram[0] = 1;
    v->ram[0x2010] = 0x80;
    vdc_render(v, pixels, 640, 200);
    CHECK(pixels[0] == 0xFFFFFF && pixels[1] == 0x000000,
          "16K VDC text rendering follows the physical RAM mirror");

    free(v->fb);
    free(v);
    free(pixels);
    if (failures == 0) { puts("test-vdc: OK"); return 0; }
    printf("test-vdc: %d failure(s)\n", failures);
    return 1;
}
