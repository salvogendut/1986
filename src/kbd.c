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
 * Map an SDL scancode onto the C64/C128 keyboard matrix (8 rows x 8 cols).
 * The KERNAL's SCNKEY converts the (row,col) to the correct screen code, so
 * these positions must match the C64 matrix exactly. Rows are port A bits
 * (0-7), columns are port B bits (0-7).
 */
bool kbd_map_scancode(int scancode, int *row, int *col) {
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
        case SDL_SCANCODE_1: *row = 6; *col = 7; return true;
        case SDL_SCANCODE_2: *row = 7; *col = 3; return true;
        case SDL_SCANCODE_3: *row = 0; *col = 0; return true;
        case SDL_SCANCODE_4: *row = 1; *col = 3; return true;
        case SDL_SCANCODE_5: *row = 2; *col = 0; return true;
        case SDL_SCANCODE_6: *row = 2; *col = 3; return true;
        case SDL_SCANCODE_7: *row = 3; *col = 0; return true;
        case SDL_SCANCODE_8: *row = 3; *col = 3; return true;
        case SDL_SCANCODE_9: *row = 4; *col = 0; return true;
        case SDL_SCANCODE_KP_0: *row = 4; *col = 3; return true;
        case SDL_SCANCODE_KP_1: *row = 6; *col = 7; return true;
        case SDL_SCANCODE_KP_2: *row = 7; *col = 3; return true;
        case SDL_SCANCODE_KP_3: *row = 0; *col = 0; return true;
        case SDL_SCANCODE_KP_4: *row = 1; *col = 3; return true;
        case SDL_SCANCODE_KP_5: *row = 2; *col = 0; return true;
        case SDL_SCANCODE_KP_6: *row = 2; *col = 3; return true;
        case SDL_SCANCODE_KP_7: *row = 3; *col = 0; return true;
        case SDL_SCANCODE_KP_8: *row = 3; *col = 3; return true;
        case SDL_SCANCODE_KP_9: *row = 4; *col = 0; return true;
        default: break;
    }

    /* Punctuation / symbols */
    switch (scancode) {
        case SDL_SCANCODE_PERIOD:    *row = 5; *col = 4; return true;
        case SDL_SCANCODE_COMMA:     *row = 5; *col = 7; return true;
        case SDL_SCANCODE_SEMICOLON: *row = 6; *col = 1; return true;
        case SDL_SCANCODE_APOSTROPHE: *row = 5; *col = 5; return true;   /* : */
        case SDL_SCANCODE_SLASH:     *row = 6; *col = 6; return true;
        case SDL_SCANCODE_BACKSLASH: *row = 6; *col = 1; return true;    /* ; */
        case SDL_SCANCODE_MINUS:     *row = 5; *col = 3; return true;
        case SDL_SCANCODE_EQUALS:    *row = 6; *col = 4; return true;
        case SDL_SCANCODE_LEFTBRACKET:  *row = 5; *col = 6; return true; /* @ */
        case SDL_SCANCODE_RIGHTBRACKET: *row = 6; *col = 3; return true; /* £ */
        case SDL_SCANCODE_GRAVE:     *row = 5; *col = 0; return true;    /* + */
        case SDL_SCANCODE_KP_PLUS:   *row = 5; *col = 0; return true;    /* + */
        case SDL_SCANCODE_KP_MINUS:  *row = 5; *col = 3; return true;    /* - */
        case SDL_SCANCODE_KP_MULTIPLY: *row = 6; *col = 0; return true;  /* * */
        case SDL_SCANCODE_KP_DIVIDE: *row = 6; *col = 6; return true;    /* / */
        default: break;
    }

    /* Special keys */
    switch (scancode) {
        case SDL_SCANCODE_RETURN:  *row = 0; *col = 1; return true;
        case SDL_SCANCODE_SPACE:   *row = 7; *col = 4; return true;
        case SDL_SCANCODE_BACKSPACE: *row = 0; *col = 0; return true;   /* DEL */
        case SDL_SCANCODE_LSHIFT:  *row = 1; *col = 7; return true;
        case SDL_SCANCODE_RSHIFT:  *row = 6; *col = 2; return true;
        case SDL_SCANCODE_LCTRL:   *row = 7; *col = 2; return true;
        case SDL_SCANCODE_TAB:     *row = 6; *col = 0; return true;     /* CTRL+? placeholder */
        case SDL_SCANCODE_ESCAPE:  *row = 7; *col = 7; return true;     /* RUN/STOP */
        case SDL_SCANCODE_F1:      *row = 0; *col = 4; return true;
        case SDL_SCANCODE_F2:      *row = 0; *col = 4; return true;     /* F1 + shift */
        case SDL_SCANCODE_F3:      *row = 0; *col = 5; return true;
        case SDL_SCANCODE_F4:      *row = 0; *col = 5; return true;
        case SDL_SCANCODE_F5:      *row = 0; *col = 6; return true;
        case SDL_SCANCODE_F6:      *row = 0; *col = 6; return true;
        case SDL_SCANCODE_F7:      *row = 0; *col = 3; return true;
        case SDL_SCANCODE_F8:      *row = 0; *col = 3; return true;
        case SDL_SCANCODE_UP:      *row = 6; *col = 5; return true;
        case SDL_SCANCODE_DOWN:    *row = 0; *col = 7; return true;
        case SDL_SCANCODE_LEFT:    *row = 7; *col = 1; return true;
        case SDL_SCANCODE_RIGHT:   *row = 0; *col = 2; return true;
        case SDL_SCANCODE_HOME:    *row = 7; *col = 0; return true;
        case SDL_SCANCODE_END:     *row = 7; *col = 0; return true;
        default: return false;
    }
}
