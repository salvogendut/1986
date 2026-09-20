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

    cfg.scale = 3;
    cfg.fast = true;
    cfg.crt_enabled = true;
    cfg.col_mode_80 = false;
    snprintf(cfg.disk_path, sizeof(cfg.disk_path), "%s", "keep-me.d64");
    CHECK(config_save(&cfg, path), "config_save");

    Config back;
    CHECK(config_load(&back, path), "config_load");
    CHECK(back.scale == 3, "scale roundtrip");
    CHECK(back.fast, "fast roundtrip");
    CHECK(back.crt_enabled, "crt roundtrip");
    CHECK(!back.col_mode_80, "40-column mode roundtrip");

    CHECK(config_save_column_mode(path, true), "save 80-column mode only");
    CHECK(config_load(&back, path), "reload 80-column mode");
    CHECK(back.col_mode_80, "80-column mode persisted");
    CHECK(back.scale == 3, "mode-only save preserves other settings");
    CHECK(strcmp(back.disk_path, "keep-me.d64") == 0,
          "mode-only save preserves media settings");

    CHECK(config_save_column_mode(path, false), "save 40-column mode only");
    CHECK(config_load(&back, path), "reload 40-column mode");
    CHECK(!back.col_mode_80, "40-column mode persisted");

    remove(path);

    if (failures == 0) { printf("test-config: OK\n"); return 0; }
    printf("test-config: %d failure(s)\n", failures);
    return 1;
}
