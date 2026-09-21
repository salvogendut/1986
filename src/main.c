#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <libgen.h>
#include "config.h"
#include "overlay.h"
#include "c128.h"
#include "paste.h"
#include "monitor.h"
#include "gifcap.h"
#include "notify.h"
#include "leds.h"
#include "compat_win.h"
#include "startup_debug.h"

/* notify.c forward-declares this debug master switch (defined in the
 * machine file in the reference tree); provide it here so the module links. */
int g_debug_enabled = 0;

/* Boot-progress trace enabled with C128_BOOT_TRACE=1. */
static bool g_boot_trace = false;
static bool g_sid_trace = false;

/* One-shot PPM capture (C128_SAVE_PPM=<path>) for visual debugging. */
static char *g_save_ppm = NULL;
static int   g_save_ppm_frame = 60;

/* --- Video capture state (F6). --- */
static GifCap  *g_videocap_gif = NULL;
static uint64_t g_videocap_gif_interval_ns = 0;
static uint64_t g_videocap_gif_elapsed_ns = 0;
static bool     g_videocap_gif_first = false;

static bool videocap_active(void) { return g_videocap_gif != NULL; }

static bool videocap_start(const char *path, int gif_width, int gif_fps) {
    if (!path || !path[0] || videocap_active()) return false;
    if (gif_fps < 1) gif_fps = 25;
    int delay_cs = 100 / gif_fps;
    g_videocap_gif = gifcap_open(path, C128_SCREEN_W, C128_SCREEN_H,
                                 gif_width, (gif_width * 5) / 8, delay_cs);
    if (!g_videocap_gif) {
        fprintf(stderr, "[videocap] GIF open failed for '%s'\n", path);
        return false;
    }
    g_videocap_gif_interval_ns = 1000000000ULL / (uint64_t)gif_fps;
    g_videocap_gif_elapsed_ns = 0;
    g_videocap_gif_first = true;
    fprintf(stderr, "[videocap] recording to %s\n", path);
    return true;
}

static void videocap_stop(void) {
    if (g_videocap_gif) {
        int n = gifcap_frame_count(g_videocap_gif);
        gifcap_close(g_videocap_gif);
        g_videocap_gif = NULL;
        fprintf(stderr, "[videocap] GIF stopped (%d frames)\n", n);
    }
}

static bool videocap_gif_due(uint64_t emulated_frame_ns) {
    if (g_videocap_gif_first) { g_videocap_gif_first = false; return true; }
    g_videocap_gif_elapsed_ns += emulated_frame_ns;
    if (g_videocap_gif_elapsed_ns < g_videocap_gif_interval_ns) return false;
    g_videocap_gif_elapsed_ns %= g_videocap_gif_interval_ns;
    return true;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "1986 — Commodore C128DCR emulator (scaffolding)\n"
        "Usage: %s [options]\n"
        "  --scale N        window scale (1..4, default 2)\n"
        "  --fullscreen     start fullscreen\n"
        "  --fast           run the 8502 at 2 MHz\n"
        "  --rom DIR        directory holding the machine ROM images\n"
        "  --disk PATH      attach a D64, D71, or D81 image at launch\n"
        "  --gif-out PATH   start recording a GIF at launch\n"
        "  --paste TEXT     inject text through the keyboard matrix\n"
        "  --paste-at N     delay --paste until emulated frame N\n"
        "  --frames N       exit after N emulated frames\n"
        "  --no-throttle    run without real-time frame pacing\n"
        "  --help           this message\n"
        "\n"
        "  F4     Screenshot (PPM)\n"
        "  F5     Reset\n"
        "  F6     Toggle GIF capture\n"
        "  F7     Pause\n"
        "  F8     Monitor\n"
        "  F9     Options overlay\n"
        "  F10    Switch 40/80-column display\n"
        "  F11    Toggle fullscreen\n"
        "  F12    Quit\n"
        "  Ctrl+V Paste clipboard text\n"
        "  Ctrl++ / Ctrl+- Adjust window scale\n",
        argv0);
}

int main(int argc, char **argv) {
    Config cfg;
    config_set_defaults(&cfg);
    const char *rom_dir = NULL;
    const char *disk_path = NULL;
    const char *gif_out = NULL;
    const char *paste_arg = NULL;
    long paste_frame = 0;
    long frames_arg = -1;
    bool no_throttle = false;
    char cfg_path[CONFIG_PATH_MAX];

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc) cfg.scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fullscreen")) cfg.fullscreen = true;
        else if (!strcmp(argv[i], "--fast")) cfg.fast = true;
        else if (!strcmp(argv[i], "--rom") && i + 1 < argc) rom_dir = argv[++i];
        else if (!strcmp(argv[i], "--disk") && i + 1 < argc) disk_path = argv[++i];
        else if (!strcmp(argv[i], "--gif-out") && i + 1 < argc) gif_out = argv[++i];
        else if (!strcmp(argv[i], "--paste") && i + 1 < argc) paste_arg = argv[++i];
        else if (!strcmp(argv[i], "--paste-at") && i + 1 < argc) paste_frame = atol(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames_arg = atol(argv[++i]);
        else if (!strcmp(argv[i], "--no-throttle")) no_throttle = true;
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(argv[0]); return 0; }
        else if (argv[i][0] == '-') { usage(argv[0]); return 1; }
    }

    /* Load user config (overrides defaults; command-line flags above still
     * win). If there is no config file yet, create one with the defaults so
     * it exists for later runs. */
    config_path(cfg_path, sizeof(cfg_path));
    if (!config_load(&cfg, cfg_path))
        config_save(&cfg, cfg_path);
    if (rom_dir) snprintf(cfg.rom_dir, sizeof(cfg.rom_dir), "%s", rom_dir);
    if (disk_path) snprintf(cfg.disk_path, sizeof(cfg.disk_path), "%s", disk_path);
    g_boot_trace = getenv("C128_BOOT_TRACE") != NULL;
    g_sid_trace = getenv("C128_SID_TRACE") != NULL;
    g_debug_enabled = g_boot_trace;
    if (getenv("C128_SAVE_PPM"))
        g_save_ppm = strdup(getenv("C128_SAVE_PPM"));
    if (getenv("C128_SAVE_FRAME"))
        g_save_ppm_frame = atoi(getenv("C128_SAVE_FRAME"));

    net_compat_init();  /* WSAStartup on Windows; no-op elsewhere */
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    atexit(SDL_Quit);

    notify_init();
    notify_set_mode(cfg.notify_mode);

    C128 c;
    c128_init(&c, &cfg);

    if (display_init(&c.display, "1986 — Commodore C128DCR", cfg.scale) != 0) {
        fprintf(stderr, "display_init failed\n");
        return 1;
    }
    display_set_smoothing(&c.display, cfg.smoothing);
    display_set_crt(&c.display, cfg.crt_enabled, cfg.crt_scanlines,
                    cfg.crt_brightness, cfg.crt_contrast,
                    cfg.crt_red, cfg.crt_green, cfg.crt_blue);
    display_set_one_display(&c.display, cfg.one_display);
    if (cfg.fullscreen) SDL_SetWindowFullscreen(c.display.window, true);

    SDL_AudioStream *audio_stream = NULL;
    if (!no_throttle) {
        SDL_AudioSpec audio_spec = {
            .format = SDL_AUDIO_S16, .channels = 1, .freq = SID_SAMPLE_RATE
        };
        audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                 &audio_spec, NULL, NULL);
        if (!audio_stream || !SDL_ResumeAudioStreamDevice(audio_stream)) {
            fprintf(stderr, "1986: SID audio unavailable: %s\n", SDL_GetError());
            if (audio_stream) SDL_DestroyAudioStream(audio_stream);
            audio_stream = NULL;
        }
    }

    /* Disk-drive activity LED at the bottom of the window. */
    leds_set_enabled(LED_FDC_A, true);

    /* Load ROMs into the machine (optional at this stage). Default to the
     * executable's directory's "roms" subdirectory when no ROM dir is
     * configured. */
    {
        char rom_default[CONFIG_PATH_MAX];
        const char *dir = cfg.rom_dir[0] ? cfg.rom_dir : NULL;
        const char *base = NULL;
        if (!dir) {
            base = SDL_GetBasePath();
            if (base) {
                snprintf(rom_default, sizeof(rom_default), "%s/roms", base);
                dir = rom_default;
            } else {
                dir = ROM_INSTALL_DIR;
            }
        }
        int n = mem_load_c128_roms(&c.mem, dir);
        if (n == 0) {
            fprintf(stderr, "1986: no C128DCR ROMs found in '%s' (boot will not start)\n", dir);
            notify_post("NO ROMS FOUND");
        } else {
            fprintf(stderr, "1986: loaded %d ROM image(s) from '%s'\n", n, dir);
        }
        /* Virtual-drive mode reads the image directly and deliberately does
         * not load a 1571 ROM. A future true-drive module will own that ROM. */
        if (cfg.disk_path[0] && drive_attach_disk(&c.drive, cfg.disk_path) != 0)
            fprintf(stderr, "1986: could not attach disk '%s'\n", cfg.disk_path);
        if (cfg.disk2_path[0] && drive_attach_disk(&c.drive2, cfg.disk2_path) != 0)
            fprintf(stderr, "1986: could not attach second disk '%s'\n",
                    cfg.disk2_path);
    }

    /* Reset after ROMs are loaded so the reset vector comes from the KERNAL. */
    c.col_mode_80 = cfg.col_mode_80;
    c128_reset(&c);
    display_focus_active(&c.display);

    /* Patch the KERNAL ROM with the IEC serial traps (like VICE) so the boot
     * does not block waiting for the serial/disk bus, and forward the IEC
     * (LISTEN/TALK/send/receive) calls to the pluggable disk drive. */
    IecCallbacks iec = {
        .ctx = &c,
        .force_slow_serial = true,
        .attention = c128_iec_attention,
        .send = c128_iec_send,
        .receive = c128_iec_receive,
        .take_status = c128_iec_take_status,
    };
    cpu_install_iec_traps(c.mem.kernal, &iec);

    Overlay overlay;
    overlay_init(&overlay, &cfg, &c);

    Monitor *monitor = monitor_create(&c);
    Paste paste;
    paste_init(&paste);
    bool paste_started = false;
    if (paste_arg && paste_frame <= 0) {
        paste_text(&paste, paste_arg);
        paste_started = true;
    }

    if (gif_out) videocap_start(gif_out, cfg.gif_width, cfg.gif_fps);

    bool running = true;
    bool fullscreen = cfg.fullscreen;
    bool paused = false;
    int  mouse_captured = 0;
    bool pc_shift_held = false;
    uint64_t next_frame = 0;

    while (running) {
        /* --- Event processing --- */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (ev.type == SDL_EVENT_WINDOW_RESIZED ||
                       ev.type == SDL_EVENT_WINDOW_SHOWN) {
                /* Nothing special — renderer resizes automatically. */
            } else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                leds_set_mouse_position(ev.motion.x, ev.motion.y, true);
            } else if (ev.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
                leds_set_mouse_position(0, 0, false);
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (!paused && !overlay_is_visible(&overlay) && mouse_captured) {
                    /* placeholder: joystick/mouse later */
                }
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                /* placeholder */
            }

            if (monitor_handle_event(monitor, &ev)) continue;

            /* F9 opens overlay — release mouse capture first. */
            if (mouse_captured && ev.type == SDL_EVENT_KEY_DOWN &&
                ev.key.scancode == SDL_SCANCODE_F9)
                mouse_captured = 0;

            if (overlay_handle_event(&overlay, &ev)) continue;

            if (ev.type == SDL_EVENT_KEY_DOWN) {
                bool ctrl  = (ev.key.mod & SDL_KMOD_CTRL) != 0;
                bool shift = (ev.key.mod & SDL_KMOD_SHIFT) != 0;
                bool fkey  = (ev.key.scancode >= SDL_SCANCODE_F1 &&
                              ev.key.scancode <= SDL_SCANCODE_F8);
                if (ev.key.scancode == SDL_SCANCODE_LSHIFT ||
                    ev.key.scancode == SDL_SCANCODE_RSHIFT)
                    pc_shift_held = true;
                bool key_plus  = (ev.key.scancode == SDL_SCANCODE_EQUALS ||
                                  ev.key.scancode == SDL_SCANCODE_KP_PLUS);
                bool key_minus = (ev.key.scancode == SDL_SCANCODE_MINUS ||
                                  ev.key.scancode == SDL_SCANCODE_KP_MINUS);
                if (ctrl && (key_plus || key_minus)) {
                    cfg.scale += key_plus ? 1 : -1;
                    if (cfg.scale < 1) cfg.scale = 1;
                    if (cfg.scale > 4) cfg.scale = 4;
                    SDL_SetWindowSize(c.display.window,
                                      WINDOW_W * cfg.scale,
                                      WINDOW_H * cfg.scale + FUNCTION_KEY_BAR_HEIGHT + LED_BAR_HEIGHT);
                    continue;
                }
                /* Shift+PrintScreen toggles the 40/80 column key. */
                if (shift && ev.key.scancode == SDL_SCANCODE_PRINTSCREEN) {
                    c.mem.mmu.col4080 = false;   /* key pressed -> 80-col */
                    continue;
                }
                /* Shift+F1-F8 press the C128 function keys (they're the
                 * emulator's plain F-key shortcuts otherwise). The held PC
                 * Shift must NOT also apply as a C128 Shift, or F1 would read
                 * as F2; override it with the function key's own Shift. */
                if (fkey && shift) {
                    int row, col;
                    bool need_shift;
                    if (kbd_map_scancode(ev.key.scancode, &row, &col, &need_shift)) {
                        kbd_set(&c.kbd, KBD_LSHIFT_ROW, KBD_LSHIFT_COL, false);
                        kbd_set(&c.kbd, KBD_SHIFT_ROW, KBD_SHIFT_COL, false);
                        if (need_shift)
                            kbd_set(&c.kbd, KBD_SHIFT_ROW, KBD_SHIFT_COL, true);
                        kbd_set(&c.kbd, row, col, true);
                    }
                    continue;
                }
                if (ev.key.scancode == SDL_SCANCODE_F12) {
                    running = false;
                } else if (ev.key.scancode == SDL_SCANCODE_F8) {
                    if (monitor_is_open(monitor))
                        monitor_handle_event(monitor,
                            &(SDL_Event){.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED,
                                         .window.windowID = monitor_window_id(monitor)});
                    else
                        monitor_open(monitor);
                } else if (ev.key.scancode == SDL_SCANCODE_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(c.display.window, fullscreen);
                } else if (ev.key.scancode == SDL_SCANCODE_F4) {
                    char path[256];
                    char tmp[256];
                    strncpy(tmp, argv[0], sizeof(tmp) - 1);
                    tmp[sizeof(tmp) - 1] = '\0';
                    snprintf(path, sizeof(path), "%s_%ld.ppm",
                             basename(tmp), (long)time(NULL));
                    display_save_ppm_active(&c.display, path);
                } else if (ev.key.scancode == SDL_SCANCODE_F5) {
                    c128_reset(&c);
                    if (audio_stream) SDL_ClearAudioStream(audio_stream);
                } else if (ev.key.scancode == SDL_SCANCODE_F6) {
                    if (videocap_active()) {
                        videocap_stop();
                    } else {
                        char path[256];
                        time_t t = time(NULL);
                        struct tm *lt = localtime(&t);
                        if (lt) strftime(path, sizeof(path),
                                         "1986-%Y%m%d-%H%M%S.gif", lt);
                        else snprintf(path, sizeof(path), "1986-capture.gif");
                        videocap_start(path, cfg.gif_width, cfg.gif_fps);
                    }
                } else if (ev.key.scancode == SDL_SCANCODE_F7) {
                    paused = !paused;
                    c.paused = paused;
                    if (paused && audio_stream) SDL_ClearAudioStream(audio_stream);
                } else if (ev.key.scancode == SDL_SCANCODE_F10) {
                    c128_switch_4080(&c);   /* toggle 40-col VIC <-> 80-col VDC */
                    cfg.col_mode_80 = c.col_mode_80;
                    display_focus_active(&c.display);
                    if (cfg.display_change_reset) {
                        c128_reset(&c);
                        if (audio_stream) SDL_ClearAudioStream(audio_stream);
                    }
                } else if (ev.key.scancode == SDL_SCANCODE_V &&
                           (SDL_GetModState() & SDL_KMOD_CTRL)) {
                    char *text = SDL_GetClipboardText();
                    if (text) { paste_text(&paste, text); SDL_free(text); }
                } else if (!fkey) {
                    /* Non-function keys go to the C128 keyboard. Plain F1-F8
                     * are reserved for the emulator shortcuts above. */
                    c128_key_event(&c, ev.key.scancode, true);
                }
            } else if (ev.type == SDL_EVENT_KEY_UP) {
                if (ev.key.scancode == SDL_SCANCODE_LSHIFT ||
                    ev.key.scancode == SDL_SCANCODE_RSHIFT)
                    pc_shift_held = false;
                if (ev.key.scancode == SDL_SCANCODE_PRINTSCREEN)
                    c.mem.mmu.col4080 = true;   /* 40/80 key released */
                bool fkey = (ev.key.scancode >= SDL_SCANCODE_F1 &&
                             ev.key.scancode <= SDL_SCANCODE_F8);
                if (fkey) {
                    /* Release a Shift+Fn key, restoring the PC Shift if it is
                     * still held (so a following letter stays shifted). */
                    int row, col;
                    bool need_shift;
                    if (kbd_map_scancode(ev.key.scancode, &row, &col, &need_shift)) {
                        kbd_set(&c.kbd, row, col, false);
                        kbd_set(&c.kbd, KBD_SHIFT_ROW, KBD_SHIFT_COL, false);
                        if (pc_shift_held)
                            kbd_set(&c.kbd, KBD_LSHIFT_ROW, KBD_LSHIFT_COL, true);
                    }
                } else {
                    c128_key_event(&c, ev.key.scancode, false);
                }
            }
        }

        /* --- Paste injection (one key per frame) --- */
        if (paste_arg && !paste_started && c128_frame_count >= paste_frame) {
            paste_text(&paste, paste_arg);
            paste_started = true;
        }
        paste_tick(&paste, &c.kbd);

        /* --- Overlay (process async file-dialog results) --- */
        overlay_tick(&overlay);

        /* --- Machine step --- */
        if (!paused) {
            int cycles = c128_frame(&c);
            uint64_t emulated_frame_ns = c128_cycles_to_ns(&c, cycles);
            /* Keep only a few frames queued if the host stalls. The SID core
             * keeps clocking even without an available audio device. */
            if (audio_stream && c.audio_count > 0 &&
                SDL_GetAudioStreamQueued(audio_stream) <
                    6 * (int)(SID_SAMPLE_RATE / 50) * (int)sizeof(s16))
                SDL_PutAudioStreamData(audio_stream, c.audio_frame,
                                       c.audio_count * (int)sizeof(s16));
            if (g_sid_trace && (c128_frame_count % 10) == 0) {
                long energy = 0;
                for (int i = 0; i < c.audio_count; ++i)
                    energy += labs(c.audio_frame[i]);
                fprintf(stderr, "[sid] frame=%d vol=%02X v1=%02X/%02X/%02X/%02X env=%u samples=%d energy=%ld\n",
                        c128_frame_count, c.sid.regs[0x18], c.sid.regs[0],
                        c.sid.regs[1], c.sid.regs[4], c.sid.regs[6],
                        c.sid.voice[0].env, c.audio_count, energy);
            }

            /* Optional boot-progress trace (C128_BOOT_TRACE=1). */
            if (g_boot_trace && (c128_frame_count % 10) == 0) {
                const Cpu8502 *cpu = &c.cpu;
                fprintf(stderr, "[boot] frame=%d PC=%04X SP=%02X P=%02X mcr=%02X\n",
                        c128_frame_count, cpu->pc, cpu->sp, cpu->p, c.mem.mmu.mcr);
            }

            /* Pace to the emulated frame time. */
            uint64_t now = SDL_GetTicksNS();
            if (next_frame == 0) next_frame = now;
            if (!no_throttle) {
                if (now < next_frame)
                    SDL_Delay((Uint32)((next_frame - now) / 1000000ULL));
                next_frame += emulated_frame_ns;
            }

            if (g_videocap_gif && videocap_gif_due(emulated_frame_ns))
                gifcap_frame(g_videocap_gif, c.display.pixels);

            /* One-shot frame capture (C128_SAVE_PPM=<path>) for visual debug. */
            if (g_save_ppm && c128_frame_count == g_save_ppm_frame) {
                display_save_ppm_active(&c.display, g_save_ppm);
                g_save_ppm = NULL;
            }

            /* Debug: stop after a fixed number of frames. */
            if (frames_arg > 0 && c128_frame_count >= frames_arg)
                running = false;
        } else {
            display_apply_greyscale(&c.display);
        }

        /* --- Notifications (fade/toast timer) --- */
        notify_tick(20);

        /* --- Frame present --- */
        display_upload(&c.display);
        overlay_render(&overlay, c.display.renderer);
        display_render_function_keys(&c.display);
        if (paused) display_draw_paused_label(&c.display);
        notify_render(c.display.renderer);
        if (!c.display.one_display && c.display.vdc_renderer)
            notify_render(c.display.vdc_renderer);
        display_flip(&c.display);
        if (monitor_is_open(monitor)) monitor_render(monitor);
    }

    if (videocap_active()) videocap_stop();
    if (audio_stream) SDL_DestroyAudioStream(audio_stream);
    if (!config_save_column_mode(cfg_path, c.col_mode_80))
        fprintf(stderr, "1986: could not save display mode to '%s'\n", cfg_path);
    paste_free(&paste);
    monitor_destroy(monitor);
    overlay_quit(&overlay);
    drive_attach_disk(&c.drive, NULL);
    drive_attach_disk(&c.drive2, NULL);
    display_destroy(&c.display);
    return 0;
}
