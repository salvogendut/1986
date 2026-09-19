#pragma once
#include <stdbool.h>
#include "kbd.h"

typedef struct {
    char *buf;      /* heap-allocated clipboard text */
    int   len;
    int   pos;      /* index of next character to inject */
    int   timer;    /* frames to wait before next action */
    bool  held;     /* true while the current key is pressed */
    int   last_scancode;
} Paste;

void paste_init(Paste *p);
void paste_free(Paste *p);

/* Queue text for character-by-character injection into the C128 keyboard.
 * Auto-appends a newline so single-shot commands fire on their own. */
void paste_text(Paste *p, const char *text);

/* Call once per frame before c128_frame(); injects one key at a time. */
void paste_tick(Paste *p, Kbd *k);
