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

static void clear_video_memory(Mem *mem) {
    memset(mem->ram, 0, sizeof(mem->ram));
    memset(mem->chargen, 0, sizeof(mem->chargen));
    memset(mem->color_ram, 0, sizeof(mem->color_ram));
}

static void set_sprite_pointer(Mem *mem, const Vic *vic, int sprite, u8 pointer) {
    u32 screen_base = vic->bank_addr + ((vic->ctrl2 & 0xF0) << 6);
    mem->ram[screen_base + 0x3F8 + (unsigned)sprite] = pointer;
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

    /* Native C128 text mode uses the upper 4K half of the 8K character ROM. */
    vic_reset(&vic);
    mem->ram[0x0400] = 0x01;
    mem->color_ram[0] = 0x01;
    mem->chargen[0x0008] = 0x00;
    mem->chargen[0x1008] = 0x80;
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0xFFFFFF,
          "native C128 text uses upper character-ROM bank");
    CHECK(pixel(display, 1, 0) == 0x000000,
          "character glyph preserves clear pixels");

    /* Sprite registers retain values and collision registers clear on read. */
    vic_write(&vic, 0xD000, 0x34);
    vic_write(&vic, 0xD001, 0x56);
    vic_write(&vic, 0xD010, 0x01);
    vic_write(&vic, 0xD015, 0xA5);
    vic_write(&vic, 0xD017, 0x12);
    vic_write(&vic, 0xD01B, 0x23);
    vic_write(&vic, 0xD01C, 0x34);
    vic_write(&vic, 0xD01D, 0x45);
    vic_write(&vic, 0xD025, 0x16);
    vic_write(&vic, 0xD026, 0x27);
    vic_write(&vic, 0xD027, 0x1E);
    CHECK(vic_read(&vic, 0xD000) == 0x34 && vic_read(&vic, 0xD001) == 0x56,
          "sprite coordinates read back");
    CHECK(vic_read(&vic, 0xD010) == 0x01 && vic_read(&vic, 0xD015) == 0xA5,
          "sprite X MSB and enable mask read back");
    CHECK(vic_read(&vic, 0xD017) == 0x12 && vic_read(&vic, 0xD01B) == 0x23 &&
          vic_read(&vic, 0xD01C) == 0x34 && vic_read(&vic, 0xD01D) == 0x45,
          "sprite expansion, priority, and multicolor registers read back");
    CHECK(vic_read(&vic, 0xD025) == 0x06 && vic_read(&vic, 0xD026) == 0x07 &&
          vic_read(&vic, 0xD027) == 0x0E,
          "sprite color registers retain their low nibble");

    /* Each of the eight standard sprites fetches through its screen pointer
     * and maps VIC coordinate (24,51) to the top-left graphics pixel. */
    static const u32 colors[8] = {
        0xFFFFFF, 0x813338, 0x75CEC8, 0x8E3C97,
        0x56AC4D, 0x2E2C9B, 0xEDF171, 0x8E5029
    };
    vic_reset(&vic);
    clear_video_memory(mem);
    vic_write(&vic, 0xD018, 0x14); /* screen matrix $0400 */
    for (int sprite = 0; sprite < VIC_SPRITES; sprite++) {
        u8 pointer = (u8)(0x20 + sprite);
        set_sprite_pointer(mem, &vic, sprite, pointer);
        mem->ram[(u32)pointer << 6] = 0x80;
        vic_write(&vic, (u16)(0xD000 + sprite * 2), (u8)(24 + sprite * 32));
        vic_write(&vic, (u16)(0xD001 + sprite * 2), 51);
        vic_write(&vic, (u16)(0xD027 + sprite), (u8)(sprite + 1));
    }
    vic_write(&vic, 0xD015, 0xFF);
    vic_render(&vic, mem, display);
    for (int sprite = 0; sprite < VIC_SPRITES; sprite++) {
        CHECK(pixel(display, sprite * 32, 0) == colors[sprite],
              "all eight standard sprite slots render");
        CHECK(pixel(display, sprite * 32 + 1, 0) == 0x000000,
              "transparent sprite pixels preserve the background");
    }

    /* Multicolor pixels use shared 0, individual, and shared 1 colours;
     * X/Y expansion duplicates their output in both axes. */
    vic_reset(&vic);
    clear_video_memory(mem);
    vic_write(&vic, 0xD018, 0x14);
    set_sprite_pointer(mem, &vic, 0, 0x20);
    mem->ram[0x0800] = 0x6C; /* multicolor codes 01, 10, 11, 00 */
    vic_write(&vic, 0xD000, 24);
    vic_write(&vic, 0xD001, 51);
    vic_write(&vic, 0xD015, 0x01);
    vic_write(&vic, 0xD017, 0x01);
    vic_write(&vic, 0xD01C, 0x01);
    vic_write(&vic, 0xD01D, 0x01);
    vic_write(&vic, 0xD025, 0x02);
    vic_write(&vic, 0xD026, 0x06);
    vic_write(&vic, 0xD027, 0x05);
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0x813338 && pixel(display, 3, 1) == 0x813338,
          "expanded sprite multicolor 01 uses shared color 0");
    CHECK(pixel(display, 4, 0) == 0x56AC4D && pixel(display, 7, 1) == 0x56AC4D,
          "expanded sprite multicolor 10 uses individual color");
    CHECK(pixel(display, 8, 0) == 0x2E2C9B && pixel(display, 11, 1) == 0x2E2C9B,
          "expanded sprite multicolor 11 uses shared color 1");
    CHECK(pixel(display, 12, 0) == 0x000000,
          "multicolor sprite code 00 is transparent");

    /* Behind-background priority preserves set graphics pixels while still
     * latching the sprite/background collision and its IRQ source. */
    vic_reset(&vic);
    clear_video_memory(mem);
    vic_write(&vic, 0xD018, 0x14);
    mem->ram[0x0400] = 0x01;
    mem->chargen[0x1008] = 0x80;
    mem->color_ram[0] = 0x01;
    set_sprite_pointer(mem, &vic, 0, 0x20);
    mem->ram[0x0800] = 0x80;
    vic_write(&vic, 0xD000, 24);
    vic_write(&vic, 0xD001, 51);
    vic_write(&vic, 0xD015, 0x01);
    vic_write(&vic, 0xD01B, 0x01);
    vic_write(&vic, 0xD01A, 0x82);
    vic_write(&vic, 0xD027, 0x02);
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0xFFFFFF,
          "background-priority sprite stays behind foreground graphics");
    CHECK((vic_read(&vic, 0xD019) & 0x82) == 0x82,
          "sprite/background collision raises its enabled IRQ source");
    CHECK(vic_read(&vic, 0xD01F) == 0x01 && vic_read(&vic, 0xD01F) == 0x00,
          "sprite/background collision latch clears on read");
    CHECK((vic_read(&vic, 0xD019) & 0x82) == 0,
          "reading sprite/background collision clears its IRQ source");

    /* Overlapping opaque pixels latch both sprite numbers; sprite zero wins
     * visually over sprite one. */
    vic_reset(&vic);
    clear_video_memory(mem);
    vic_write(&vic, 0xD018, 0x14);
    set_sprite_pointer(mem, &vic, 0, 0x20);
    set_sprite_pointer(mem, &vic, 1, 0x21);
    mem->ram[0x0800] = 0x80;
    mem->ram[0x0840] = 0x80;
    vic_write(&vic, 0xD000, 24);
    vic_write(&vic, 0xD001, 51);
    vic_write(&vic, 0xD002, 24);
    vic_write(&vic, 0xD003, 51);
    vic_write(&vic, 0xD015, 0x03);
    vic_write(&vic, 0xD01A, 0x84);
    vic_write(&vic, 0xD027, 0x02);
    vic_write(&vic, 0xD028, 0x05);
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0x813338,
          "lower-numbered sprite has visual priority");
    CHECK((vic_read(&vic, 0xD019) & 0x84) == 0x84,
          "sprite/sprite collision raises its enabled IRQ source");
    CHECK(vic_read(&vic, 0xD01E) == 0x03 && vic_read(&vic, 0xD01E) == 0x00,
          "sprite/sprite collision latch reports both sprites and clears");
    CHECK((vic_read(&vic, 0xD019) & 0x84) == 0,
          "reading sprite/sprite collision clears its IRQ source");

    /* CIA-selected VIC banks apply to both pointer and sprite-data fetches. */
    vic_reset(&vic);
    clear_video_memory(mem);
    vic_set_bank(&vic, 1);
    vic_write(&vic, 0xD018, 0x14);
    set_sprite_pointer(mem, &vic, 0, 0x20);
    mem->ram[0x4800] = 0x80;
    vic_write(&vic, 0xD000, 24);
    vic_write(&vic, 0xD001, 51);
    vic_write(&vic, 0xD015, 0x01);
    vic_write(&vic, 0xD027, 0x07);
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0xEDF171,
          "sprite pointer and data use the selected 16K VIC bank");

    vic_reset(&vic);
    clear_video_memory(mem);
    vic_set_bank(&vic, 4); /* first 16K window in the second 64K RAM bank */
    vic_write(&vic, 0xD018, 0x14);
    set_sprite_pointer(mem, &vic, 0, 0x20);
    mem->ram[0x10800] = 0x80;
    vic_write(&vic, 0xD000, 24);
    vic_write(&vic, 0xD001, 51);
    vic_write(&vic, 0xD015, 0x01);
    vic_write(&vic, 0xD027, 0x03);
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0x75CEC8,
          "$D506 can place sprite pointer and data in the second 64K bank");

    free(display);
    free(mem);
    if (failures == 0) { printf("test-vic: OK\n"); return 0; }
    printf("test-vic: %d failure(s)\n", failures);
    return 1;
}
