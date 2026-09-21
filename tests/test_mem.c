#include "mem.h"
#include <stdio.h>
#include <stdlib.h>

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

    free(mem);
    if (failures == 0) { printf("test-mem: OK\n"); return 0; }
    printf("test-mem: %d failure(s)\n", failures);
    return 1;
}
