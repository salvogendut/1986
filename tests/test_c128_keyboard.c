#include "c128.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

/* Keep this machine-level test independent of SDL window creation. */
void display_set_vdc_active(Display *d, bool active) {
    d->vdc_active = active;
}

static int failures;
#define CHECK(condition, message) do { if (!(condition)) { \
    fprintf(stderr, "FAIL: %s\n", message); ++failures; } } while (0)

static u64 test_cpu_clock;
u64 cpu_cycles(void) { return test_cpu_clock; }

static bool pressed(const Kbd *kbd, int row, int col) {
    return (kbd_matrix(kbd, row) & (1u << col)) == 0;
}

int main(void) {
    C128 *c = calloc(1, sizeof(*c));
    Config cfg = {0};
    CHECK(c != NULL, "allocate keyboard test machine");
    if (!c) return 1;
    c->cfg = &cfg;
    kbd_reset(&c->kbd);

    c128_key_event(c, SDL_SCANCODE_LSHIFT, true);
    c128_key_event(c, SDL_SCANCODE_LALT, true);
    CHECK(pressed(&c->kbd, KBD_LSHIFT_ROW, KBD_LSHIFT_COL) &&
          pressed(&c->kbd, 7, 5),
          "host Shift+Alt presses native Shift+C= matrix positions");
    c128_key_event(c, SDL_SCANCODE_LALT, false);
    c128_key_event(c, SDL_SCANCODE_LSHIFT, false);
    CHECK(!pressed(&c->kbd, KBD_LSHIFT_ROW, KBD_LSHIFT_COL) &&
          !pressed(&c->kbd, 7, 5), "Shift+C= releases cleanly");

    CHECK((c128_cpu_port_value(c) & 0x40) != 0,
          "released CAPS switch reads high at the 8502 port");
    c128_key_event(c, SDL_SCANCODE_CAPSLOCK, true);
    CHECK(c->kbd.caps_lock && (c128_cpu_port_value(c) & 0x40) == 0,
          "CapsLock toggles the active-low physical CAPS switch");
    c128_key_event(c, SDL_SCANCODE_CAPSLOCK, true);
    CHECK(c->kbd.caps_lock, "repeated CapsLock keydown does not toggle again");
    c128_key_event(c, SDL_SCANCODE_CAPSLOCK, false);
    CHECK(c->kbd.caps_lock, "CAPS switch remains latched after key release");
    c128_key_event(c, SDL_SCANCODE_CAPSLOCK, true);
    CHECK(!c->kbd.caps_lock && (c128_cpu_port_value(c) & 0x40) != 0,
          "second CapsLock press releases the physical switch");
    c128_key_event(c, SDL_SCANCODE_CAPSLOCK, false);

    c128_key_event(c, SDL_SCANCODE_TAB, true);
    c128_key_event(c, SDL_SCANCODE_KP_9, true);
    c128_key_event(c, SDL_SCANCODE_RIGHT, true);
    CHECK(pressed(&c->kbd, 8, 3) && pressed(&c->kbd, 9, 6) &&
          pressed(&c->kbd, 10, 6),
          "host extended keys occupy the three C128-only matrix rows");
    c128_key_event(c, SDL_SCANCODE_TAB, false);
    c128_key_event(c, SDL_SCANCODE_KP_9, false);
    c128_key_event(c, SDL_SCANCODE_RIGHT, false);

    kbd_set(&c->kbd, 8, 3, true);
    c->vic.keyboard_select = 0xFE;
    CHECK((c128_keyboard_port_b(c, 0xFF) & (1u << 3)) == 0,
          "$D02F row 8 selection reaches CIA1 port B");
    c->vic.keyboard_select = 0xFF;
    CHECK(c128_keyboard_port_b(c, 0xFF) == 0xFF,
          "deselecting $D02F extended rows releases CIA1 port B");
    kbd_set(&c->kbd, 8, 3, false);

    c128_key_event(c, SDL_SCANCODE_ESCAPE, true);
    c128_key_event(c, SDL_SCANCODE_PAGEUP, true);
    CHECK(pressed(&c->kbd, 9, 0) && c->restore_down,
          "Escape+PageUp supplies C128 ESC and RESTORE NMI");
    c128_key_event(c, SDL_SCANCODE_PAGEUP, false);
    c128_key_event(c, SDL_SCANCODE_ESCAPE, false);
    CHECK(!pressed(&c->kbd, 9, 0) && !c->restore_down,
          "C128 ESC and RESTORE release cleanly");
    c128_key_event(c, SDL_SCANCODE_END, true);
    CHECK(pressed(&c->kbd, 7, 7), "End supplies RUN/STOP");
    c128_key_event(c, SDL_SCANCODE_END, false);

    /* F10 without a reset selects an output, but must not copy one screen
     * into the other's independent video memory or change VDC geometry. */
    c->col_mode_80 = true;
    c->mem.mmu.col4080 = false;
    c->vic.screen_addr = 0x0400;
    c->mem.ram[0x0400] = 0x51;
    c->vdc.ram[0] = 0x41;
    c->vdc.ram[0x04ff] = 0x42;
    c->vdc.screen_adr = 0x1000;
    c->vdc.chargen_adr = 0x6000;
    c->vdc.bytes_per_char = 16;
    c128_switch_4080(c);
    CHECK(!c->col_mode_80 && !c->display.vdc_active && c->mem.mmu.col4080,
          "F10 selects the VIC output");
    CHECK(c->mem.ram[0x0400] == 0x51,
          "switching to VIC leaves its screen RAM intact");
    c128_switch_4080(c);
    CHECK(c->col_mode_80 && c->display.vdc_active && !c->mem.mmu.col4080,
          "F10 selects the VDC output");
    CHECK(c->vdc.ram[0] == 0x41 && c->vdc.ram[0x04ff] == 0x42,
          "switching back to VDC leaves its screen RAM intact");
    CHECK(c->vdc.screen_adr == 0x1000 && c->vdc.chargen_adr == 0x6000 &&
          c->vdc.bytes_per_char == 16,
          "switching back to VDC leaves its display registers intact");

    /* VICE keeps the VDC host ports registered in C64 mode. Elite128 uses
     * this exact R18/R19 round trip to distinguish a C128 from a stock C64. */
    vdc_init(&c->vdc);
    c->mem.mmu.c64_mode = true;
    test_cpu_clock = 100;
    c128_vdc_port_write(c, 0xD600, 18);
    c128_vdc_port_write(c, 0xD601, 0x40);
    c128_vdc_port_write(c, 0xD600, 19);
    c128_vdc_port_write(c, 0xD601, 0x80);
    c128_vdc_port_write(c, 0xD600, 18);
    CHECK(c128_vdc_port_read(c, 0xD601) == 0x40,
          "C64 personality retains access to the C128 VDC ports");

    /* The ML monitor's scheduler state keeps breakpoints separated by CPU
     * and refuses to step a processor which does not own the shared bus. */
    c->mem.mmu.mcr5 = 1;
    c->cpu.pc = 0x1234;
    c->z80.pc = 0x5678;
    unsigned bp8502 = c128_debug_breakpoint_add(c, C128_DEBUG_CPU_8502, 0x2000);
    unsigned bpz80 = c128_debug_breakpoint_add(c, C128_DEBUG_CPU_Z80, 0x2000);
    CHECK(bp8502 && bpz80 && bp8502 != bpz80,
          "same address has independent 8502 and Z80 breakpoints");
    CHECK(c128_debug_breakpoint_enable(c, bpz80, false),
          "Z80 breakpoint can be disabled independently");
    CHECK(c128_debug_breakpoint_remove(c, bp8502),
          "8502 breakpoint can be removed by id");
    c->paused = true;
    CHECK(!c128_debug_step(c, C128_DEBUG_CPU_Z80),
          "inactive Z80 cannot be stepped while 8502 owns bus");
    CHECK(c128_debug_step(c, C128_DEBUG_CPU_8502) && c->debug.step_pending,
          "active 8502 schedules exactly one instruction");

    free(c->vdc.fb);
    free(c);
    if (!failures) puts("test-c128-keyboard: OK");
    return failures ? 1 : 0;
}
