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

    free(mem);
    if (failures == 0) { printf("test-mem: OK\n"); return 0; }
    printf("test-mem: %d failure(s)\n", failures);
    return 1;
}
