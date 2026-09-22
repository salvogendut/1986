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

static bool pressed(const Kbd *kbd, int row, int col) {
    return (kbd_matrix(kbd, row) & (1u << col)) == 0;
}

int main(void) {
    C128 *c = calloc(1, sizeof(*c));
    CHECK(c != NULL, "allocate keyboard test machine");
    if (!c) return 1;
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

    c128_key_event(c, SDL_SCANCODE_CAPSLOCK, true);
    CHECK(pressed(&c->kbd, KBD_SHIFT_ROW, KBD_SHIFT_COL) &&
          pressed(&c->kbd, 7, 5),
          "CapsLock convenience key sends native Shift+C= chord");
    c128_key_event(c, SDL_SCANCODE_CAPSLOCK, false);
    CHECK(!pressed(&c->kbd, KBD_SHIFT_ROW, KBD_SHIFT_COL) &&
          !pressed(&c->kbd, 7, 5), "CapsLock chord releases cleanly");

    c128_key_event(c, SDL_SCANCODE_ESCAPE, true);
    c128_key_event(c, SDL_SCANCODE_PAGEUP, true);
    CHECK(pressed(&c->kbd, 7, 7) && c->restore_down,
          "Escape+PageUp supplies RUN/STOP and RESTORE NMI");
    c128_key_event(c, SDL_SCANCODE_PAGEUP, false);
    c128_key_event(c, SDL_SCANCODE_ESCAPE, false);
    CHECK(!pressed(&c->kbd, 7, 7) && !c->restore_down,
          "RUN/STOP and RESTORE release cleanly");

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

    free(c);
    if (!failures) puts("test-c128-keyboard: OK");
    return failures ? 1 : 0;
}
