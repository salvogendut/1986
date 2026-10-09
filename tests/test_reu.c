#include "reu.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, m); ++failures; } } while (0)
static Reu reu;
static u8 host[65536];
static unsigned reads, writes;
static u8 read_host(void *ctx, u16 address) {
    (void)ctx; ++reads; return host[address];
}
static void write_host(void *ctx, u16 address, u8 value) {
    (void)ctx; ++writes; host[address] = value;
}
static const ReuBus bus = {NULL, read_host, write_host};

static void setup(unsigned kb) {
    reu_configure(&reu, kb);
    reu_power_cycle(&reu);
    memset(host, 0, sizeof(host));
    reads = writes = 0;
}

static void transfer(u16 address, u32 expansion, u16 length, u8 command, u8 control) {
    reu_write(&reu, 2, (u8)address); reu_write(&reu, 3, (u8)(address >> 8));
    reu_write(&reu, 4, (u8)expansion); reu_write(&reu, 5, (u8)(expansion >> 8));
    reu_write(&reu, 6, (u8)(expansion >> 16));
    reu_write(&reu, 7, (u8)length); reu_write(&reu, 8, (u8)(length >> 8));
    reu_write(&reu, 10, control);
    reu_write(&reu, 1, command);
}

static unsigned run(void) {
    unsigned clocks = 0;
    while (reu.active && clocks < 140000) { reu_tick(&reu, &bus); ++clocks; }
    CHECK(!reu.active, "DMA is bounded");
    return clocks;
}

int main(void) {
    setup(0);
    CHECK(reu_read(&reu, 0xdf00) == 0xff, "absent device floats high");
    transfer(0, 0, 1, 0x90, 0);
    CHECK(!reu.active && !reu.armed, "absent device cannot DMA");
    setup(512);
    CHECK(reu_read(&reu, 0xdf00) == 0x10 && reu_read(&reu, 1) == 0x10 &&
          reu_read(&reu, 6) == 0xf8 && reu_read(&reu, 7) == 0xff &&
          reu_read(&reu, 8) == 0xff && reu_read(&reu, 9) == 0x1f &&
          reu_read(&reu, 10) == 0x3f, "REC power-on register values");
    reu_write(&reu, 0, 0xff);
    reu_write(&reu, 0xdfe2, 0x56);
    CHECK(reu_read(&reu, 0xdf42) == 0x56 && reu_read(&reu, 0) == 0x10 &&
          reu_read(&reu, 0xdf0b) == 0xff && reu_read(&reu, 0xdfff) == 0xff,
          "32-byte IO2 mirrors, read-only status, unused registers");
    for (unsigned i = 0; i < 32; ++i) host[0x1000 + i] = (u8)(i ^ 0x5a);
    transfer(0x1000, 0x7fff0, 32, 0x90, 0);
    CHECK(reu.active && reu_read(&reu, 0xdf01) == 0xff, "IO2 disconnects during DMA");
    reu_write(&reu, 2, 0xff);
    CHECK(run() == 33, "stash takes one bus cycle per byte plus takeover");
    CHECK(!memcmp(reu.ram + 0x7fff0, host + 0x1000, 16) &&
          !memcmp(reu.ram, host + 0x1010, 16), "512K DRAM and REC address wrap");
    CHECK(reu_read(&reu, 2) == 0x20 && reu_read(&reu, 3) == 0x10 &&
          reu_read(&reu, 4) == 0x10 && reu_read(&reu, 6) == 0xf8 &&
          reu_read(&reu, 7) == 1 && reu_read(&reu, 1) == 0x10 &&
          reu_read(&reu, 0) == 0x50 && reu_read(&reu, 0) == 0x10,
          "completion advances addresses, leaves length=1, clears execute and read-clears status");
    transfer(0xfff0, 0x7fff0, 32, 0x91, 0);
    CHECK(run() == 33 && !memcmp(host + 0xfff0, host + 0x1000, 16) &&
          !memcmp(host, host + 0x1010, 16), "fetch and 16-bit host address wrap");

    /* All eight expansion banks must be independently addressable. */
    for (unsigned bank = 0; bank < 8; ++bank) {
        host[0x2000] = (u8)(0x70 + bank);
        transfer(0x2000, bank << 16, 1, 0x90, 0); run();
    }
    for (unsigned bank = 0; bank < 8; ++bank) {
        transfer((u16)(0x2100 + bank), bank << 16, 1, 0x91, 0); run();
        CHECK(host[0x2100 + bank] == 0x70 + bank, "independent REU banks");
    }
    host[0x2000] = 0x11; reu.ram[0x30000] = 0x22;
    transfer(0x2000, 0x30000, 1, 0x92, 0);
    CHECK(run() == 3 && host[0x2000] == 0x22 && reu.ram[0x30000] == 0x11,
          "swap exchanges bytes using two bus cycles");
    host[0x1234] = 0xa5;
    transfer(0x1234, 0x20000, 20, 0x90, 0x80); run();
    CHECK(reu.ram[0x20013] == 0xa5 && reu_read(&reu, 2) == 0x34,
          "fixed host address fills expansion RAM");
    transfer(0x3000, 0x20000, 20, 0x91, 0x40); run();
    CHECK(host[0x3013] == 0xa5 && reu_read(&reu, 4) == 0,
          "fixed expansion address fills host RAM");
    transfer(0x1234, 0x40000, 0, 0x90, 0x80);
    CHECK(run() == 65537 && reu.ram[0x4ffff] == 0xa5,
          "zero length transfers a full 64K, not zero bytes");

    transfer(0x2000, 0x10000, 2, 0xb0, 0); run();
    CHECK(reu_read(&reu, 2) == 0 && reu_read(&reu, 3) == 0x20 &&
          reu_read(&reu, 4) == 0 && reu_read(&reu, 6) == 0xf9 && reu_read(&reu, 7) == 2,
          "autoload restores all programmed address/length registers");
    transfer(0x20ff, 0x1ffff, 2, 0x90, 0); run();
    reu_write(&reu, 2, 0x55); reu_write(&reu, 4, 0x66); reu_write(&reu, 8, 1);
    CHECK(reu_read(&reu, 3) == 0x20 && reu_read(&reu, 5) == 0xff && reu_read(&reu, 7) == 2,
          "partial register writes restore their shadow partner (half-autoload)");
    transfer(0x1234, 0, 1, 0x80, 0);
    reu_tick(&reu, &bus);
    CHECK(!reu.active && reu.armed, "command can wait for FF00");
    reu_ff00_trigger(&reu); run();
    CHECK(reu.ram[0] == 0xa5 && !reu.armed, "FF00 starts the armed transfer once");
    reu_ff00_trigger(&reu);
    CHECK(!reu.active, "FF00 cannot retrigger a completed command");

    setup(512);
    reu_write(&reu, 9, 0xc0);
    transfer(0x1000, 0, 1, 0x90, 0); run();
    CHECK(reu_irq(&reu) && reu_read(&reu, 0) == 0xd0 && !reu_irq(&reu),
          "end-of-block IRQ is cleared by status read");
    reu_write(&reu, 9, 0);
    transfer(0x1000, 0, 1, 0x90, 0); run();
    CHECK(!reu_irq(&reu), "masked completion has no IRQ");
    reu_write(&reu, 9, 0xc0);
    CHECK(reu_irq(&reu), "enabling a pending event asserts IRQ");
    reu_write(&reu, 9, 0);
    CHECK(reu_irq(&reu), "mask changes do not clear an already latched IRQ");
    reu_read(&reu, 0);

    /* VICE models the REC's special final/penultimate compare behavior. */
    for (unsigned fail = 0; fail < 4; ++fail) {
        reu_reset(&reu);
        memset(host + 0x1000, 0, 4); memset(reu.ram, 0, 4);
        host[0x1000 + fail] = 1;
        reu_write(&reu, 9, 0xa0);
        transfer(0x1000, 0, 4, 0x93, 0);
        CHECK(run() == (fail == 3 ? 5 : fail + 3), "verify mismatch cycle count");
        u8 expected = fail >= 2 ? 0xf0 : 0xb0;
        CHECK(reu_irq(&reu) && reu_read(&reu, 0) == expected &&
              reu_read(&reu, 2) == fail + 1 &&
              reu_read(&reu, 7) == (fail >= 2 ? 1 : 3 - fail),
              "verify fault updates address, residual length and EOB quirks");
    }
    reu_reset(&reu); memset(host + 0x1000, 0, 4);
    transfer(0x1000, 0, 4, 0x93, 0);
    CHECK(run() == 5 && reu_read(&reu, 0) == 0x50, "successful compare sets EOB only");

    setup(128);
    CHECK(reu_read(&reu, 0) == 0, "1700 reports 64K DRAM chips");
    host[0x1000] = 0x9a; host[0x1001] = 0xbc;
    transfer(0x1000, 0x1ffff, 2, 0x90, 0); run();
    CHECK(reu.ram[0x1ffff] == 0x9a && reu.ram[0] == 0xbc && reu_read(&reu, 6) == 0xf8,
          "1700 wraps at 128K");
    transfer(0x2000, 0x20000, 1, 0x91, 0); run();
    CHECK(host[0x2000] == 0xbc, "1700 upper banks mirror DRAM");
    setup(256);
    host[0x1000] = 0x45;
    transfer(0x1000, 0x40000, 1, 0x90, 0); run();
    transfer(0x2000, 0x40000, 1, 0x91, 0); run();
    CHECK(host[0x2000] == 0x45 && reu.ram[0] == 0,
          "1764 missing banks return the floating latch, not mirrored DRAM");
    reu.ram[0x1234] = 0x67;
    transfer(0, 0, 0, 0x90, 0);
    reu_reset(&reu);
    CHECK(!reu.active && !reu.armed && !reu_irq(&reu) && reu.ram[0x1234] == 0x67,
          "RESET cancels DMA but retains REU RAM");
    reu_power_cycle(&reu);
    CHECK(reu.size_kb == 256 && reu.ram[0x1234] == 0, "power cycle clears volatile expansion RAM");
    reu_configure(&reu, 64);
    CHECK(reu.size_kb == 256, "unsupported sizes cannot reconfigure the REC");
    if (!failures) puts("test-reu: OK");
    return failures != 0;
}
