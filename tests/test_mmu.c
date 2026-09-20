#include "mmu.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

int main(void) {
    Mmu mmu;
    mmu_init(&mmu);

    CHECK(mmu.mcr == 0x01, "default MCR");
    CHECK(mmu.mmio, "MMIO mapped by default");

    mmu_write(&mmu, 0xD506, 0x2A);   /* RAM configuration register */
    CHECK(mmu.rcr == 0x2A, "RAM configuration from $D506");
    CHECK(mmu_read(&mmu, 0xD506) == 0x2A, "$D506 readback");

    mmu_write(&mmu, 0xD507, 0xC0);   /* 2 MHz + Z80 */
    CHECK((mmu.mode & 0xC0) == 0xC0, "mode bits");

    mmu_write(&mmu, 0xD50D, 0x02);
    CHECK(mmu.vdc_bank == 0x02, "VDC bank");

    /* $D505 bit 6 requests the separate C64 personality. The native-C128
     * emulator rejects it, reports it once, and preserves the other bits. */
    mmu_write(&mmu, 0xD505, 0x47);
    CHECK((mmu.mcr5 & 0x40) == 0, "C64 mode request is rejected");
    CHECK((mmu.mcr5 & 0x0F) == 0x07, "native MCR bits are preserved");
    CHECK(mmu_take_c64_request(&mmu), "first C64 request is reported");
    CHECK(!mmu_take_c64_request(&mmu), "C64 request is consumed once");
    mmu_write(&mmu, 0xD505, 0x47);
    CHECK(!mmu_take_c64_request(&mmu), "repeated active request is deduplicated");
    mmu_write(&mmu, 0xD505, 0x07);
    mmu_write(&mmu, 0xD505, 0x47);
    CHECK(mmu_take_c64_request(&mmu), "a later C64 request is reported again");

    if (failures == 0) { printf("test-mmu: OK\n"); return 0; }
    printf("test-mmu: %d failure(s)\n", failures);
    return 1;
}
