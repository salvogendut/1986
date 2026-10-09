#include "c128.h"
#include "leds.h"
#include "paste.h"
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

static void reu_command(C128 *c, u16 host, u32 expansion, u16 length, u8 command) {
    c128_mem_write(c, 0xdf02, host); c128_mem_write(c, 0xdf03, host >> 8);
    c128_mem_write(c, 0xdf04, expansion); c128_mem_write(c, 0xdf05, expansion >> 8);
    c128_mem_write(c, 0xdf06, expansion >> 16);
    c128_mem_write(c, 0xdf07, length); c128_mem_write(c, 0xdf08, length >> 8);
    c128_mem_write(c, 0xdf01, command);
}

static void test_reu_machine(C128 *c) {
    setup(c);
    CHECK(c128_mem_read(c, 0xdf00) == 0xff, "REU is absent by default");
    CHECK(c128_set_reu(c, true, 512) && c->cfg->reu_enabled,
          "enable live 512K REU");
    CHECK(!c128_set_reu(c, true, 64) && c->reu.size_kb == 512,
          "reject invalid expansion size without changing hardware");
    /* DMA RAM bank comes from RCR, not the CPU CR; shared/relocated pages
     * and the 8502 on-chip $00/$01 ports must not leak into REU transfers. */
    c->mem.mmu.rcr = 0x4c;
    c->mem.mmu.page0 = 0x30; c->mem.mmu.page1 = 0x31;
    c->cpu.io_ddr = 0x2f; c->cpu.io_port = 0x37;
    c->mem.ram[0x10000] = 0xa5; c->mem.ram[0x10001] = 0x5a;
    c->mem.ram[0x10002] = 0x91; c->mem.ram[0x10100] = 0x62;
    c->mem.ram[0x3000] = 0x33; c->mem.ram[0x3100] = 0x44;
    reu_command(c, 0, 0x10000, 0x101, 0x90);
    c128_frame(c);
    CHECK(c->reu.ram[0x10000] == 0xa5 && c->reu.ram[0x10001] == 0x5a &&
          c->reu.ram[0x10002] == 0x91 && c->reu.ram[0x10100] == 0x62,
          "DMA bypasses the CPU port, page relocation and common RAM");
    c->reu.ram[0] = 0x77;
    reu_command(c, 0, 0, 1, 0x91); c128_frame(c);
    CHECK(c->mem.ram[0x10000] == 0x77 && c->cpu.io_ddr == 0x2f && c->mem.ram[0x3000] == 0x33,
          "DMA write to $00 changes physical RAM, not DDR or relocated zero page");

    setup(c);
    c->mem.basic[0] = 0xab; c->mem.ram[0x4000] = 0xcd;
    c->mem.mmu.mcr = 0x3c; /* BASIC low visible, otherwise RAM and I/O */
    reu_command(c, 0x4000, 0, 1, 0x90); c128_frame(c);
    CHECK(c->reu.ram[0] == 0xab, "DMA reads visible ROM through native decoder");
    c->reu.ram[0] = 0xef;
    reu_command(c, 0x4000, 0, 1, 0x91); c128_frame(c);
    CHECK(c->mem.ram[0x4000] == 0xef && c->mem.basic[0] == 0xab,
          "DMA writes RAM beneath ROM without modifying ROM");
    c->mem.mmu.mcr = 0x3f;
    c128_mem_write(c, 0xdf02, 0x88);
    CHECK(c128_mem_read(c, 0xdf02) == 0x88 && reu_peek(&c->reu, 2) != 0x88,
          "hidden IO2 is ordinary RAM");

    setup(c);
    c->reu.ram[0] = 0x35;
    reu_command(c, 0x2000, 0, 1, 0x81);
    CHECK(c->reu.armed && !c->reu.active, "native command arms FF00 trigger");
    c128_mem_write(c, 0xff00, 0x3f);
    CHECK(c->mem.mmu.mcr == 0x3f && c->reu.active,
          "FF00 changes MMU mapping before starting DMA");
    c128_frame(c);
    CHECK(c->mem.ram[0x2000] == 0x35, "delayed native fetch completes with IO2 hidden");

    setup(c);
    c->reu.ram[0] = 0x06;
    reu_command(c, 0xd020, 0, 1, 0x91); c128_frame(c);
    CHECK((c128_mem_read(c, 0xd020) & 15) == 6, "DMA reaches mapped VIC registers");
    reu_command(c, 0xdf00, 0x100, 11, 0x90); c128_frame(c);
    bool floated = true;
    for (unsigned i = 0; i < 11; ++i) floated &= c->reu.ram[0x100 + i] == 0xff;
    CHECK(floated, "DMA cannot read back its own IO2 registers");
    memset(c->reu.ram + 0x100, 0x90, 11);
    reu_command(c, 0xdf00, 0x100, 11, 0x91); c128_frame(c);
    CHECK(!c->reu.active && !c->reu.armed && reu_peek(&c->reu, 1) == 0x11,
          "DMA cannot recursively reprogram its own command register");

    unsigned remaining[2];
    for (unsigned display = 0; display < 2; ++display) {
        setup(c);
        if (display) vic_write(&c->vic, 0xd011, 0x1b);
        c128_mem_write(c, 0xdf0a, 0x80);
        reu_command(c, 0x2345, 0, 0, 0x90);
        c128_frame(c);
        remaining[display] = c->reu.remaining;
    }
    CHECK(remaining[1] > remaining[0], "VIC badline DMA stalls the REU bus");

    /* A 64K transfer spans several frames without executing CPU code, yet
     * video, audio and CIA clocks keep advancing at both CPU speeds. */
    for (unsigned fast = 0; fast < 2; ++fast) {
        setup(c);
        vic_write(&c->vic, 0xd030, fast);
        c->mem.ram[0x2345] = 0x6d;
        c128_mem_write(c, 0xdf0a, 0x80);
        reu_command(c, 0x2345, 0, 0, 0x90);
        u16 pc = c->cpu.pc;
        for (int frame = 0; frame < 3; ++frame) {
            u64 before = c->bus_cycles;
            c128_frame(c);
            CHECK(c->reu.active && c->cpu.pc == pc &&
                  c->bus_cycles - before == CPU_PAL_FRAME_CYCLES &&
                  c->audio_count > 850,
                  "REU holds CPU while each frame and its peripherals remain bounded");
        }
        c128_frame(c);
        CHECK(!c->reu.active && c->reu.ram[0xffff] == 0x6d,
              "full-bank DMA finishes at the same bus speed in 1/2 MHz modes");
    }
    setup(c);
    c->reu.ram[0] = 0x99;
    c128_mem_write(c, 0xdf09, 0xc0);
    reu_command(c, 0x2000, 0, 1, 0x91); c128_frame(c);
    CHECK(reu_irq(&c->reu) && c->cpu.irq_level, "REU IRQ reaches the 8502");
    CHECK(c128_debug_mem_read(c, C128_DEBUG_CPU_8502, 0xdf00) == 0xd0 &&
          reu_irq(&c->reu) && c->cpu.irq_level,
          "monitor can inspect REC status without acknowledging its IRQ");
    c128_mem_read(c, 0xdf00);
    CHECK(!c->cpu.irq_level, "status acknowledgement immediately releases IRQ");

    setup(c);
    mmu_write(&c->mem.mmu, 0xd505, 0);
    c->mem.mmu.mcr = 0x7e;
    c->z80.pc = 0x2000; c->z80.halted = true;
    c->z80_bus.io_write(c, 0xdf02, 0);
    c->z80_bus.io_write(c, 0xdf03, 0x40);
    c->z80_bus.io_write(c, 0xdf07, 1);
    c->z80_bus.io_write(c, 0xdf08, 0);
    c->z80_bus.io_write(c, 0xdf09, 0xc0);
    c->reu.ram[0] = 0xb6;
    c->z80_bus.io_write(c, 0xdf01, 0x91);
    c128_frame(c);
    CHECK(c->mem.ram[0x4000] == 0xb6 && c->mem.ram[0x14000] != 0xb6,
          "Z80 I/O can program REU; DMA still uses the RCR bank");
    CHECK(c->z80.pending_irq && reu_irq(&c->reu), "REU IRQ reaches the Z80");
    c->z80_bus.io_read(c, 0xdf00);
    CHECK(!c->z80.pending_irq, "Z80 status read acknowledges the REU IRQ");

    setup(c);
    mmu_write(&c->mem.mmu, 0xd505, 0x41);
    mem_set_processor_port(&c->mem, 0x2f, 0x37);
    c->reu.ram[0] = 0xd2;
    reu_command(c, 0x2000, 0, 1, 0x81);
    c128_mem_write(c, 0xff00, 0x5a); c128_frame(c);
    CHECK(c->mem.ram[0x2000] == 0xd2 && c->mem.ram[0xff00] == 0x5a,
          "C64 personality retains IO2 and the FF00 trigger");
    c128_reset(c);
    CHECK(c->reu.ram[0] == 0xd2 && !c->reu.active, "machine RESET retains REU RAM");
    c128_power_cycle(c);
    CHECK(c->reu.size_kb == 512 && c->reu.ram[0] == 0, "machine power cycle clears REU RAM");
    c->mem.mmu.mcr = 0x3e; /* expose IO2 again after the power-on CR */
    CHECK(c128_set_reu(c, false, 512) && c128_mem_read(c, 0xdf00) == 0xff,
          "disable detaches IO2 and clears pending expansion state");
}

/* Optional private-ROM smoke test: exercise the same native BASIC boot and
 * presented VDC framebuffer as the application, without opening a window. */
static void test_basic_cursor(C128 *c) {
    const char *rom_dir = getenv("C128_TEST_ROM_DIR");
    if (!rom_dir || !*rom_dir) return;
    Config cfg;
    config_set_defaults(&cfg);
    cfg.reu_enabled = true;
    c128_init(c, &cfg);
    int loaded = mem_load_c128_roms(&c->mem, rom_dir);
    CHECK(loaded >= 3, "load private ROMs for native BASIC cursor test");
    if (loaded >= 3) {
        c->col_mode_80 = true;
        c128_power_cycle(c);
        cpu_install_iec_traps(c->mem.kernal, NULL);
        for (int frame = 0; frame < 300; ++frame) c128_frame(c);
        CHECK(c->display.vdc_active && c->vdc.display_fb_valid &&
              c->vdc.regs[1] == 80 && (c->vdc.regs[10] & 0x40),
              "native 80-column BASIC enables the blinking hardware cursor");
        size_t bytes = sizeof(u32) * VDC_SCREEN_W * VDC_SCREEN_H;
        u32 *reference = malloc(bytes);
        CHECK(reference != NULL, "allocate BASIC cursor reference frame");
        if (reference) {
            memcpy(reference, c->display.vdc_pixels, bytes);
            u16 cursor = c->vdc.cursor_adr;
            int changed_frames = 0, unchanged_frames = 0;
            bool stable = true, cursor_only = true;
            for (int frame = 0; frame < 80; ++frame) {
                c128_frame(c);
                stable &= c->vdc.cursor_adr == cursor && !c->paused;
                int min_x = VDC_SCREEN_W, min_y = VDC_SCREEN_H;
                int max_x = -1, max_y = -1;
                for (int y = 0; y < VDC_SCREEN_H; ++y)
                    for (int x = 0; x < VDC_SCREEN_W; ++x) {
                        int i = y * VDC_SCREEN_W + x;
                        if (reference[i] == c->display.vdc_pixels[i]) continue;
                        if (x < min_x) min_x = x;
                        if (y < min_y) min_y = y;
                        if (x > max_x) max_x = x;
                        if (y > max_y) max_y = y;
                    }
                if (max_x < 0) ++unchanged_frames;
                else {
                    ++changed_frames;
                    int cell_height = ((c->vdc.regs[9] & 31) + 1) *
                        VDC_SCREEN_H / c->vdc.fb_h + 1;
                    cursor_only &= max_x - min_x + 1 <= 8 &&
                        max_y - min_y + 1 <= cell_height;
                }
            }
            CHECK(stable && cursor_only && changed_frames > 0 && unchanged_frames > 0,
                  "idle BASIC alternates cursor pixels while the rest of the screen stays stable");
            free(reference);
        }
        Paste paste;
        paste_init(&paste);
        paste_text(&paste, "BANK 0:POKE 8192,123:STASH 1,8192,0,7:POKE 8192,0:FETCH 1,8192,0,7\r");
        for (int frame = 0; frame < 500; ++frame) {
            paste_tick(&paste, &c->kbd);
            c128_frame(c);
        }
        CHECK(paste.pos == paste.len && !paste.held &&
              c->reu.ram[0x70000] == 123 && c->mem.ram[8192] == 123 && !c->paused,
              "native BASIC 7 STASH/FETCH round-trips data through the eighth REU bank");
        paste_text(&paste, "BANK 1:POKE 8192,77:SWAP 1,8192,0,7\r");
        for (int frame = 0; frame < 300; ++frame) {
            paste_tick(&paste, &c->kbd);
            c128_frame(c);
        }
        CHECK(paste.pos == paste.len && !paste.held &&
              c->reu.ram[0x70000] == 77 && c->mem.ram[0x12000] == 123 && !c->paused,
              "native BASIC 7 SWAP uses the selected C128 RAM bank");
        paste_free(&paste);
    }
    free(c->vdc.fb);
    free(c->vdc.display_fb);
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
    test_reu_machine(&c);
    free(c.vdc.fb); free(c.vdc.display_fb);
    test_basic_cursor(&c);
    if (!failures) puts("test-machine-clock: OK");
    return failures != 0;
}
