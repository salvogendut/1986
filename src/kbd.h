#pragma once
#include "types.h"
#include <stdbool.h>

/*
 * C128 keyboard matrix.
 *
 * The C128 uses the C64-compatible 8x8 scan matrix plus three extra rows.
 * CIA1 port A selects the first eight rows; VIC-IIe $D02F selects rows 8-10.
 * All eleven rows are sensed through CIA1 port B.
 */

#define KBD_BASE_ROWS 8
#define KBD_EXT_ROWS 3
#define KBD_ROWS (KBD_BASE_ROWS + KBD_EXT_ROWS)
#define KBD_COLS 8

/* Matrix position of the Shift key used for keys whose C128 native form
 * requires Shift. Uses the RIGHT shift so it doesn't clash with a held LEFT
 * shift when the user presses an arrow key. */
#define KBD_SHIFT_ROW 6
#define KBD_SHIFT_COL 4
/* LEFT Shift matrix position (the PC Shift key). */
#define KBD_LSHIFT_ROW 1
#define KBD_LSHIFT_COL 7

typedef struct {
    u8 matrix[KBD_ROWS];   /* active-low: cleared bit = pressed key */
    bool shift;
    bool ctrl;
    bool alt;              /* C128 = the Commodore key (set by RAlt?) */
    bool caps_lock;
    bool caps_key_down;    /* suppress host key-repeat toggles */
} Kbd;

void kbd_init(Kbd *k);
void kbd_reset(Kbd *k);
void kbd_set(Kbd *k, int row, int col, bool down);
u8   kbd_matrix(const Kbd *k, int row);

/* Map an SDL scancode to a matrix position. Returns false if the key has no
 * C128 equivalent (kept out of the matrix). If the key's C128 native form
 * needs Shift (e.g. the Up/Left cursor keys, which are the Shifted Down/Right
 * keys), *shift is set to true. */
bool kbd_map_scancode(int scancode, int *row, int *col, bool *shift);
