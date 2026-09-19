#include "monitor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct Monitor {
    C128        *c;
    SDL_Window  *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    u32          pixels[320 * 200];
    bool         open;
};

Monitor *monitor_create(C128 *c) {
    Monitor *m = calloc(1, sizeof(Monitor));
    if (!m) return NULL;
    m->c = c;
    return m;
}

void monitor_destroy(Monitor *m) {
    if (!m) return;
    if (m->texture)  SDL_DestroyTexture(m->texture);
    if (m->renderer) SDL_DestroyRenderer(m->renderer);
    if (m->window)   SDL_DestroyWindow(m->window);
    free(m);
}

void monitor_open(Monitor *m) {
    if (m->open) return;
    m->window = SDL_CreateWindow("1986 Monitor", 640, 400, 0);
    if (!m->window) return;
    m->renderer = SDL_CreateRenderer(m->window, NULL);
    if (!m->renderer) return;
    m->texture = SDL_CreateTexture(m->renderer, SDL_PIXELFORMAT_XRGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, 320, 200);
    m->open = true;
}

bool monitor_is_open(const Monitor *m) {
    return m && m->open;
}

bool monitor_handle_event(Monitor *m, SDL_Event *e) {
    if (!m->open) return false;
    if (e->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
        e->window.windowID == SDL_GetWindowID(m->window)) {
        m->open = false;
        SDL_DestroyTexture(m->texture); m->texture = NULL;
        SDL_DestroyRenderer(m->renderer); m->renderer = NULL;
        SDL_DestroyWindow(m->window); m->window = NULL;
        return true;
    }
    return false;
}

void monitor_render(Monitor *m) {
    if (!m->open) return;

    /* Fill the register panel. */
    for (int i = 0; i < 320 * 200; i++) m->pixels[i] = 0x101020;
    const Cpu8502 *cpu = &m->c->cpu;
    char lines[16][64];
    int n = 0;
    snprintf(lines[n++], 64, "PC=%04X  SP=%02X", cpu->pc, cpu->sp);
    snprintf(lines[n++], 64, "A=%02X  X=%02X  Y=%02X", cpu->a, cpu->x, cpu->y);
    snprintf(lines[n++], 64, "P=%02X  cycles=%llu", cpu->p,
             (unsigned long long)cpu->cycles);
    snprintf(lines[n++], 64, "IRQ=%d  NMI=%d", cpu->irq_level, cpu->nmi_level);
    snprintf(lines[n++], 64, "Z80 PC=%04X  cycles=%llu", m->c->z80.pc,
             (unsigned long long)m->c->z80.cycles);

    int y = 8;
    for (int i = 0; i < n; i++) {
        const char *s = lines[i];
        for (int j = 0; s[j]; j++) {
            int cx = 8 + j * 8;
            int cy = y + i * 16;
            for (int yy = 0; yy < 12; yy++) {
                for (int xx = 0; xx < 7; xx++) {
                    if (cx + xx < 320 && cy + yy < 200)
                        m->pixels[(cy + yy) * 320 + (cx + xx)] = 0xC0C0C0;
                }
            }
        }
    }

    SDL_UpdateTexture(m->texture, NULL, m->pixels, 320 * sizeof(u32));
    SDL_SetRenderDrawColor(m->renderer, 0, 0, 0, 255);
    SDL_RenderClear(m->renderer);
    SDL_RenderTexture(m->renderer, m->texture, NULL, NULL);
    SDL_RenderPresent(m->renderer);
}

SDL_WindowID monitor_window_id(const Monitor *m) {
    if (!m->window) return 0;
    return SDL_GetWindowID(m->window);
}
