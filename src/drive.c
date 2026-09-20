#include "drive.h"
#include "leds.h"
#include <stdio.h>
#include <string.h>

void drive_init(Drive *d, Config *cfg) {
    memset(d, 0, sizeof(*d));
    d->cfg = cfg;
    d->unit = cfg->drive_unit;
}

void drive_reset(Drive *d) {
    d->unit = d->cfg->drive_unit;
}

int drive_load_rom(Drive *d, const char *dir) {
    if (!dir || !dir[0]) return -1;
    char path[1024];
    snprintf(path, sizeof(path), "%s/dos1571.bin", dir);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(d->rom, 1, DRIVE_ROM_SIZE, f);
    fclose(f);
    if (n < 0x4000) return -1;   /* 1571 ROM is 32 KB */
    d->rom_loaded = true;
    return 0;
}

int drive_attach_disk(Drive *d, const char *path) {
    if (d->disk_attached) {
        d64_close(&d->d64);
        d->disk_attached = false;
    }
    if (!path || !path[0]) return 0;
    if (d64_open(&d->d64, path) != 0) return -1;
    d->disk_attached = true;
    return 0;
}

int drive_directory(const Drive *d, char *out, size_t cap) {
    if (!d->disk_attached) return 0;
    out[0] = '\0';
    return d64_read_directory(&d->d64, out, cap);
}
