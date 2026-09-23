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
    CHECK(!mmu_cpu_is_8502(&mmu), "reset gives the bus to the Z80");

    mmu_write(&mmu, 0xD505, 0xB1);
    CHECK(mmu_cpu_is_8502(&mmu), "$D505 bit 0 gives the bus to the 8502");
    mmu_write(&mmu, 0xD505, 0xB0);
    CHECK(!mmu_cpu_is_8502(&mmu), "clearing $D505 bit 0 returns to the Z80");

    mmu_write(&mmu, 0xD506, 0x2A);   /* RAM configuration register */
    CHECK(mmu.rcr == 0x2A, "RAM configuration from $D506");
    CHECK(mmu_read(&mmu, 0xD506) == 0x2A, "$D506 readback");

    mmu_write(&mmu, 0xD508, 0x01);   /* bank latch commits with page number */
    CHECK(mmu_read(&mmu, 0xD508) == 0xF0, "uncommitted bank readback");
    mmu_write(&mmu, 0xD507, 0xC0);
    CHECK(mmu.page0 == 0xC0 && mmu.page0_bank == 1,
          "zero page relocation commits page and bank");
    CHECK(mmu_read(&mmu, 0xD508) == 0xF1, "committed zero-page bank readback");
    mmu_write(&mmu, 0xD50A, 0x01);
    mmu_write(&mmu, 0xD509, 0xF0);
    CHECK(mmu.page1 == 0xF0 && mmu.page1_bank == 1,
          "stack page relocation commits page and bank");

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

    /* The same hardware request changes personality only when the temporary
     * development gate has explicitly armed it. */
    mmu_set_c64_enabled(&mmu, true);
    mmu.mcr = 0x40;
    mmu_write(&mmu, 0xD505, 0x47);
    CHECK(mmu_is_c64_mode(&mmu), "enabled gate accepts C64 personality request");
    CHECK(mmu.c64_ram_bank == 1, "C64 personality latches the selected RAM bank");
    CHECK((mmu_read(&mmu, 0xD505) & 0x40) == 0,
          "$D505 mode bit is write-only in C64 personality");
    mmu_set_c64_enabled(&mmu, false);
    CHECK(!mmu_is_c64_mode(&mmu) && !(mmu.mcr5 & 0x40),
          "disabling gate leaves native personality active");
    mmu_reset(&mmu);
    CHECK(!mmu_is_c64_mode(&mmu) && !mmu.c64_enabled,
          "reset preserves the disabled test gate");

    if (failures == 0) { printf("test-mmu: OK\n"); return 0; }
    printf("test-mmu: %d failure(s)\n", failures);
    return 1;
}
