#include "c128.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

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

    free(c);
    if (!failures) puts("test-c128-keyboard: OK");
    return failures ? 1 : 0;
}
