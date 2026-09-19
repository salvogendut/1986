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

bool kbd_map_scancode(int scancode, int *row, int *col) {
    /* Positional map to the C128 matrix. This is a scaffold subset that
     * covers the most-used keys; it will grow into the full 8x8 layout. */
    switch (scancode) {
        case SDL_SCANCODE_RETURN: *row = 0; *col = 1; return true;
        case SDL_SCANCODE_SPACE:  *row = 6; *col = 1; return true;
        case SDL_SCANCODE_A:      *row = 4; *col = 2; return true;
        case SDL_SCANCODE_B:      *row = 6; *col = 4; return true;
        case SDL_SCANCODE_C:      *row = 5; *col = 4; return true;
        case SDL_SCANCODE_D:      *row = 4; *col = 3; return true;
        case SDL_SCANCODE_E:      *row = 3; *col = 3; return true;
        case SDL_SCANCODE_F:      *row = 4; *col = 4; return true;
        case SDL_SCANCODE_G:      *row = 4; *col = 5; return true;
        case SDL_SCANCODE_H:      *row = 4; *col = 6; return true;
        case SDL_SCANCODE_I:      *row = 3; *col = 4; return true;
        case SDL_SCANCODE_J:      *row = 4; *col = 7; return true;
        case SDL_SCANCODE_K:      *row = 5; *col = 0; return true;
        case SDL_SCANCODE_L:      *row = 5; *col = 1; return true;
        case SDL_SCANCODE_M:      *row = 6; *col = 5; return true;
        case SDL_SCANCODE_N:      *row = 6; *col = 6; return true;
        case SDL_SCANCODE_O:      *row = 3; *col = 5; return true;
        case SDL_SCANCODE_P:      *row = 3; *col = 6; return true;
        case SDL_SCANCODE_Q:      *row = 3; *col = 1; return true;
        case SDL_SCANCODE_R:      *row = 3; *col = 2; return true;
        case SDL_SCANCODE_S:      *row = 4; *col = 1; return true;
        case SDL_SCANCODE_T:      *row = 3; *col = 7; return true;
        case SDL_SCANCODE_U:      *row = 3; *col = 3; return true;
        case SDL_SCANCODE_V:      *row = 6; *col = 0; return true;
        case SDL_SCANCODE_W:      *row = 3; *col = 0; return true;
        case SDL_SCANCODE_X:      *row = 5; *col = 3; return true;
        case SDL_SCANCODE_Y:      *row = 3; *col = 7; return true;
        case SDL_SCANCODE_Z:      *row = 5; *col = 2; return true;
        default: return false;
    }
}
