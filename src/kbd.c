#include "kbd.h"
#include <string.h>
#include <SDL3/SDL.h>

void kbd_init(Kbd *k) {
    memset(k, 0, sizeof(*k));
    kbd_reset(k);
}

void kbd_reset(Kbd *k) {
    memset(k->matrix, 0xFF, sizeof(k->matrix));   /* active-low: all released */
    k->shift = k->ctrl = k->alt = k->caps_lock = false;
}

void kbd_set(Kbd *k, int row, int col, bool down) {
    if (row < 0 || row >= KBD_ROWS || col < 0 || col >= KBD_COLS) return;
    if (down) k->matrix[row] &= (u8)~(1u << col);
    else      k->matrix[row] |= (u8)(1u << col);
}

u8 kbd_matrix(const Kbd *k, int row) {
    if (row < 0 || row >= KBD_ROWS) return 0xFF;
    return k->matrix[row];
}

/*
 * Map an SDL scancode onto the C128 keyboard matrix (8 rows x 8 cols).
 *
 * Row/column positions match VICE's C128 sdl_pos.vkm exactly. The KERNAL's
 * SCNKEY converts the scanned (row,col) into the correct screen code, so the
 * positions must be exactly right. Rows are driven by CIA1 port A (active
 * low), columns are read on CIA1 port B (active low).
 */
bool kbd_map_scancode(int scancode, int *row, int *col, bool *shift) {
    *shift = false;
    /* Letters */
    switch (scancode) {
        case SDL_SCANCODE_A: *row = 1; *col = 2; return true;
        case SDL_SCANCODE_B: *row = 3; *col = 4; return true;
        case SDL_SCANCODE_C: *row = 2; *col = 4; return true;
        case SDL_SCANCODE_D: *row = 2; *col = 2; return true;
        case SDL_SCANCODE_E: *row = 1; *col = 6; return true;
        case SDL_SCANCODE_F: *row = 2; *col = 5; return true;
        case SDL_SCANCODE_G: *row = 3; *col = 2; return true;
        case SDL_SCANCODE_H: *row = 3; *col = 5; return true;
        case SDL_SCANCODE_I: *row = 4; *col = 1; return true;
        case SDL_SCANCODE_J: *row = 4; *col = 2; return true;
        case SDL_SCANCODE_K: *row = 4; *col = 5; return true;
        case SDL_SCANCODE_L: *row = 5; *col = 2; return true;
        case SDL_SCANCODE_M: *row = 4; *col = 4; return true;
        case SDL_SCANCODE_N: *row = 4; *col = 7; return true;
        case SDL_SCANCODE_O: *row = 4; *col = 6; return true;
        case SDL_SCANCODE_P: *row = 5; *col = 1; return true;
        case SDL_SCANCODE_Q: *row = 7; *col = 6; return true;
        case SDL_SCANCODE_R: *row = 2; *col = 1; return true;
        case SDL_SCANCODE_S: *row = 1; *col = 5; return true;
        case SDL_SCANCODE_T: *row = 2; *col = 6; return true;
        case SDL_SCANCODE_U: *row = 3; *col = 6; return true;
        case SDL_SCANCODE_V: *row = 3; *col = 7; return true;
        case SDL_SCANCODE_W: *row = 1; *col = 1; return true;
        case SDL_SCANCODE_X: *row = 2; *col = 7; return true;
        case SDL_SCANCODE_Y: *row = 3; *col = 1; return true;
        case SDL_SCANCODE_Z: *row = 1; *col = 4; return true;
        default: break;
    }

    /* Digits */
    switch (scancode) {
        case SDL_SCANCODE_0: *row = 4; *col = 3; return true;
        case SDL_SCANCODE_1: *row = 7; *col = 0; return true;
        case SDL_SCANCODE_2: *row = 7; *col = 3; return true;
        case SDL_SCANCODE_3: *row = 1; *col = 0; return true;
        case SDL_SCANCODE_4: *row = 1; *col = 3; return true;
        case SDL_SCANCODE_5: *row = 2; *col = 0; return true;
        case SDL_SCANCODE_6: *row = 2; *col = 3; return true;
        case SDL_SCANCODE_7: *row = 3; *col = 0; return true;
        case SDL_SCANCODE_8: *row = 3; *col = 3; return true;
        case SDL_SCANCODE_9: *row = 4; *col = 0; return true;
        default: break;
    }

    /* Punctuation / symbols */
    switch (scancode) {
        case SDL_SCANCODE_MINUS:     *row = 5; *col = 0; return true;    /* + */
        case SDL_SCANCODE_EQUALS:    *row = 5; *col = 3; return true;    /* - */
        case SDL_SCANCODE_LEFTBRACKET:  *row = 5; *col = 6; return true; /* @ */
        case SDL_SCANCODE_RIGHTBRACKET: *row = 6; *col = 1; return true; /* * */
        case SDL_SCANCODE_SEMICOLON:    *row = 5; *col = 5; return true; /* : */
        case SDL_SCANCODE_APOSTROPHE:   *row = 6; *col = 2; return true; /* ; */
        case SDL_SCANCODE_BACKSLASH:    *row = 6; *col = 5; return true; /* = */
        case SDL_SCANCODE_SLASH:        *row = 6; *col = 7; return true;
        case SDL_SCANCODE_COMMA:        *row = 5; *col = 7; return true;
        case SDL_SCANCODE_PERIOD:       *row = 5; *col = 4; return true;
        case SDL_SCANCODE_GRAVE:        *row = 7; *col = 1; return true; /* Left Arrow */
        default: break;
    }

    /* Special keys */
    switch (scancode) {
        case SDL_SCANCODE_RETURN:   *row = 0; *col = 1; return true;
        case SDL_SCANCODE_SPACE:    *row = 7; *col = 4; return true;
        case SDL_SCANCODE_BACKSPACE: *row = 0; *col = 0; return true;   /* DEL */
        case SDL_SCANCODE_LSHIFT:   *row = 1; *col = 7; return true;
        case SDL_SCANCODE_RSHIFT:   *row = 6; *col = 4; return true;
        case SDL_SCANCODE_LCTRL:    *row = 7; *col = 2; return true;    /* CONTROL */
        case SDL_SCANCODE_RCTRL:    *row = 7; *col = 2; return true;    /* CONTROL */
        case SDL_SCANCODE_LALT:     *row = 7; *col = 5; return true;    /* Commodore (CBM) */
        case SDL_SCANCODE_RALT:     *row = 7; *col = 5; return true;    /* Commodore (CBM) */
        case SDL_SCANCODE_ESCAPE:   *row = 7; *col = 7; return true;    /* RUN/STOP */
        case SDL_SCANCODE_F1:       *row = 0; *col = 4; return true;    /* C128 F1 */
        case SDL_SCANCODE_F2:       *row = 0; *col = 4; *shift = true; return true;    /* C128 F2 (shifted F1) */
        case SDL_SCANCODE_F3:       *row = 0; *col = 5; return true;    /* C128 F3 */
        case SDL_SCANCODE_F4:       *row = 0; *col = 5; *shift = true; return true;    /* C128 F4 */
        case SDL_SCANCODE_F5:       *row = 0; *col = 6; return true;    /* C128 F5 */
        case SDL_SCANCODE_F6:       *row = 0; *col = 6; *shift = true; return true;    /* C128 F6 */
        case SDL_SCANCODE_F7:       *row = 0; *col = 3; return true;    /* C128 F7 */
        case SDL_SCANCODE_F8:       *row = 0; *col = 3; *shift = true; return true;    /* C128 F8 */
        case SDL_SCANCODE_UP:       *row = 0; *col = 7; *shift = true; return true;    /* cursor up (shifted Down) */
        case SDL_SCANCODE_DOWN:     *row = 0; *col = 7; return true;                   /* cursor down */
        case SDL_SCANCODE_LEFT:     *row = 0; *col = 2; *shift = true; return true;    /* cursor left (shifted Right) */
        case SDL_SCANCODE_RIGHT:    *row = 0; *col = 2; return true;                   /* cursor right */
        case SDL_SCANCODE_HOME:     *row = 6; *col = 3; return true;    /* CLR/HOME */
        default: return false;
    }
}
