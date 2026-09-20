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

    if (failures == 0) { printf("test-mmu: OK\n"); return 0; }
    printf("test-mmu: %d failure(s)\n", failures);
    return 1;
}
