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
    CHECK(!cfg.real_disk_drive, "real drive defaults off");
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
    cfg.real_disk_drive = true;
    cfg.second_drive = true;
    cfg.drive_unit = 9;
    cfg.drive2_unit = 10;
    cfg.main_input_port = 1;
    cfg.joy_port_mode[0] = JOYPORT_MOUSE;
    snprintf(cfg.disk_path, sizeof(cfg.disk_path), "%s", "keep-me.d64");
    snprintf(cfg.disk2_path, sizeof(cfg.disk2_path), "%s", "second.d81");
    snprintf(cfg.u36_path, sizeof(cfg.u36_path), "%s", "utility.rom");
    CHECK(config_save(&cfg, path), "config_save");

    Config back;
    CHECK(config_load(&back, path), "config_load");
    CHECK(back.scale == 3, "scale roundtrip");
    CHECK(back.fast, "fast roundtrip");
    CHECK(back.crt_enabled, "crt roundtrip");
    CHECK(!back.col_mode_80, "40-column mode roundtrip");
    CHECK(back.real_disk_drive, "real-drive preference roundtrip");
    CHECK(back.second_drive && back.drive_unit == 9 && back.drive2_unit == 10 &&
          strcmp(back.disk2_path, "second.d81") == 0,
          "second-drive toggle, unit, and image roundtrip");
    CHECK(back.main_input_port == 1 && back.joy_port_mode[0] == JOYPORT_MOUSE,
          "input port and 1351 mode roundtrip");
    CHECK(strcmp(back.u36_path, "utility.rom") == 0,
          "U36 ROM path roundtrip");

    CHECK(config_save_column_mode(path, true), "save 80-column mode only");
    CHECK(config_load(&back, path), "reload 80-column mode");
    CHECK(back.col_mode_80, "80-column mode persisted");
    CHECK(back.scale == 3, "mode-only save preserves other settings");
    CHECK(strcmp(back.disk_path, "keep-me.d64") == 0,
          "mode-only save preserves media settings");
    CHECK(strcmp(back.u36_path, "utility.rom") == 0,
          "mode-only save preserves U36 setting");
    CHECK(back.real_disk_drive,
          "mode-only save preserves real-drive preference");
    CHECK(back.second_drive && back.drive2_unit == 10 &&
          strcmp(back.disk2_path, "second.d81") == 0,
          "mode-only save preserves second-drive media settings");

    CHECK(config_save_column_mode(path, false), "save 40-column mode only");
    CHECK(config_load(&back, path), "reload 40-column mode");
    CHECK(!back.col_mode_80, "40-column mode persisted");

    FILE *collision = fopen(path, "w");
    CHECK(collision != NULL, "create unit-collision config");
    if (collision) {
        fputs("drive_unit = 10\ndrive2_unit = 10\nsecond_drive = 1\n", collision);
        fclose(collision);
    }
    CHECK(config_load(&back, path) && back.drive_unit == 10 &&
          back.drive2_unit == 8,
          "loading a colliding unit assignment chooses a distinct unit");

    remove(path);

    if (failures == 0) { printf("test-config: OK\n"); return 0; }
    printf("test-config: %d failure(s)\n", failures);
    return 1;
}
