/* Browser host for the C128DCR core.
 *
 * SDL is used only for its stable scancode constants. Video and audio are
 * handed directly to Javascript through these small exported functions.
 */
#include <emscripten.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "c128.h"
#include "cartridge.h"
#include "cpu.h"
#include "drive.h"
#include "drive1571cr.h"
#include "joyport.h"

#define AUDIO_RING_SAMPLES (SID_SAMPLE_RATE * 4)

static C128 g_c128;
static Config g_config;
static s16 g_audio_ring[AUDIO_RING_SAMPLES];
static int g_audio_read;
static int g_audio_write;
static char g_disk_path[CONFIG_PATH_MAX];
static char g_tape_path[CONFIG_PATH_MAX];
static char g_cart_path[CONFIG_PATH_MAX];
static bool g_started;

extern unsigned web_led_mask(void);
extern void web_led_tick(void);

static bool browser_c64_mode(void *ctx) {
    return c128_is_c64_mode((const C128 *)ctx);
}

static void install_drive_backend(void) {
    IecCallbacks iec = {
        .ctx = &g_c128,
        .force_slow_serial = true,
        .attention = c128_iec_attention,
        .send = c128_iec_send,
        .receive = c128_iec_receive,
        .take_status = c128_iec_take_status,
        .c64_mode = browser_c64_mode,
    };
    g_c128.drive_raw_iec = g_config.real_disk_drive &&
        g_c128.integrated_drive.rom_loaded;
    g_c128.drive2_raw_iec = false;
    iec_bus_enable_second(&g_c128.iec_bus, false);
    if (!g_c128.drive_raw_iec)
        cpu_install_iec_traps(g_c128.mem.kernal, &iec);
}

static int machine_start(bool real_drive) {
    bool col80 = g_started ? g_c128.col_mode_80 : true;
    if (g_started) {
        drive_attach_disk(&g_c128.drive, NULL);
        c128_eject_tape(&g_c128);
    }
    config_set_defaults(&g_config);
    g_config.col_mode_80 = col80;
    g_config.one_display = true;
    g_config.vdc_ram_kb = 64;
    g_config.real_disk_drive = real_drive;
    g_config.drive_type = 1571;
    g_config.second_drive = false;
    g_config.notify_mode = NOTIFY_MODE_OFF;

    c128_init(&g_c128, &g_config);
    if (mem_load_c128_roms(&g_c128.mem, "/roms") < 3)
        return -1;
    drive1571cr_load_rom(&g_c128.integrated_drive, "/roms/dos1571cr.bin");
    install_drive_backend();

    if (g_disk_path[0] && drive_attach_disk(&g_c128.drive, g_disk_path) != 0)
        g_disk_path[0] = '\0';
    if (g_tape_path[0] && !c128_mount_tape(&g_c128, g_tape_path))
        g_tape_path[0] = '\0';
    if (g_cart_path[0] && cartridge_attach(&g_c128.mem.cart, g_cart_path) != CART_OK)
        g_cart_path[0] = '\0';

    g_c128.col_mode_80 = col80;
    c128_reset(&g_c128);
    g_started = true;
    g_audio_read = g_audio_write = 0;
    return 0;
}

EMSCRIPTEN_KEEPALIVE int poc_init(void) {
    g_disk_path[0] = g_tape_path[0] = g_cart_path[0] = '\0';
    return machine_start(false);
}

EMSCRIPTEN_KEEPALIVE void poc_reset(void) {
    c128_reset(&g_c128);
    g_audio_read = g_audio_write = 0;
}

EMSCRIPTEN_KEEPALIVE int poc_step(void) {
    c128_frame(&g_c128);
    for (int i = 0; i < g_c128.audio_count; ++i) {
        int next = (g_audio_write + 1) % AUDIO_RING_SAMPLES;
        if (next == g_audio_read) break;
        g_audio_ring[g_audio_write] = g_c128.audio_frame[i];
        g_audio_write = next;
    }
    web_led_tick();
    return 0;
}

EMSCRIPTEN_KEEPALIVE unsigned int *poc_pixels(void) {
    return g_c128.display.vdc_active ? g_c128.display.vdc_pixels
                                     : g_c128.display.pixels;
}

EMSCRIPTEN_KEEPALIVE int poc_width(void) {
    return g_c128.display.vdc_active ? VDC_SCREEN_W : C128_SCREEN_W;
}

EMSCRIPTEN_KEEPALIVE int poc_height(void) {
    return g_c128.display.vdc_active ? VDC_SCREEN_H : C128_SCREEN_H;
}

EMSCRIPTEN_KEEPALIVE int poc_display(void) {
    return g_c128.display.vdc_active ? 80 : 40;
}

EMSCRIPTEN_KEEPALIVE void poc_set_display(int columns) {
    c128_set_4080(&g_c128, columns == 80);
}

EMSCRIPTEN_KEEPALIVE void poc_key(int scancode, int pressed) {
    c128_key_event(&g_c128, scancode, pressed != 0);
}

EMSCRIPTEN_KEEPALIVE void poc_joy(int control, int pressed) {
    static u8 state;
    u8 mask = 0;
    if (control == 0) mask = JOY_UP;
    else if (control == 1) mask = JOY_DOWN;
    else if (control == 2) mask = JOY_LEFT;
    else if (control == 3) mask = JOY_RIGHT;
    else if (control == 4 || control == 5) mask = JOY_FIRE;
    if (!mask) return;
    if (pressed) state |= mask; else state &= (u8)~mask;
    joyports_set_joystick(&g_c128.joyports, 1, state);
}

EMSCRIPTEN_KEEPALIVE int poc_load_disk(const char *path) {
    int result = drive_attach_disk(&g_c128.drive, path);
    if (result == 0) snprintf(g_disk_path, sizeof(g_disk_path), "%s", path);
    return result;
}

EMSCRIPTEN_KEEPALIVE void poc_eject_disk(void) {
    drive_attach_disk(&g_c128.drive, NULL);
    g_disk_path[0] = '\0';
}

EMSCRIPTEN_KEEPALIVE int poc_load_tape(const char *path) {
    if (!c128_mount_tape(&g_c128, path)) return -1;
    snprintf(g_tape_path, sizeof(g_tape_path), "%s", path);
    return 0;
}

EMSCRIPTEN_KEEPALIVE void poc_eject_tape(void) {
    c128_eject_tape(&g_c128);
    g_tape_path[0] = '\0';
}

EMSCRIPTEN_KEEPALIVE void poc_tape_control(int action) {
    if (action == 0) tape_stop(&g_c128.tape);
    else if (action == 1) tape_play(&g_c128.tape);
    else if (action == 2) tape_rewind(&g_c128.tape);
}

EMSCRIPTEN_KEEPALIVE int poc_load_cart(const char *path) {
    CartridgeResult result = cartridge_attach(&g_c128.mem.cart, path);
    if (result != CART_OK) return -(int)result;
    snprintf(g_cart_path, sizeof(g_cart_path), "%s", path);
    c128_reset(&g_c128);
    return 0;
}

EMSCRIPTEN_KEEPALIVE void poc_eject_cart(void) {
    cartridge_detach(&g_c128.mem.cart);
    g_cart_path[0] = '\0';
    c128_reset(&g_c128);
}

EMSCRIPTEN_KEEPALIVE int poc_set_real_drive(int enabled) {
    return machine_start(enabled != 0);
}

EMSCRIPTEN_KEEPALIVE int poc_real_drive(void) {
    return g_c128.drive_raw_iec ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE unsigned poc_activity(void) {
    return web_led_mask();
}

EMSCRIPTEN_KEEPALIVE int poc_audio_avail(void) {
    return (g_audio_write - g_audio_read + AUDIO_RING_SAMPLES) % AUDIO_RING_SAMPLES;
}

EMSCRIPTEN_KEEPALIVE int poc_audio_read_pos(void) { return g_audio_read; }
EMSCRIPTEN_KEEPALIVE short *poc_audio_buffer(void) { return g_audio_ring; }
EMSCRIPTEN_KEEPALIVE void poc_audio_advance(int count) {
    if (count > 0) g_audio_read = (g_audio_read + count) % AUDIO_RING_SAMPLES;
}

EMSCRIPTEN_KEEPALIVE void poc_audio_reset(void) {
    g_audio_read = g_audio_write = 0;
}
