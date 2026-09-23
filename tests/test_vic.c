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
static u64 test_cpu_cycles;
u64 cpu_cycles(void) { return test_cpu_cycles; }

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

    mem_init(mem);
    mem_set_processor_port(mem, 0x07, 0x00); /* VIC colour banks 0, ROM on */

    vic_init(&vic);
    CHECK(!vic.fast_mode && (vic_read(&vic, 0xD030) & 1) == 0,
          "VIC-IIe fast mode defaults off");
    vic_write(&vic, 0xD030, 0x01);
    CHECK(vic.fast_mode && (vic_read(&vic, 0xD030) & 1),
          "$D030 bit 0 selects VIC-IIe 2 MHz mode");
    vic_reset(&vic);
    vic_write(&vic, 0xD01A, 0x01);
    vic.irq_status = 0x81;
    vic_write(&vic, 0xD019, 0x40); /* final value of LSR $D019 */
    CHECK(vic.irq_status == 0x81,
          "ordinary write only acknowledges IRQ bits in its value");
    vic_write_rmw(&vic, 0xD019, 0x40);
    CHECK(vic.irq_status == 0,
          "LSR $D019 acknowledges the read byte through its RMW bus write");
    vic_reset(&vic);
    vic_write(&vic, 0xD01A, 0x01);
    vic_write(&vic, 0xD012, 100);
    test_cpu_cycles = 100 * 63;
    CHECK(vic_tick(&vic) && (vic_read(&vic, 0xD019) & 0x01),
          "first raster compare asserts its IRQ");
    vic_write(&vic, 0xD019, 0x01);
    vic_write(&vic, 0xD012, 120);
    test_cpu_cycles = 120 * 63;
    CHECK(vic_tick(&vic) && (vic_read(&vic, 0xD019) & 0x01),
          "a new compare can assert another raster IRQ in the same frame");
    vic_write(&vic, 0xD019, 0x01);
    CHECK(!vic_tick(&vic), "one compare does not repeatedly fire on one raster");
    vic_write(&vic, 0xD011, 0x80);
    vic_write(&vic, 0xD012, 44); /* 256 + 44 = raster 300 */
    CHECK(vic.raster_irq_line == 300,
          "D011 bit 7 supplies the ninth raster compare bit");
    test_cpu_cycles = 300 * 63;
    CHECK(vic_tick(&vic), "raster compare can match above line 255");
    test_cpu_cycles = 100 * 63;
    CHECK(!(vic_read(&vic, 0xD011) & 0x80),
          "D011 read bit 7 reports the current raster, not the compare latch");
    test_cpu_cycles = 0;
    vic_reset(&vic);
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

    /* A raster split can point adjacent bitmap scanlines at different bitmap
     * banks. The screen matrix remains buffered from the badline fetch. */
    vic_reset(&vic);
    vic_write(&vic, 0xD011, 0x3B);
    vic_write(&vic, 0xD018, 0x10); /* first scanline: bitmap $0000 */
    mem->ram[0x0400] = 0xA3;
    mem->ram[0x0000] = 0x80;
    vic_latch_raster(&vic, mem, 51);
    vic_write(&vic, 0xD018, 0x18); /* second scanline: bitmap $2000 */
    mem->ram[0x2001] = 0x00;
    vic_latch_raster(&vic, mem, 52);
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0xC46C71 &&
          pixel(display, 0, 1) == 0x75CEC8,
          "bitmap raster split uses the bitmap bank selected per scanline");

    vic_reset(&vic);
    vic_write(&vic, 0xD011, 0x3B);
    vic_write(&vic, 0xD018, 0x18);
    vic.bg_color[0] = 0x02;

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

    /* Multicolor text is selected per character by color RAM bit 3. Each
     * two-bit glyph pair selects background 0/1/2 or the low three bits of
     * the character color. COMMANDO relies heavily on this VIC-II mode. */
    vic_reset(&vic);
    clear_video_memory(mem);
    mem_set_processor_port(mem, 0x07, 0x00);
    vic_write(&vic, 0xD011, 0x1B);
    vic_write(&vic, 0xD016, 0x18);
    vic_write(&vic, 0xD018, 0x14);
    vic_write(&vic, 0xD021, 0x02);
    vic_write(&vic, 0xD022, 0x03);
    vic_write(&vic, 0xD023, 0x04);
    mem->ram[0x0400] = 0x01;
    mem->color_ram[0] = 0x0D;
    mem->chargen[0x1008] = 0x1B; /* 00, 01, 10, 11 */
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0x813338 &&
          pixel(display, 1, 0) == 0x813338,
          "multicolor text 00 uses background 0");
    CHECK(pixel(display, 2, 0) == 0x75CEC8 &&
          pixel(display, 3, 0) == 0x75CEC8,
          "multicolor text 01 uses background 1");
    CHECK(pixel(display, 4, 0) == 0x8E3C97 &&
          pixel(display, 5, 0) == 0x8E3C97,
          "multicolor text 10 uses background 2");
    CHECK(pixel(display, 6, 0) == 0x56AC4D &&
          pixel(display, 7, 0) == 0x56AC4D,
          "multicolor text 11 uses the character color");

    /* Extended-color text uses the character's upper two bits to select one
     * of four backgrounds and its lower six bits for the glyph. */
    vic_reset(&vic);
    clear_video_memory(mem);
    mem_set_processor_port(mem, 0x07, 0x00);
    vic_write(&vic, 0xD011, 0x5B);
    vic_write(&vic, 0xD016, 0x08);
    vic_write(&vic, 0xD018, 0x14);
    vic_write(&vic, 0xD024, 0x07);
    mem->ram[0x0400] = 0xC1;
    mem->color_ram[0] = 0x01;
    mem->chargen[0x1008] = 0x80;
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0xFFFFFF,
          "extended-color text retains the foreground color");
    CHECK(pixel(display, 1, 0) == 0xEDF171,
          "extended-color text selects its background from the character");

    /* Video mode, memory pointers, and colors are sampled per raster line.
     * A final-state renderer would render both rows in only one of these
     * modes and colors. */
    vic_reset(&vic);
    clear_video_memory(mem);
    mem_set_processor_port(mem, 0x07, 0x00);
    vic_write(&vic, 0xD011, 0x3B); /* bitmap */
    vic_write(&vic, 0xD016, 0x08);
    vic_write(&vic, 0xD018, 0x18); /* matrix $0400, bitmap $2000 */
    mem->ram[0x0400] = 0xA1;
    mem->ram[0x2000] = 0x80;
    vic_latch_raster(&vic, mem, 51);
    vic_write(&vic, 0xD011, 0x1B); /* text */
    vic_write(&vic, 0xD018, 0x14); /* same matrix, characters $1000 */
    mem->color_ram[0] = 0x01;
    mem->chargen[0x1509] = 0x40;
    vic_latch_raster(&vic, mem, 52);
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0xC46C71,
          "a raster line retains bitmap mode and its memory pointers");
    CHECK(pixel(display, 1, 1) == 0xFFFFFF,
          "the following raster line can switch to text mode");

    vic_reset(&vic);
    clear_video_memory(mem);
    vic_write(&vic, 0xD020, 0x02);
    vic_latch_raster(&vic, mem, 16);
    vic_write(&vic, 0xD020, 0x05);
    vic_latch_raster(&vic, mem, 17);
    vic_write(&vic, 0xD021, 0x03);
    vic_latch_raster(&vic, mem, 51);
    vic_write(&vic, 0xD021, 0x04);
    vic_latch_raster(&vic, mem, 52);
    vic_render(&vic, mem, display);
    CHECK(display->pixels[0] == 0x813338 &&
          display->pixels[C128_SCREEN_W] == 0x56AC4D,
          "border color changes are preserved per raster line");
    CHECK(pixel(display, 0, 0) == 0x75CEC8 &&
          pixel(display, 0, 1) == 0x8E3C97,
          "background color changes are preserved per raster line");

    /* $D011 YSCROLL chooses the first badline. Advancing it by one moves
     * character row zero down by one physical scanline instead of jumping a
     * complete character row. */
    vic_reset(&vic);
    clear_video_memory(mem);
    mem_set_processor_port(mem, 0x07, 0x00);
    vic_write(&vic, 0xD018, 0x14);
    vic_write(&vic, 0xD020, 0x02);
    mem->ram[0x0400] = 0x01;
    mem->color_ram[0] = 0x01;
    mem->chargen[0x1008] = 0x80;
    vic_write(&vic, 0xD011, 0x1B); /* 25 rows, YSCROLL 3 */
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0xFFFFFF,
          "YSCROLL 3 starts glyph row zero on raster 51");
    vic_write(&vic, 0xD011, 0x1C); /* YSCROLL 4 */
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0x000000 &&
          pixel(display, 0, 1) == 0xFFFFFF,
          "incrementing YSCROLL moves graphics down one scanline");

    /* RSEL's 24-row window starts four lines lower. COMMANDO uses this with
     * YSCROLL 7 for its playfield and lower status/text split. */
    vic_write(&vic, 0xD011, 0x17); /* 24 rows, YSCROLL 7 */
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0x813338,
          "24-row mode keeps raster 51 in the border");
    CHECK(pixel(display, 0, 4) == 0xFFFFFF,
          "24-row mode starts glyph row zero on raster 55");

    /* A YSCROLL write after the upper playfield has started changes future
     * badline comparisons, not the row-counter phase already in progress. */
    vic_reset(&vic);
    clear_video_memory(mem);
    mem_set_processor_port(mem, 0x07, 0x00);
    vic_write(&vic, 0xD016, 0x08);
    vic_write(&vic, 0xD018, 0x14);
    memset(&mem->ram[0x0400], 0x01, 1000);
    memset(&mem->color_ram[0], 0x01, 1000);
    mem->chargen[0x1009] = 0x40;
    mem->chargen[0x100A] = 0x20;
    vic_write(&vic, 0xD011, 0x1B); /* establish YSCROLL 3 badlines */
    for (unsigned line = 0; line <= 100; line++)
        vic_latch_raster(&vic, mem, line);
    vic_write(&vic, 0xD011, 0x1C); /* change after raster row 6 began */
    for (unsigned line = 101; line < VIC_RASTER_LINES; line++)
        vic_latch_raster(&vic, mem, line);
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 2, 50) == 0xFFFFFF &&
          pixel(display, 1, 50) == 0x000000,
          "mid-frame YSCROLL write preserves the active row-counter phase");

    /* Sprite registers and pointer-table RAM are sampled per raster. Raster
     * multiplexers rewrite both while earlier sprites are still visible. */
    vic_reset(&vic);
    clear_video_memory(mem);
    vic_write(&vic, 0xD018, 0x14);
    set_sprite_pointer(mem, &vic, 0, 0x20);
    mem->ram[0x0800] = 0x80;
    vic_write(&vic, 0xD000, 24);
    vic_write(&vic, 0xD001, 51);
    vic_write(&vic, 0xD015, 0x01);
    vic_write(&vic, 0xD027, 0x02);
    vic_latch_raster(&vic, mem, 51);
    set_sprite_pointer(mem, &vic, 0, 0x21);
    mem->ram[0x0843] = 0x80;
    vic_write(&vic, 0xD027, 0x05);
    vic_latch_raster(&vic, mem, 52);
    set_sprite_pointer(mem, &vic, 0, 0x22); /* overwritten after the split */
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0x813338 &&
          pixel(display, 0, 1) == 0x56AC4D,
          "sprite registers and pointers are preserved per raster line");

    /* VICE's multicolor mask treats pair 01 as background and pairs 10/11
     * as foreground. A behind-background sprite must remain visible over 01
     * but disappear behind 10. */
    vic_reset(&vic);
    clear_video_memory(mem);
    mem_set_processor_port(mem, 0x07, 0x00);
    vic_write(&vic, 0xD011, 0x1B);
    vic_write(&vic, 0xD016, 0x18);
    vic_write(&vic, 0xD018, 0x14);
    vic_write(&vic, 0xD023, 0x03);
    mem->ram[0x0400] = 0x01;
    mem->color_ram[0] = 0x08;
    mem->chargen[0x1008] = 0x40; /* first pair 01: background */
    set_sprite_pointer(mem, &vic, 0, 0x20);
    mem->ram[0x0800] = 0x80;
    vic_write(&vic, 0xD000, 24);
    vic_write(&vic, 0xD001, 51);
    vic_write(&vic, 0xD015, 0x01);
    vic_write(&vic, 0xD01B, 0x01);
    vic_write(&vic, 0xD027, 0x02);
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0x813338,
          "priority sprite remains visible over multicolor pair 01");
    mem->chargen[0x1008] = 0x80; /* first pair 10: foreground */
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0x75CEC8,
          "priority sprite stays behind multicolor pair 10");

    /* Native C128 text mode uses the upper 4K half of the 8K character ROM. */
    vic_reset(&vic);
    mem->ram[0x0400] = 0x01;
    mem->color_ram[0] = 0x01;
    mem->chargen[0x0008] = 0x00;
    mem->chargen[0x1008] = 0x80;
    mem_set_processor_port(mem, 0x07, 0x00); /* colour banks 0, ROM visible */
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0xFFFFFF,
          "native C128 text uses upper character-ROM bank");
    CHECK(pixel(display, 1, 0) == 0x000000,
          "character glyph preserves clear pixels");

    /* CPU ROM and VIC ROM visibility are separate. Input bit 2 floats high,
     * so the VIC sees RAM even when the CPU's MMU maps character ROM. */
    mem_set_processor_port(mem, 0x03, 0x00); /* bit 2 floats high */
    CHECK(mem_read(mem, 0xD008) == 0x80,
          "CPU retains native character-ROM mapping when VIC uses RAM");
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0x000000,
          "input bit 2 selects RAM, not VIC character ROM");

    mem->ram[0x1008] = 0x40;
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 1, 0) == 0xFFFFFF && pixel(display, 0, 0) == 0x000000,
          "RAM-defined glyphs come from the $D018 character address");

    mem_set_processor_port(mem, 0x07, 0x04); /* bit 2 driven high */
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 1, 0) == 0xFFFFFF && pixel(display, 0, 0) == 0x000000,
          "output bit 2 high also selects RAM character data");

    mem_set_processor_port(mem, 0x47, 0x00); /* ROM on; bit 6 low on US model */
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0xFFFFFF,
          "$01 bit 6 does not switch the US VIC character-ROM bank");
    vic_write(&vic, 0xD018, 0x16); /* screen $0400, characters $1800 */
    mem->chargen[0x1808] = 0x40;
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 1, 0) == 0xFFFFFF && pixel(display, 0, 0) == 0x000000,
          "second half of VIC character-ROM window selects next ROM page");

    vic_write(&vic, 0xD018, 0x18); /* screen $0400, characters $2000 */
    mem->ram[0x2008] = 0x20;
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 2, 0) == 0xFFFFFF && pixel(display, 0, 0) == 0x000000,
          "outside ROM window $D018 selects RAM despite port bit 2 low");

    vic_set_bank(&vic, 1);
    mem->ram[0x4400] = 0x01;
    mem->ram[0x6008] = 0x10;
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 3, 0) == 0xFFFFFF,
          "RAM character fetch follows the CIA-selected VIC bank");

    vic_write(&vic, 0xD018, 0x14);
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 0, 0) == 0xFFFFFF,
          "native character ROM remains visible in another VIC bank");

    /* In C64 personality the VIC sees the lower 4K character-ROM half and
     * the first 1K color RAM, even when the latched CPU/VIC RAM bank is 1. */
    mmu_set_c64_enabled(&mem->mmu, true);
    mem->mmu.mcr = 0x40;
    mmu_write(&mem->mmu, 0xD505, 0x47);
    vic_set_bank(&vic, 4);
    vic_write(&vic, 0xD018, 0x14);
    mem->ram[0x10400] = 0x01;
    mem->chargen[0x0008] = 0x20;
    mem->chargen[0x1008] = 0x00;
    mem->color_ram[0] = 0x01;
    mem->color_ram[0x400] = 0x02;
    mem_set_processor_port(mem, 0x07, 0x03);
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 2, 0) == 0xFFFFFF,
          "C64 personality renders from lower character-ROM half");
    CHECK(pixel(display, 0, 0) == 0x000000,
          "C64 character glyph preserves clear pixels");
    mmu_set_c64_enabled(&mem->mmu, false);
    mem_set_processor_port(mem, 0x47, 0x00);
    mem->chargen[0x1008] = 0x80;

    vic_set_bank(&vic, 4); /* first 16K window in second 64K RAM bank */
    vic_write(&vic, 0xD018, 0x18);
    mem->ram[0x10400] = 0x01;
    mem->ram[0x12008] = 0x08;
    vic_render(&vic, mem, display);
    CHECK(pixel(display, 4, 0) == 0xFFFFFF,
          "RAM character fetch reaches the second 64K VIC RAM bank");

    vic_reset(&vic);
    vic_write(&vic, 0xD018, 0x14);

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
