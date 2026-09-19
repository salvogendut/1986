#include "paste.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Shift key position in the C128 matrix (left shift). The KERNAL treats left
 * and right shift identically for character generation. */
#define SHIFT_ROW 1
#define SHIFT_COL 7

/* Timing: frames at 50 Hz. */
#define HOLD_FRAMES 2   /* how long a key is held */
#define GAP_FRAMES  1   /* silent gap between characters */

/* Optional per-char gap multiplier read from C128_PASTE_GAP at start of the
 * paste, e.g. C128_PASTE_GAP=40 holds a key for 2 frames and gap for 40 so
 * text trickles in slowly enough to survive the KERNAL draining its keyboard
 * buffer between keystrokes. 0 means "use the defaults". */
static int paste_gap_override = -1;

typedef struct {
    int  row, col;
    bool shift;
    bool valid;
} C128Key;

/*
 * Printable ASCII 0-127 -> C128 keyboard matrix position, derived from VICE's
 * data/C128/sdl_pos.vkm. Unset entries have valid=false and are skipped.
 *
 * The C128 boots to uppercase/graphics mode, so the unshifted letter key
 * produces the uppercase letter. Both 'a'-'z' and 'A'-'Z' therefore map to
 * the unshifted key: pasted lowercase text is uppercased exactly as if it had
 * been typed on the physical keyboard in the default mode.
 */
static const C128Key keymap[128] = {
    ['\n'] = { 0, 1, false, true },   /* Return */
    ['\b'] = { 0, 0, false, true },   /* DEL (backspace) */
    [' ']  = { 7, 4, false, true },

    /* Digits — unshifted */
    ['0'] = { 4, 3, false, true },
    ['1'] = { 7, 0, false, true },
    ['2'] = { 7, 3, false, true },
    ['3'] = { 1, 0, false, true },
    ['4'] = { 1, 3, false, true },
    ['5'] = { 2, 0, false, true },
    ['6'] = { 2, 3, false, true },
    ['7'] = { 3, 0, false, true },
    ['8'] = { 3, 3, false, true },
    ['9'] = { 4, 0, false, true },

    /* Letters — unshifted (upper and lower case both map to the same key) */
    ['a'] = { 1, 2, false, true }, ['A'] = { 1, 2, false, true },
    ['b'] = { 3, 4, false, true }, ['B'] = { 3, 4, false, true },
    ['c'] = { 2, 4, false, true }, ['C'] = { 2, 4, false, true },
    ['d'] = { 2, 2, false, true }, ['D'] = { 2, 2, false, true },
    ['e'] = { 1, 6, false, true }, ['E'] = { 1, 6, false, true },
    ['f'] = { 2, 5, false, true }, ['F'] = { 2, 5, false, true },
    ['g'] = { 3, 2, false, true }, ['G'] = { 3, 2, false, true },
    ['h'] = { 3, 5, false, true }, ['H'] = { 3, 5, false, true },
    ['i'] = { 4, 1, false, true }, ['I'] = { 4, 1, false, true },
    ['j'] = { 4, 2, false, true }, ['J'] = { 4, 2, false, true },
    ['k'] = { 4, 5, false, true }, ['K'] = { 4, 5, false, true },
    ['l'] = { 5, 2, false, true }, ['L'] = { 5, 2, false, true },
    ['m'] = { 4, 4, false, true }, ['M'] = { 4, 4, false, true },
    ['n'] = { 4, 7, false, true }, ['N'] = { 4, 7, false, true },
    ['o'] = { 4, 6, false, true }, ['O'] = { 4, 6, false, true },
    ['p'] = { 5, 1, false, true }, ['P'] = { 5, 1, false, true },
    ['q'] = { 7, 6, false, true }, ['Q'] = { 7, 6, false, true },
    ['r'] = { 2, 1, false, true }, ['R'] = { 2, 1, false, true },
    ['s'] = { 1, 5, false, true }, ['S'] = { 1, 5, false, true },
    ['t'] = { 2, 6, false, true }, ['T'] = { 2, 6, false, true },
    ['u'] = { 3, 6, false, true }, ['U'] = { 3, 6, false, true },
    ['v'] = { 3, 7, false, true }, ['V'] = { 3, 7, false, true },
    ['w'] = { 1, 1, false, true }, ['W'] = { 1, 1, false, true },
    ['x'] = { 2, 7, false, true }, ['X'] = { 2, 7, false, true },
    ['y'] = { 3, 1, false, true }, ['Y'] = { 3, 1, false, true },
    ['z'] = { 1, 4, false, true }, ['Z'] = { 1, 4, false, true },

    /* Symbols — unshifted */
    ['+']  = { 5, 0, false, true },
    ['-']  = { 5, 3, false, true },
    ['@']  = { 5, 6, false, true },
    ['*']  = { 6, 1, false, true },
    [':']  = { 5, 5, false, true },
    [';']  = { 6, 2, false, true },
    ['=']  = { 6, 5, false, true },
    [',']  = { 5, 7, false, true },
    ['.']  = { 5, 4, false, true },
    ['/']  = { 6, 7, false, true },

    /* Symbols — require shift */
    ['!']  = { 7, 0, true, true },   /* shift+1 */
    ['"']  = { 7, 3, true, true },   /* shift+2 */
    ['#']  = { 1, 0, true, true },   /* shift+3 */
    ['$']  = { 1, 3, true, true },   /* shift+4 */
    ['%']  = { 2, 0, true, true },   /* shift+5 */
    ['&']  = { 2, 3, true, true },   /* shift+6 */
    ['\''] = { 3, 0, true, true },   /* shift+7 */
    ['(']  = { 3, 3, true, true },   /* shift+8 */
    [')']  = { 4, 0, true, true },   /* shift+9 */
    ['[']  = { 5, 5, true, true },   /* shift+: */
    [']']  = { 6, 2, true, true },   /* shift+; */
    ['<']  = { 5, 7, true, true },   /* shift+, */
    ['>']  = { 5, 4, true, true },   /* shift+. */
    ['?']  = { 6, 7, true, true },   /* shift+/ */
};

static void key_down(Kbd *k, const C128Key *ck) {
    if (ck->shift) kbd_set(k, SHIFT_ROW, SHIFT_COL, true);
    kbd_set(k, ck->row, ck->col, true);
}

static void key_up(Kbd *k, const C128Key *ck) {
    kbd_set(k, ck->row, ck->col, false);
    if (ck->shift) kbd_set(k, SHIFT_ROW, SHIFT_COL, false);
}

void paste_init(Paste *p) {
    p->buf   = NULL;
    p->len   = 0;
    p->pos   = 0;
    p->timer = 0;
    p->held  = false;
}

void paste_free(Paste *p) {
    free(p->buf);
    p->buf = NULL;
    p->len = p->pos = 0;
}

void paste_text(Paste *p, const char *text) {
    free(p->buf);
    p->len = (int)strlen(text);
    p->buf = malloc(p->len + 2);  /* +1 for appended newline, +1 for NUL */
    if (!p->buf) { p->len = 0; return; }
    memcpy(p->buf, text, p->len);
    p->buf[p->len++] = '\n';
    p->buf[p->len]   = '\0';
    p->pos   = 0;
    p->timer = 3;   /* wait for the Ctrl (CBM) key to clear from the matrix */
    p->held  = false;
}

void paste_text_raw(Paste *p, const char *text) {
    free(p->buf);
    p->len = (int)strlen(text);
    p->buf = malloc(p->len + 1);
    if (!p->buf) { p->len = 0; return; }
    memcpy(p->buf, text, p->len);
    p->buf[p->len] = '\0';
    p->pos   = 0;
    p->timer = 3;
    p->held  = false;
}

void paste_tick(Paste *p, Kbd *k) {
    if (!p->buf || p->pos >= p->len) return;

    if (p->timer > 0) { p->timer--; return; }

    /* Skip \r and unmapped characters. */
    while (p->pos < p->len) {
        unsigned char c = (unsigned char)p->buf[p->pos];
        if (c == '\r') { p->pos++; continue; }
        if (c < 128 && keymap[c].valid) break;
        p->pos++;
    }
    if (p->pos >= p->len) return;

    const C128Key *ck = &keymap[(unsigned char)p->buf[p->pos]];

    if (!p->held) {
        key_down(k, ck);
        p->held  = true;
        p->timer = HOLD_FRAMES;
    } else {
        key_up(k, ck);
        p->held  = false;
        p->pos++;
        if (paste_gap_override < 0) {
            const char *e = getenv("C128_PASTE_GAP");
            paste_gap_override = e ? atoi(e) : 0;
        }
        p->timer = paste_gap_override > 0 ? paste_gap_override : GAP_FRAMES;
    }
}
