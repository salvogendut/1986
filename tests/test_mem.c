#define _POSIX_C_SOURCE 200809L
#include "mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

int main(void) {
    Mem *mem = calloc(1, sizeof(*mem));
    CHECK(mem != NULL, "allocate memory fixture");
    if (!mem) return 1;
    mem_init(mem);

    CHECK(mem->pla_data == 0xFF, "input processor-port lines float high");
    mem_set_processor_port(mem, 0x04, 0x00);
    CHECK(mem->pla_data == 0xFB,
          "output bit 2 low enables VIC character-ROM visibility");
    mem_set_processor_port(mem, 0x00, 0x00);
    CHECK(mem->pla_data == 0xFF,
          "writing bit 2 low while configured as input does not enable ROM");
    mem_set_processor_port(mem, 0x04, 0x04);
    CHECK(mem->pla_data == 0xFF, "output bit 2 high disables VIC ROM");
    mem_set_processor_port(mem, 0x47, 0x40);
    CHECK(mem->pla_data == 0xF8,
          "DDR masks output bits while leaving other lines high");

    mem->chargen[0x0000] = 0x24;
    mem->chargen[0x1000] = 0x42;
    mem->ram[0xD000] = 0x11;

    /* Native reset configuration $01 banks I/O out and exposes the native
     * C128 character-ROM half at $D000-$DFFF. */
    CHECK(!mem_io_visible(mem), "MCR $01 banks I/O out");
    CHECK(mem_read(mem, 0xD000) == 0x42,
          "native C128 character bank is readable at $D000");
    mem_set_processor_port(mem, 0x04, 0x00);
    CHECK(mem_read(mem, 0xD000) == 0x42,
          "VIC ROM select does not alter CPU native character-ROM half");
    mem_set_processor_port(mem, 0x44, 0x00);
    CHECK(mem_read(mem, 0xD000) == 0x42,
          "$01 bit 6 does not select character-ROM bank on US C128");

    /* Clearing CR bit 0 exposes I/O. mem_read() is then the underlying-memory
     * fallback used only after the machine bus decides not to dispatch I/O. */
    mem->mmu.mcr = 0x00;
    CHECK(mem_io_visible(mem), "MCR $00 exposes I/O");
    CHECK(mem_read(mem, 0xD000) == 0x11,
          "I/O configuration leaves underlying RAM available");

    /* With I/O hidden but RAM selected for the upper region, character ROM
     * must not override RAM. Writes always pass through to the RAM below. */
    mem->mmu.mcr = 0x31;
    CHECK(!mem_io_visible(mem), "MCR $31 banks I/O out");
    CHECK(mem_read(mem, 0xD000) == 0x11,
          "upper RAM selection does not expose character ROM");
    mem_write(mem, 0xD000, 0x99);
    CHECK(mem->ram[0xD000] == 0x99, "writes reach RAM beneath character ROM");

    /* On a 128K machine CR bit 6 selects bank 1; without common RAM only
     * the separately mapped zero and stack pages still point at bank 0. */
    mmu_ffxx_write(&mem->mmu, 0xFF00, 0x7F); /* bank 1, all RAM */
    mmu_write(&mem->mmu, 0xD506, 0x00);
    mem_write(mem, 0x0150, 0x12);
    mem_write(mem, 0x0250, 0x34);
    CHECK(mem->ram[0x0150] == 0x12 && mem->ram[0x10150] == 0,
          "default stack page stays in bank 0 without common RAM");
    CHECK(mem->ram[0x10250] == 0x34 && mem->ram[0x0250] == 0,
          "RAM above stack page follows bank 1 when common RAM is disabled");

    mmu_write(&mem->mmu, 0xD508, 0x01);
    mmu_write(&mem->mmu, 0xD507, 0xE0);
    mmu_write(&mem->mmu, 0xD50A, 0x01);
    mmu_write(&mem->mmu, 0xD509, 0x4F);
    mem_write(mem, 0x00E8, 0xA6);
    mem_write(mem, 0x01FC, 0xB7);
    CHECK(mem->ram[0x1E0E8] == 0xA6 && mem_read(mem, 0x00E8) == 0xA6,
          "relocated zero page accesses selected RAM bank");
    CHECK(mem->ram[0x14FFC] == 0xB7 && mem_read(mem, 0x01FC) == 0xB7 &&
          mem_cpu_page_offset(mem, 1) == 0x14F00,
          "relocated stack page uses committed page and bank");
    mmu_write(&mem->mmu, 0xD507, 0x00);
    mmu_write(&mem->mmu, 0xD509, 0x01);
    mmu_write(&mem->mmu, 0xD508, 0x00);
    mmu_write(&mem->mmu, 0xD507, 0x00);
    mmu_write(&mem->mmu, 0xD50A, 0x00);
    mmu_write(&mem->mmu, 0xD509, 0x01);

    static const unsigned common_sizes[] = { 0x400, 0x1000, 0x2000, 0x4000 };
    for (unsigned code = 0; code < 4; code++) {
        unsigned size = common_sizes[code];
        unsigned low_inside = size - 1;
        unsigned low_outside = size;
        mmu_write(&mem->mmu, 0xD506, 0x04 | code);
        mem->ram[low_inside] = 0x41;
        mem->ram[0x10000 + low_inside] = 0x42;
        mem->ram[low_outside] = 0x43;
        mem->ram[0x10000 + low_outside] = 0x44;
        CHECK(mem_read(mem, low_inside) == 0x41 &&
              mem_read(mem, low_outside) == 0x44,
              "lower common RAM read boundary follows RCR size");
        mem_write(mem, low_inside, 0x51);
        mem_write(mem, low_outside, 0x52);
        CHECK(mem->ram[low_inside] == 0x51 &&
              mem->ram[0x10000 + low_inside] == 0x42 &&
              mem->ram[0x10000 + low_outside] == 0x52 &&
              mem->ram[low_outside] == 0x43,
              "lower common RAM write boundary follows RCR size");

        unsigned high_inside = 0x10000 - size;
        unsigned high_outside = high_inside - 1;
        mmu_write(&mem->mmu, 0xD506, 0x08 | code);
        mem->ram[high_inside] = 0x61;
        mem->ram[0x10000 + high_inside] = 0x62;
        mem->ram[high_outside] = 0x63;
        mem->ram[0x10000 + high_outside] = 0x64;
        CHECK(mem_read(mem, high_inside) == 0x61 &&
              mem_read(mem, high_outside) == 0x64,
              "upper common RAM read boundary follows RCR size");
        mem_write(mem, high_inside, 0x71);
        mem_write(mem, high_outside, 0x72);
        CHECK(mem->ram[high_inside] == 0x71 &&
              mem->ram[0x10000 + high_inside] == 0x62 &&
              mem->ram[0x10000 + high_outside] == 0x72 &&
              mem->ram[high_outside] == 0x63,
              "upper common RAM write boundary follows RCR size");
    }

    /* The diagnostic ROM copies itself around $1000 and sets RCR=$06 (8K
     * shared low RAM) before its bank-1 test. Code must remain in bank 0. */
    mmu_ffxx_write(&mem->mmu, 0xFF00, 0x3F); /* copy while bank 0 selected */
    mmu_write(&mem->mmu, 0xD506, 0x06);
    mem_write(mem, 0x1067, 0xEA);
    mmu_ffxx_write(&mem->mmu, 0xFF00, 0x7F); /* execute in bank 1 */
    CHECK(mem->ram[0x1067] == 0xEA && mem->ram[0x11067] == 0 &&
          mem_read(mem, 0x1067) == 0xEA,
          "diagnostic code at $1067 remains accessible in bank 1");

    mmu_write(&mem->mmu, 0xD506, 0x0F); /* 16K at both ends */
    mem_write(mem, 0x3000, 0x81);
    mem_write(mem, 0x8000, 0x82);
    mem_write(mem, 0xF000, 0x83);
    CHECK(mem->ram[0x3000] == 0x81 && mem->ram[0x18000] == 0x82 &&
          mem->ram[0xF000] == 0x83,
          "both common ends leave the middle in bank 1");

    /* Writes beneath visible ROM must use the same common-RAM routing. */
    mmu_ffxx_write(&mem->mmu, 0xFF00, 0x41); /* bank 1, KERNAL visible */
    mem->kernal[0x100] = 0x91;
    mem_write(mem, 0xE100, 0x92);
    CHECK(mem_read(mem, 0xE100) == 0x91 &&
          mem->ram[0xE100] == 0x92 && mem->ram[0x1E100] == 0,
          "RAM behind KERNAL ROM respects upper common memory");

    /* C64 personality uses the 6510 processor port as its PLA and keeps the
     * RAM bank latched at the instant $D505 bit 6 is asserted. */
    mmu_set_c64_enabled(&mem->mmu, true);
    mem->mmu.mcr = 0x40;
    mmu_write(&mem->mmu, 0xD506, 0x00);
    mmu_write(&mem->mmu, 0xD505, 0x47);
    mem->c64_basic[0] = 0x64;
    mem->c64_kernal[0] = 0xE6;
    mem->chargen[0] = 0xC6;
    mem->ram[0x1A000] = 0x1A;
    mem->ram[0x1D000] = 0x1D;
    mem->ram[0x1E000] = 0x1E;
    mem_set_processor_port(mem, 0x07, 0x07);
    CHECK(mem_c64_mode(mem) && mem_io_visible(mem),
          "C64 personality exposes I/O with CHAREN and ROM lines high");
    CHECK(mem_read(mem, 0xA000) == 0x64 && mem_read(mem, 0xE000) == 0xE6,
          "C64 BASIC and KERNAL ROMs replace banked RAM");
    mem_write(mem, 0xA000, 0xA6);
    CHECK(mem_read(mem, 0xA000) == 0x64 && mem->ram[0x1A000] == 0xA6,
          "C64 ROM writes pass through to the latched RAM bank");
    mem_set_processor_port(mem, 0x07, 0x03);
    CHECK(!mem_io_visible(mem) && mem_read(mem, 0xD000) == 0xC6,
          "C64 CHAREN low maps the lower character-ROM half");
    mem_set_processor_port(mem, 0x07, 0x00);
    CHECK(mem_read(mem, 0xA000) == 0xA6 && mem_read(mem, 0xD000) == 0x1D &&
          mem_read(mem, 0xE000) == 0x1E,
          "C64 LORAM/HIRAM low expose underlying latched-bank RAM");
    mmu_set_c64_enabled(&mem->mmu, false);
    mmu_reset(&mem->mmu);

    /* U36 is selected separately for the lower and upper 16 KiB. */
    char u36_file[] = "/tmp/1986-u36-test-XXXXXX";
    int u36_fd = mkstemp(u36_file);
    CHECK(u36_fd >= 0, "create U36 test image");
    if (u36_fd >= 0) {
        u8 image[ROM_U36];
        memset(image, 0x36, 0x4000);
        memset(image + 0x4000, 0x63, 0x4000);
        CHECK(write(u36_fd, image, sizeof(image)) == sizeof(image),
              "write 32 KiB U36 image");
        close(u36_fd);
        CHECK(mem_attach_u36(mem, u36_file), "attach U36 image");
        mem->basic[0x4000] = 0xBA;
        mem->kernal[0] = 0xCE;
        mem->cart.attached = true;
        mem->cart.rom[0] = 0xCA;
        mem->cart.rom[0x6000] = 0xCB;
        mem->mmu.mcr = 0x15; /* lower and upper internal ROM */
        CHECK(mem_read(mem, 0x8000) == 0x36 &&
              mem_read(mem, 0xBFFF) == 0x36 &&
              mem_read(mem, 0xC000) == 0x63 &&
              mem_read(mem, 0xD000) == 0x63 &&
              mem_read(mem, 0xFFFF) == 0x63,
              "U36 maps both 16 KiB halves independently of cartridge");
        mem_write(mem, 0x8000, 0x77);
        CHECK(mem_read(mem, 0x8000) == 0x36 && mem->ram[0x8000] == 0x77,
              "writes pass through U36 to RAM");
        mem->mmu.mcr = 0x11; /* BASIC lower, U36 upper */
        CHECK(mem_read(mem, 0x8000) == 0xBA && mem_read(mem, 0xE000) == 0x63,
              "lower and upper ROM selectors are independent");
        mem->mmu.mcr = 0x29; /* external lower, external upper */
        CHECK(mem_read(mem, 0x8000) == 0xCA && mem_read(mem, 0xE000) == 0xCB,
              "external cartridge selection remains independent");
        mem_detach_u36(mem);
        mem->mmu.mcr = 0x15;
        CHECK(!mem->u36_attached && mem_read(mem, 0x8000) == 0 &&
              mem_read(mem, 0xE000) == 0,
              "empty U36 reads as blank ROM, not underlying RAM");
        FILE *half = fopen(u36_file, "wb");
        CHECK(half != NULL, "rewrite U36 test image");
        if (half) {
            memset(image, 0x16, 0x4000);
            CHECK(fwrite(image, 1, 0x4000, half) == 0x4000,
                  "write 16 KiB U36 image");
            fclose(half);
            CHECK(mem_attach_u36(mem, u36_file) &&
                  mem->u36_rom[0] == 0x16 && mem->u36_rom[0x4000] == 0x16,
                  "16 KiB U36 image mirrors into upper half");
        }
        FILE *short_image = fopen(u36_file, "wb");
        CHECK(short_image != NULL, "rewrite malformed U36 image");
        if (short_image) {
            CHECK(fwrite(image, 1, 0x1000, short_image) == 0x1000,
                  "write malformed U36 image");
            fclose(short_image);
            CHECK(!mem_attach_u36(mem, u36_file) && !mem->u36_attached,
                  "malformed replacement leaves U36 empty");
        }
        unlink(u36_file);
    }

    /* RESET preserves RAM, while a power cycle rebuilds volatile memory and
     * leaves the loaded ROMs and attached cartridge in place. */
    mem->ram[0] = mem->ram[128] = mem->ram[256] = 0x5a;
    mem->color_ram[0] = 0;
    mem->basic[0] = 0xb7;
    mem->cart.rom[0] = 0xc7;
    mem->cart.attached = true;
    mem_power_cycle(mem);
    CHECK(mem->ram[0] == 0 && mem->ram[127] == 0 &&
          mem->ram[128] == 0 && mem->ram[255] == 0 &&
          mem->ram[256] == 0,
          "power cycle clears both C128 RAM banks");
    CHECK(mem->color_ram[0] == 0x0f && mem->color_ram[0x7ff] == 0x0f,
          "power cycle reinitializes both color-RAM banks");
    CHECK(mem->basic[0] == 0xb7 && mem->cart.rom[0] == 0xc7 &&
          mem->cart.attached && mem->pla_data == 0xff,
          "power cycle preserves ROM media and floats the processor port");

    free(mem);
    if (failures == 0) { printf("test-mem: OK\n"); return 0; }
    printf("test-mem: %d failure(s)\n", failures);
    return 1;
}
