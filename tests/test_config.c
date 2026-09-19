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

    cfg.scale = 3;
    cfg.fast = true;
    cfg.crt_enabled = true;
    const char *path = "/tmp/1986-test.conf";
    CHECK(config_save(&cfg, path), "config_save");

    Config back;
    CHECK(config_load(&back, path), "config_load");
    CHECK(back.scale == 3, "scale roundtrip");
    CHECK(back.fast, "fast roundtrip");
    CHECK(back.crt_enabled, "crt roundtrip");

    remove(path);

    if (failures == 0) { printf("test-config: OK\n"); return 0; }
    printf("test-config: %d failure(s)\n", failures);
    return 1;
}
