#include "monitor.h"
#include "mos6502dis.h"
#include "z80dis.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MON_COLS 80
#define MON_ROWS 25
#define OUT_ROWS 23
#define CHAR_W 8
#define CHAR_H 8
#define SCALE_X 1.5f
#define SCALE_Y 3.6f
#define WIN_W ((int)(MON_COLS * CHAR_W * SCALE_X))
#define WIN_H ((int)(MON_ROWS * CHAR_H * SCALE_Y))

typedef enum { PAGE_NONE, PAGE_DIS, PAGE_MEM } PageMode;

struct Monitor {
    C128 *c;
    SDL_Window *window;
    SDL_Renderer *renderer;
    bool open;
    C128DebugCpu cpu;
    char screen[OUT_ROWS][MON_COLS + 1];
    char input[MON_COLS + 1];
    int input_len;
    PageMode page;
    int page_lines;
    u8 snapshot[65536];
    u16 dis_address, dis_end;
    int dis_lines;
    bool dis_has_end, dis_active;
    u16 mem_address, mem_end;
    bool mem_active;
};

static const char *cpu_name(C128DebugCpu cpu) {
    return cpu == C128_DEBUG_CPU_Z80 ? "Z80" : "8502";
}

static void screen_scroll(Monitor *m) {
    memmove(m->screen[0], m->screen[1],
            (OUT_ROWS - 1) * (MON_COLS + 1));
    memset(m->screen[OUT_ROWS - 1], ' ', MON_COLS);
    m->screen[OUT_ROWS - 1][MON_COLS] = '\0';
}

static void screen_puts(Monitor *m, const char *s) {
    screen_scroll(m);
    size_t n = strlen(s);
    if (n > MON_COLS) n = MON_COLS;
    memcpy(m->screen[OUT_ROWS - 1], s, n);
}

static void screen_printf(Monitor *m, const char *fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    screen_puts(m, line);
}

static void take_snapshot(Monitor *m) {
    for (unsigned i = 0; i < 65536; ++i)
        m->snapshot[i] = c128_debug_mem_read(m->c, m->cpu, (u16)i);
}

static void show_registers(Monitor *m) {
    const char *owner = cpu_name(c128_debug_owner(m->c));
    if (m->cpu == C128_DEBUG_CPU_8502) {
        const Cpu8502 *c = &m->c->cpu;
        screen_printf(m, "8502%s PC:%04X A:%02X X:%02X Y:%02X SP:%02X P:%02X",
                      c128_debug_owner(m->c) == m->cpu ? "*" : " ",
                      c->pc, c->a, c->x, c->y, c->sp, c->p);
        screen_printf(m, " flags:%c%c-%c%c%c%c%c cycles:%llu owner:%s",
                      c->p & P_N ? 'N' : '-', c->p & P_V ? 'V' : '-',
                      c->p & P_B ? 'B' : '-', c->p & P_D ? 'D' : '-',
                      c->p & P_I ? 'I' : '-', c->p & P_Z ? 'Z' : '-',
                      c->p & P_C ? 'C' : '-',
                      (unsigned long long)c->cycles, owner);
    } else {
        const Z80 *z = &m->c->z80;
        screen_printf(m, "Z80%s PC:%04X SP:%04X AF:%04X BC:%04X DE:%04X HL:%04X",
                      c128_debug_owner(m->c) == m->cpu ? "*" : " ",
                      z->pc, z->sp, z->af, z->bc, z->de, z->hl);
        screen_printf(m, " IX:%04X IY:%04X AF':%04X BC':%04X DE':%04X HL':%04X",
                      z->ix, z->iy, z->af_, z->bc_, z->de_, z->hl_);
        screen_printf(m, " I:%02X R:%02X IM:%u IFF:%d/%d HALT:%d owner:%s",
                      z->i, z->r, z->im, z->iff1, z->iff2, z->halted, owner);
    }
}

static bool passed_end(u16 start, u16 current, u16 end) {
    if (start <= end) return current > end || current < start;
    return current > end && current < start;
}

static bool dis_emit(Monitor *m) {
    if (!m->dis_active) return true;
    u16 address = m->dis_address;
    char text[64];
    int bytes = m->cpu == C128_DEBUG_CPU_Z80
        ? z80dis(m->snapshot, address, text, sizeof(text))
        : mos6502dis(m->snapshot, address, text, sizeof(text));
    if (bytes < 1) bytes = 1;
    char raw[20] = "";
    for (int i = 0; i < bytes && i < 4; ++i) {
        char byte[4];
        snprintf(byte, sizeof(byte), "%02X ", m->snapshot[(u16)(address + i)]);
        strncat(raw, byte, sizeof(raw) - strlen(raw) - 1);
    }
    screen_printf(m, "%s:%04X  %-12s %s", cpu_name(m->cpu), address, raw, text);
    m->dis_address = (u16)(address + bytes);
    if (m->dis_has_end) {
        if (passed_end(address, m->dis_address, m->dis_end)) m->dis_active = false;
    } else if (--m->dis_lines <= 0) {
        m->dis_active = false;
    }
    if (++m->page_lines >= OUT_ROWS - 1 && m->dis_active) return false;
    return true;
}

static void dis_page(Monitor *m) {
    m->page_lines = 0;
    while (m->dis_active && dis_emit(m)) {}
    m->page = m->dis_active ? PAGE_DIS : PAGE_NONE;
}

static bool mem_emit(Monitor *m) {
    if (!m->mem_active) return true;
    u16 address = m->mem_address;
    char hex[16 * 3 + 1] = "";
    char ascii[17];
    int count = 16;
    if (address <= m->mem_end && (unsigned)address + 15u > m->mem_end)
        count = m->mem_end - address + 1;
    for (int i = 0; i < 16; ++i) {
        char byte[4];
        if (i < count) snprintf(byte, sizeof(byte), "%02X ", m->snapshot[(u16)(address + i)]);
        else strcpy(byte, "   ");
        strncat(hex, byte, sizeof(hex) - strlen(hex) - 1);
        u8 b = i < count ? m->snapshot[(u16)(address + i)] : ' ';
        ascii[i] = b >= 0x20 && b < 0x7f ? (char)b : '.';
    }
    ascii[16] = '\0';
    screen_printf(m, "%04X: %s %s", address, hex, ascii);
    u16 next = (u16)(address + 16);
    m->mem_address = next;
    if (passed_end(address, next, m->mem_end)) m->mem_active = false;
    if (++m->page_lines >= OUT_ROWS - 1 && m->mem_active) return false;
    return true;
}

static void mem_page(Monitor *m) {
    m->page_lines = 0;
    while (m->mem_active && mem_emit(m)) {}
    m->page = m->mem_active ? PAGE_MEM : PAGE_NONE;
}

static bool parse_cpu(const char *s, C128DebugCpu *cpu) {
    if (!strcasecmp(s, "8502") || !strcasecmp(s, "6502")) {
        *cpu = C128_DEBUG_CPU_8502; return true;
    }
    if (!strcasecmp(s, "Z80")) {
        *cpu = C128_DEBUG_CPU_Z80; return true;
    }
    return false;
}

static void show_help(Monitor *m) {
    screen_puts(m, "CPU [8502|Z80|OWNER] select processor context (TAB also switches)");
    screen_puts(m, "R                     show selected CPU registers");
    screen_puts(m, "D [addr [end]]        disassemble selected CPU address space");
    screen_puts(m, "M <addr> [end]        dump selected CPU memory (MMU-visible)");
    screen_puts(m, "E <addr> <bytes...>   edit selected CPU memory");
    screen_puts(m, "B [addr]              set/list CPU-tagged breakpoints");
    screen_puts(m, "BE|BD|BC <id>         enable/disable/clear breakpoint");
    screen_puts(m, "P / N / G             pause / step owner / continue");
    screen_puts(m, "MMU                   show ownership and MMU registers");
    screen_puts(m, "H or ? / X            help / close monitor");
}

static void execute(Monitor *m, const char *raw) {
    screen_printf(m, "> %s", raw);
    while (isspace((unsigned char)*raw)) ++raw;
    if (!*raw) return;
    char cmd[16];
    int ci = 0;
    while (*raw && !isspace((unsigned char)*raw) && ci < (int)sizeof(cmd) - 1)
        cmd[ci++] = (char)toupper((unsigned char)*raw++);
    cmd[ci] = '\0';
    while (isspace((unsigned char)*raw)) ++raw;
    const char *args = raw;

    if (!strcmp(cmd, "CPU")) {
        char name[16] = "";
        sscanf(args, "%15s", name);
        if (!name[0]) {
            screen_printf(m, "Selected:%s  bus owner:%s", cpu_name(m->cpu),
                          cpu_name(c128_debug_owner(m->c)));
        } else if (!strcasecmp(name, "OWNER")) {
            m->cpu = c128_debug_owner(m->c);
            screen_printf(m, "CPU context is now %s (bus owner)", cpu_name(m->cpu));
        } else if (parse_cpu(name, &m->cpu)) {
            screen_printf(m, "CPU context is now %s%s", cpu_name(m->cpu),
                          c128_debug_owner(m->c) == m->cpu ? " (bus owner)" : " (inactive)");
        } else screen_puts(m, "Usage: CPU 8502|Z80|OWNER");
    } else if (!strcmp(cmd, "R")) {
        show_registers(m);
    } else if (!strcmp(cmd, "D")) {
        unsigned start = c128_debug_pc(m->c, m->cpu), end = 0;
        int n = sscanf(args, "%x %x", &start, &end);
        take_snapshot(m);
        m->dis_address = (u16)start;
        m->dis_end = (u16)end;
        m->dis_has_end = n >= 2;
        m->dis_lines = 10;
        m->dis_active = true;
        dis_page(m);
    } else if (!strcmp(cmd, "M")) {
        unsigned start, end;
        int n = sscanf(args, "%x %x", &start, &end);
        if (n < 1) { screen_puts(m, "Usage: M <addr> [end]"); return; }
        take_snapshot(m);
        m->mem_address = (u16)start;
        m->mem_end = n >= 2 ? (u16)end : (u16)(start + 0x7f);
        m->mem_active = true;
        mem_page(m);
    } else if (!strcmp(cmd, "E")) {
        char copy[256];
        snprintf(copy, sizeof(copy), "%s", args);
        char *tok = strtok(copy, " \t");
        unsigned address;
        if (!tok || sscanf(tok, "%x", &address) != 1) {
            screen_puts(m, "Usage: E <addr> <byte> [byte ...]"); return;
        }
        unsigned count = 0, value;
        while ((tok = strtok(NULL, " \t")) != NULL) {
            if (sscanf(tok, "%x", &value) != 1 || value > 0xff) {
                screen_printf(m, "Invalid byte: %s", tok); return;
            }
            c128_debug_mem_write(m->c, m->cpu, (u16)(address + count), (u8)value);
            ++count;
        }
        if (!count) screen_puts(m, "Usage: E <addr> <byte> [byte ...]");
        else screen_printf(m, "Wrote %u byte%s at %s:%04X", count,
                           count == 1 ? "" : "s", cpu_name(m->cpu), (u16)address);
    } else if (!strcmp(cmd, "B")) {
        if (!*args) {
            bool any = false;
            for (unsigned i = 0; i < C128_DEBUG_MAX_BREAKPOINTS; ++i) {
                const C128DebugBreakpoint *bp = c128_debug_breakpoint_at(m->c, i);
                if (!bp) continue;
                screen_printf(m, " BP%-3u %-4s:%04X %s", bp->id, cpu_name(bp->cpu),
                              bp->address, bp->enabled ? "enabled" : "disabled");
                any = true;
            }
            if (!any) screen_puts(m, "No breakpoints set");
        } else {
            unsigned address;
            if (sscanf(args, "%x", &address) != 1) {
                screen_puts(m, "Usage: B <addr> (uses selected CPU)"); return;
            }
            unsigned id = c128_debug_breakpoint_add(m->c, m->cpu, (u16)address);
            if (id) screen_printf(m, "Breakpoint %u set at %s:%04X", id,
                                  cpu_name(m->cpu), (u16)address);
            else screen_puts(m, "Breakpoint table is full");
        }
    } else if (!strcmp(cmd, "BE") || !strcmp(cmd, "BD")) {
        unsigned id;
        bool enabled = !strcmp(cmd, "BE");
        if (sscanf(args, "%u", &id) == 1 &&
            c128_debug_breakpoint_enable(m->c, id, enabled))
            screen_printf(m, "Breakpoint %u %s", id, enabled ? "enabled" : "disabled");
        else screen_puts(m, "Usage: BE|BD <breakpoint-id>");
    } else if (!strcmp(cmd, "BC")) {
        unsigned id;
        if (sscanf(args, "%u", &id) == 1 && c128_debug_breakpoint_remove(m->c, id))
            screen_printf(m, "Breakpoint %u cleared", id);
        else screen_puts(m, "Usage: BC <breakpoint-id>");
    } else if (!strcmp(cmd, "P")) {
        c128_debug_pause(m->c);
        screen_printf(m, "Paused at %s:%04X", cpu_name(c128_debug_owner(m->c)),
                      c128_debug_pc(m->c, c128_debug_owner(m->c)));
    } else if (!strcmp(cmd, "N")) {
        if (!m->c->paused) screen_puts(m, "Machine is running; use P first");
        else if (!c128_debug_step(m->c, m->cpu))
            screen_printf(m, "%s is inactive; select CPU OWNER to step", cpu_name(m->cpu));
        else screen_printf(m, "Stepping one %s instruction", cpu_name(m->cpu));
    } else if (!strcmp(cmd, "G") || !strcmp(cmd, "GO")) {
        c128_debug_continue(m->c);
        screen_puts(m, "Running");
    } else if (!strcmp(cmd, "MMU")) {
        Mmu *mmu = &m->c->mem.mmu;
        screen_printf(m, "owner:%s MCR(D500):%02X CR(D505):%02X PCR:%02X/%02X/%02X/%02X",
                      cpu_name(c128_debug_owner(m->c)), mmu->mcr, mmu->mcr5,
                      mmu->prefig, mmu->pcr2, mmu->pcr3, mmu->pcr4);
        screen_printf(m, "RAM bank:%u common:%02X mode:%s 40/80:%s",
                      (mmu->mcr >> 6) & 1, mmu->rcr,
                      c128_is_c64_mode(m->c) ? "C64" : "C128",
                      m->c->col_mode_80 ? "80" : "40");
    } else if (!strcmp(cmd, "X") || !strcmp(cmd, "Q")) {
        m->open = false;
        SDL_StopTextInput(m->window);
        SDL_HideWindow(m->window);
    } else {
        show_help(m);
    }
}

static void handle_key(Monitor *m, SDL_Keycode key) {
    if (m->page != PAGE_NONE) {
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
            if (m->page == PAGE_DIS) dis_page(m); else mem_page(m);
        } else if (key == SDLK_ESCAPE) {
            m->dis_active = m->mem_active = false;
            m->page = PAGE_NONE;
        }
        return;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        m->input[m->input_len] = '\0';
        execute(m, m->input);
        m->input_len = 0;
        m->input[0] = '\0';
    } else if (key == SDLK_BACKSPACE) {
        if (m->input_len) m->input[--m->input_len] = '\0';
    } else if (key == SDLK_TAB) {
        m->cpu = m->cpu == C128_DEBUG_CPU_8502
            ? C128_DEBUG_CPU_Z80 : C128_DEBUG_CPU_8502;
        screen_printf(m, "CPU context is now %s%s", cpu_name(m->cpu),
                      c128_debug_owner(m->c) == m->cpu ? " (bus owner)" : " (inactive)");
    } else if (key == SDLK_F7) {
        if (m->c->paused) c128_debug_continue(m->c); else c128_debug_pause(m->c);
    } else if (key == SDLK_F8 || key == SDLK_ESCAPE) {
        m->open = false;
        SDL_StopTextInput(m->window);
        SDL_HideWindow(m->window);
    }
}

static void append_text(Monitor *m, const char *text) {
    size_t n = strlen(text);
    if (m->input_len + (int)n >= MON_COLS - 3) return;
    memcpy(m->input + m->input_len, text, n + 1);
    m->input_len += (int)n;
}

Monitor *monitor_create(C128 *c) {
    Monitor *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->c = c;
    m->cpu = c128_debug_owner(c);
    m->window = SDL_CreateWindow("1986 ML Monitor", WIN_W, WIN_H,
                                 SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (!m->window) { free(m); return NULL; }
    m->renderer = SDL_CreateRenderer(m->window, NULL);
    if (!m->renderer) {
        SDL_DestroyWindow(m->window);
        free(m);
        return NULL;
    }
    for (int row = 0; row < OUT_ROWS; ++row) {
        memset(m->screen[row], ' ', MON_COLS);
        m->screen[row][MON_COLS] = '\0';
    }
    screen_puts(m, "1986 C128 Multiprocessor ML Monitor");
    screen_puts(m, "CPU contexts: MOS 8502 and Z80; '*' marks the current bus owner.");
    screen_puts(m, "Type H for commands. TAB switches CPU context; F7 pauses; F8 closes.");
    show_registers(m);
    return m;
}

void monitor_destroy(Monitor *m) {
    if (!m) return;
    if (m->renderer) SDL_DestroyRenderer(m->renderer);
    if (m->window) SDL_DestroyWindow(m->window);
    free(m);
}

void monitor_open(Monitor *m) {
    if (!m) return;
    m->cpu = c128_debug_owner(m->c);
    m->open = true;
    SDL_ShowWindow(m->window);
    SDL_RaiseWindow(m->window);
    SDL_StartTextInput(m->window);
}

bool monitor_is_open(const Monitor *m) { return m && m->open; }

bool monitor_handle_event(Monitor *m, SDL_Event *e) {
    if (!m || !m->open) return false;
    SDL_WindowID id = SDL_GetWindowID(m->window);
    if (e->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e->window.windowID == id) {
        m->open = false;
        SDL_StopTextInput(m->window);
        SDL_HideWindow(m->window);
        return true;
    }
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.windowID == id) {
        handle_key(m, e->key.key);
        return true;
    }
    if (e->type == SDL_EVENT_TEXT_INPUT && e->text.windowID == id) {
        append_text(m, e->text.text);
        return true;
    }
    return false;
}

void monitor_tick(Monitor *m) {
    if (!m) return;
    C128DebugCpu cpu;
    u16 address;
    C128DebugStopReason reason = c128_debug_take_stop(m->c, &cpu, &address);
    if (reason == C128_DEBUG_STOP_NONE || reason == C128_DEBUG_STOP_PAUSE) return;
    m->cpu = cpu;
    if (reason == C128_DEBUG_STOP_BREAKPOINT)
        screen_printf(m, "*** Breakpoint at %s:%04X ***", cpu_name(cpu), address);
    else
        screen_printf(m, "*** Step complete at %s:%04X ***", cpu_name(cpu), address);
    take_snapshot(m);
    m->dis_address = address;
    m->dis_has_end = false;
    m->dis_lines = 3;
    m->dis_active = true;
    dis_page(m);
    if (!m->open) monitor_open(m);
}

static void draw_status(Monitor *m) {
    char bar[MON_COLS + 1];
    const char *state = m->c->paused ? "PAUSED" : "RUN";
    const char *owner = cpu_name(c128_debug_owner(m->c));
    if (m->cpu == C128_DEBUG_CPU_8502) {
        const Cpu8502 *c = &m->c->cpu;
        snprintf(bar, sizeof(bar),
                 "8502%s PC:%04X A:%02X X:%02X Y:%02X SP:%02X P:%02X OWNER:%s [%s]",
                 c128_debug_owner(m->c) == m->cpu ? "*" : " ", c->pc,
                 c->a, c->x, c->y, c->sp, c->p, owner, state);
    } else {
        const Z80 *z = &m->c->z80;
        snprintf(bar, sizeof(bar),
                 "Z80%s PC:%04X SP:%04X AF:%04X BC:%04X DE:%04X HL:%04X OWNER:%s [%s]",
                 c128_debug_owner(m->c) == m->cpu ? "*" : " ", z->pc,
                 z->sp, z->af, z->bc, z->de, z->hl, owner, state);
    }
    float y = OUT_ROWS * CHAR_H;
    SDL_FRect bg = { 0, y, MON_COLS * CHAR_W, CHAR_H };
    SDL_SetRenderDrawColor(m->renderer, m->c->paused ? 0x33 : 0x00,
                          m->c->paused ? 0x00 : 0x22, 0x00, 255);
    SDL_RenderFillRect(m->renderer, &bg);
    SDL_SetRenderDrawColor(m->renderer, 0xff,
                          m->c->paused ? 0x55 : 0xff,
                          m->c->paused ? 0x55 : 0x44, 255);
    SDL_RenderDebugText(m->renderer, 0, y, bar);
}

void monitor_render(Monitor *m) {
    if (!m || !m->open) return;
    SDL_SetRenderDrawColor(m->renderer, 0, 0, 0, 255);
    SDL_RenderClear(m->renderer);
    SDL_SetRenderScale(m->renderer, SCALE_X, SCALE_Y);
    SDL_SetRenderDrawColor(m->renderer, 0xcc, 0xff, 0xcc, 255);
    for (int row = 0; row < OUT_ROWS; ++row)
        SDL_RenderDebugText(m->renderer, 0, row * CHAR_H, m->screen[row]);
    draw_status(m);
    float y = (OUT_ROWS + 1) * CHAR_H;
    if (m->page != PAGE_NONE) {
        SDL_SetRenderDrawColor(m->renderer, 0xff, 0xff, 0x00, 255);
        SDL_RenderDebugText(m->renderer, 0, y,
                            "-- more -- ENTER/SPACE continues, ESC cancels");
    } else {
        SDL_SetRenderDrawColor(m->renderer, 0x88, 0xff, 0x88, 255);
        SDL_RenderDebugText(m->renderer, 0, y, "> ");
        SDL_RenderDebugText(m->renderer, 2 * CHAR_W, y, m->input);
        SDL_FRect cursor = { (float)((2 + m->input_len) * CHAR_W), y,
                             CHAR_W, CHAR_H };
        SDL_RenderFillRect(m->renderer, &cursor);
    }
    SDL_SetRenderScale(m->renderer, 1.0f, 1.0f);
    SDL_RenderPresent(m->renderer);
}

SDL_WindowID monitor_window_id(const Monitor *m) {
    return m && m->window ? SDL_GetWindowID(m->window) : 0;
}
