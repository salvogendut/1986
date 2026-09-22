#include "config.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

int main(void) {
    Config cfg;
    config_set_defaults(&cfg);
    CHECK(cfg.scale == 2, "default scale");
    CHECK(cfg.model == C128_MODEL_DCR, "default model DCR");
    CHECK(cfg.col_mode_80, "default display is 80 columns");
    CHECK(cfg.vdc_ram_kb == 64, "C128DCR defaults to 64K VDC RAM");
    CHECK(!cfg.real_disk_drive, "real drive defaults off");
    CHECK(!cfg.drive_audio_monitor, "drive audio monitor defaults off");
    CHECK(!cfg.drive_visual_monitor, "drive visual monitor defaults off");
    CHECK(cfg.drive_type == 1571 && cfg.drive2_type == 1571,
          "both hardware drive types default to 1571");
    CHECK(!cfg.second_drive && cfg.drive_unit == 8 && cfg.drive2_unit == 9,
          "second drive defaults off with a distinct unit");
    CHECK(cfg.main_input_port == 2 && cfg.joy_port_mode[0] == JOYPORT_JOYSTICK &&
          cfg.joy_port_mode[1] == JOYPORT_JOYSTICK,
          "default gamepad target is joystick port 2");

    const char *path = "/tmp/1986-test.conf";
    FILE *legacy = fopen(path, "w");
    CHECK(legacy != NULL, "create legacy config");
    if (legacy) {
        fputs("scale = 4\n", legacy);
        fclose(legacy);
    }
    Config legacy_cfg;
    CHECK(config_load(&legacy_cfg, path), "load config without display mode");
    CHECK(legacy_cfg.col_mode_80,
          "config without display mode defaults to 80 columns");
    CHECK(!legacy_cfg.real_disk_drive,
          "config without real-drive selection defaults off");
    CHECK(!legacy_cfg.second_drive && legacy_cfg.drive2_unit == 9,
          "legacy config defaults the second drive off at unit 9");

    cfg.scale = 3;
    cfg.fast = true;
    cfg.crt_enabled = true;
    cfg.col_mode_80 = false;
    cfg.vdc_ram_kb = 16;
    cfg.real_disk_drive = true;
    cfg.drive_audio_monitor = true;
    cfg.drive_visual_monitor = true;
    cfg.second_drive = true;
    cfg.drive_unit = 9;
    cfg.drive2_unit = 10;
    cfg.drive_type = 1581;
    cfg.drive2_type = 1571;
    cfg.main_input_port = 1;
    cfg.joy_port_mode[0] = JOYPORT_MOUSE;
    snprintf(cfg.disk_path, sizeof(cfg.disk_path), "%s", "keep-me.d64");
    snprintf(cfg.disk2_path, sizeof(cfg.disk2_path), "%s", "second.d81");
    snprintf(cfg.u36_path, sizeof(cfg.u36_path), "%s", "utility.rom");
    snprintf(cfg.last_disk_dir, sizeof(cfg.last_disk_dir), "%s", "/media/drive1");
    snprintf(cfg.last_disk2_dir, sizeof(cfg.last_disk2_dir), "%s", "/media/drive2");
    snprintf(cfg.last_tape_dir, sizeof(cfg.last_tape_dir), "%s", "/media/tapes");
    snprintf(cfg.last_cart_dir, sizeof(cfg.last_cart_dir), "%s", "/media/carts");
    snprintf(cfg.last_u36_dir, sizeof(cfg.last_u36_dir), "%s", "/media/roms");
    CHECK(config_save(&cfg, path), "config_save");

    Config back;
    CHECK(config_load(&back, path), "config_load");
    CHECK(back.scale == 3, "scale roundtrip");
    CHECK(back.fast, "fast roundtrip");
    CHECK(back.crt_enabled, "crt roundtrip");
    CHECK(!back.col_mode_80, "40-column mode roundtrip");
    CHECK(back.vdc_ram_kb == 16, "16K VDC RAM setting roundtrip");
    CHECK(back.real_disk_drive, "real-drive preference roundtrip");
    CHECK(back.drive_audio_monitor, "drive audio monitor roundtrip");
    CHECK(back.drive_visual_monitor, "drive visual monitor roundtrip");
    CHECK(back.drive_type == 1581 && back.drive2_type == 1571,
          "hardware type selection roundtrip");
    CHECK(back.second_drive && back.drive_unit == 9 && back.drive2_unit == 10 &&
          strcmp(back.disk2_path, "second.d81") == 0,
          "second-drive toggle, unit, and image roundtrip");
    CHECK(back.main_input_port == 1 && back.joy_port_mode[0] == JOYPORT_MOUSE,
          "input port and 1351 mode roundtrip");

    CHECK(config_save_input_port(path, 2), "F1 host-port setting saved");
    CHECK(config_load(&back, path) && back.main_input_port == 2 &&
          back.joy_port_mode[0] == JOYPORT_MOUSE && back.second_drive,
          "F1 host-port save preserves modes and other settings");
    CHECK(!config_save_input_port(path, 3), "invalid host port is rejected");
    CHECK(strcmp(back.u36_path, "utility.rom") == 0,
          "U36 ROM path roundtrip");
    CHECK(strcmp(back.last_disk_dir, "/media/drive1") == 0 &&
          strcmp(back.last_disk2_dir, "/media/drive2") == 0 &&
          strcmp(back.last_tape_dir, "/media/tapes") == 0 &&
          strcmp(back.last_cart_dir, "/media/carts") == 0 &&
          strcmp(back.last_u36_dir, "/media/roms") == 0,
          "each file dialog's recent directory roundtrips independently");

    CHECK(config_save_column_mode(path, true), "save 80-column mode only");
    CHECK(config_load(&back, path), "reload 80-column mode");
    CHECK(back.col_mode_80, "80-column mode persisted");
    CHECK(back.vdc_ram_kb == 16, "mode-only save preserves VDC RAM size");
    CHECK(back.scale == 3, "mode-only save preserves other settings");
    CHECK(strcmp(back.disk_path, "keep-me.d64") == 0,
          "mode-only save preserves media settings");
    CHECK(strcmp(back.u36_path, "utility.rom") == 0,
          "mode-only save preserves U36 setting");
    CHECK(back.real_disk_drive,
          "mode-only save preserves real-drive preference");
    CHECK(back.drive_audio_monitor,
          "mode-only save preserves drive audio preference");
    CHECK(back.drive_visual_monitor,
          "mode-only save preserves drive visual preference");
    CHECK(back.main_input_port == 2 && back.joy_port_mode[0] == JOYPORT_MOUSE,
          "mode-only save preserves F1 input port and mouse mode");
    CHECK(back.second_drive && back.drive2_unit == 10 &&
          strcmp(back.disk2_path, "second.d81") == 0,
          "mode-only save preserves second-drive media settings");

    CHECK(config_save_column_mode(path, false), "save 40-column mode only");
    CHECK(config_load(&back, path), "reload 40-column mode");
    CHECK(!back.col_mode_80, "40-column mode persisted");

    FILE *collision = fopen(path, "w");
    CHECK(collision != NULL, "create unit-collision config");
    if (collision) {
        fputs("drive_unit = 10\ndrive2_unit = 10\nsecond_drive = 1\n"
              "vdc_ram_kb = 32\ndrive_type = 999\ndrive2_type = 0\n", collision);
        fclose(collision);
    }
    CHECK(config_load(&back, path) && back.drive_unit == 10 &&
          back.drive2_unit == 8,
          "loading a colliding unit assignment chooses a distinct unit");
    CHECK(back.vdc_ram_kb == 64, "invalid VDC RAM size falls back to DCR default");
    CHECK(back.drive_type == 1571 && back.drive2_type == 1571,
          "invalid hardware drive types fall back to 1571");

    remove(path);

    if (failures == 0) { printf("test-config: OK\n"); return 0; }
    printf("test-config: %d failure(s)\n", failures);
    return 1;
}
