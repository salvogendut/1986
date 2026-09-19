#include "paste.h"
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

void paste_init(Paste *p) {
    p->buf = NULL;
    p->len = p->pos = 0;
    p->timer = 0;
    p->held = false;
    p->last_scancode = -1;
}

void paste_free(Paste *p) {
    free(p->buf);
    p->buf = NULL;
    p->len = p->pos = 0;
}

void paste_text(Paste *p, const char *text) {
    paste_free(p);
    if (!text) return;
    size_t n = strlen(text);
    p->buf = malloc(n + 2);           /* room for appended newline */
    if (!p->buf) return;
    memcpy(p->buf, text, n);
    p->buf[n] = '\n';
    p->buf[n + 1] = '\0';
    p->len = (int)n + 1;
    p->pos = 0;
    p->timer = 0;
    p->held = false;
}

/* Character -> SDL scancode (a subset covering the printable ASCII we can
 * inject). Returns -1 for characters with no simple matrix position. */
static int char_to_scancode(char c) {
    if (c >= 'a' && c <= 'z') return SDL_SCANCODE_A + (c - 'a');
    if (c >= 'A' && c <= 'Z') return SDL_SCANCODE_A + (c - 'A'); /* no shift handling yet */
    if (c >= '0' && c <= '9') return SDL_SCANCODE_0 + (c - '0');
    if (c == ' ') return SDL_SCANCODE_SPACE;
    if (c == '\n' || c == '\r') return SDL_SCANCODE_RETURN;
    if (c == '\t') return SDL_SCANCODE_TAB;
    if (c == 0x08) return SDL_SCANCODE_BACKSPACE;
    return -1;
}

void paste_tick(Paste *p, Kbd *k) {
    if (!p->buf || p->pos >= p->len) return;

    /* Release the previous key before pressing the next. */
    if (p->held) {
        p->held = false;
        if (p->last_scancode >= 0) {
            int row, col;
            if (kbd_map_scancode(p->last_scancode, &row, &col))
                kbd_set(k, row, col, false);
        }
        p->timer = 1;
        return;
    }

    if (p->timer > 0) { p->timer--; return; }

    char c = p->buf[p->pos++];
    int sc = char_to_scancode(c);
    p->last_scancode = sc;
    if (sc >= 0) {
        int row, col;
        if (kbd_map_scancode(sc, &row, &col)) {
            kbd_set(k, row, col, true);
            p->held = true;
        }
    }
    p->timer = 2;
}
