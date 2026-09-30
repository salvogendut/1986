#include "c128.h"
#include "leds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exercise the real scheduler and CPU cores without a window or ROMs. */
int g_debug_enabled;
void display_set_vdc_active(Display *d, bool active) { d->vdc_active = active; }
void leds_ping(LedId id) { (void)id; }
void leds_set_drive_unit(LedId id, int unit) { (void)id; (void)unit; }
void leds_set_cpu_frequency(unsigned n) { (void)n; }
void leds_set_z80_frequency(unsigned n) { (void)n; }
void notify_post(const char *fmt, ...) { (void)fmt; }
static int failures;
#define CHECK(c, text) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", text); ++failures; } } while (0)

static void setup(C128 *c) {
    c128_reset(c);
    c->mem.mmu.mcr = 0x3e; /* RAM plus I/O */
    mmu_write(&c->mem.mmu, 0xd505, 1);
    c->mem.ram[0xfffc] = 0; c->mem.ram[0xfffd] = 0x10;
    /* INX; JMP $1000 (five clocks per iteration). */
    const u8 code[] = {0xe8, 0x4c, 0, 0x10};
    memcpy(c->mem.ram + 0x1000, code, sizeof(code));
    cpu_reset(&c->cpu);
    vic_write(&c->vic, 0xd011, 0); /* no display DMA in frequency tests */
    cia_write(&c->cia1, 4, 0xff);
    cia_write(&c->cia1, 5, 0xff);
    cia_write(&c->cia1, 14, 0x11);
}

int main(void) {
    static C128 c;
    Config cfg;
    config_set_defaults(&cfg);
    c128_init(&c, &cfg);
    setup(&c);
    c128_frame(&c);
    u64 slow = cpu_cycles();
    CHECK(c.bus_cycles >= CPU_PAL_FRAME_CYCLES &&
          c.bus_cycles < CPU_PAL_FRAME_CYCLES + 10,
          "a PAL frame advances the shared clock by 19656 cycles");
    CHECK(c.cia1.ta_counter > 45000 && c.cia1.ta_counter < 46000,
          "CIA timer follows the one-MHz clock");
    CHECK(c.audio_count > 850 && c.audio_count < 900,
          "SID sample count follows the PAL frame clock");

    setup(&c);
    vic_write(&c.vic, 0xd030, 1);
    c128_frame(&c);
    CHECK(cpu_cycles() >= slow * 2 - 10 && cpu_cycles() <= slow * 2 + 10,
          "2 MHz has twice as many clock edges, including refresh waits");
    unsigned instructions = (cpu_cycles() - 5 * VIC_RASTER_LINES) / 5;
    CHECK((u8)(instructions + (c.cpu.pc == 0x1001)) == c.cpu.x,
          "five refresh waits per line reduce retired CPU instructions");
    CHECK(c.bus_cycles >= CPU_PAL_FRAME_CYCLES &&
          c.bus_cycles < CPU_PAL_FRAME_CYCLES + 10 &&
          c.cia1.ta_counter > 45000 && c.cia1.ta_counter < 46000,
          "2 MHz does not double the VIC or CIA clocks");

    setup(&c);
    vic_write(&c.vic, 0xd030, 1);
    const u8 io_code[] = {0x8d, 0x20, 0xd0, 0xad, 0x12, 0xd0,
                         0x24, 0xff, 0x8d, 0x20, 0xd0};
    memcpy(c.mem.ram + 0x1000, io_code, sizeof(io_code));
    c.paused = true;
    const unsigned half_cycles[] = {4, 4, 3, 5};
    for (unsigned i = 0; i < 4; i++) {
        u64 before = c.vic.beam_half_clock;
        CHECK(c128_debug_step(&c, C128_DEBUG_CPU_8502), "step I/O phase fixture");
        c128_frame(&c);
        CHECK(c.vic.beam_half_clock - before == half_cycles[i],
              "2 MHz I/O aligns read start and write completion to PHI2");
    }

    /* A D030 write in the middle of a line changes the very next access,
     * not the next line's CPU budget. */
    setup(&c);
    const u8 switch_code[] = {0xa9, 1, 0x8d, 0x30, 0xd0, 0xea, 0x4c, 5, 0x10};
    memcpy(c.mem.ram + 0x1000, switch_code, sizeof(switch_code));
    unsigned bp = c128_debug_breakpoint_add(&c, C128_DEBUG_CPU_8502, 0x1005);
    c128_frame(&c);
    u64 before = c.vic.beam_half_clock;
    CHECK(c.paused && c.vic.fast_mode, "debugger stops after the speed write");
    CHECK(c128_debug_step(&c, C128_DEBUG_CPU_8502), "step active 8502");
    c128_frame(&c);
    CHECK(c.cpu.pc == 0x1006 && c.vic.beam_half_clock - before == 2,
          "two-clock NOP after D030 takes two half cycles");
    c128_debug_breakpoint_remove(&c, bp);

    /* A partially scanned frame resumes after F8 stepping, not from line0. */
    c128_debug_continue(&c);
    c128_frame(&c);
    CHECK(c.vic.beam_half_clock >= 2 * CPU_PAL_FRAME_CYCLES &&
          c.vic.beam_half_clock < 2 * CPU_PAL_FRAME_CYCLES + 10,
          "debugger resume completes the interrupted video frame");

    setup(&c);
    /* LD A,1; LD BC,$D505; OUT (C),A hands control to the parked 8502. */
    const u8 zcode[] = {0x3e, 1, 0x01, 5, 0xd5, 0xed, 0x79, 0x76};
    memcpy(c.mem.ram + 0x2000, zcode, sizeof(zcode));
    c.z80.pc = 0x2000;
    mmu_write(&c.mem.mmu, 0xd505, 0);
    c128_frame(&c);
    CHECK(mmu_cpu_is_8502(&c.mem.mmu) && cpu_cycles() > 1000,
          "Z80 hands the bus to the 8502 within the current frame");
    CHECK(c.vic.beam_half_clock >= 2 * CPU_PAL_FRAME_CYCLES &&
          c.vic.beam_half_clock < 2 * CPU_PAL_FRAME_CYCLES + 20,
          "CPU handoff retains one shared video timeline");
    free(c.vdc.fb); free(c.vdc.display_fb);
    if (!failures) puts("test-machine-clock: OK");
    return failures != 0;
}
