#include "drive.h"
#include "leds.h"
#include <string.h>

void drive_init(Drive *d, Config *cfg) {
    memset(d, 0, sizeof(*d));
    d->cfg = cfg;
    d->unit = cfg->drive_unit;
    virtual_drive_init(&d->virtual_drive, d->unit);
}

void drive_reset(Drive *d) {
    d->unit = d->cfg->drive_unit;
    virtual_drive_set_unit(&d->virtual_drive, d->unit);
    virtual_drive_reset(&d->virtual_drive);
}

int drive_attach_disk(Drive *d, const char *path) {
    virtual_drive_attach(&d->virtual_drive, NULL);
    if (d->disk_attached) {
        disk_image_close(&d->image);
        d->disk_attached = false;
    }
    if (!path || !path[0]) return 0;
    if (disk_image_open(&d->image, path) != 0) return -1;
    d->disk_attached = true;
    virtual_drive_attach(&d->virtual_drive, &d->image);
    return 0;
}

void drive_set_unit(Drive *d, int unit) {
    d->unit = unit;
    virtual_drive_set_unit(&d->virtual_drive, unit);
}

void drive_attention(Drive *d, u8 byte) {
    virtual_drive_attention(&d->virtual_drive, byte);
}

void drive_send(Drive *d, u8 byte) {
    virtual_drive_send(&d->virtual_drive, byte);
}

int drive_receive(Drive *d, u8 *byte) {
    int status = virtual_drive_receive(&d->virtual_drive, byte);
    if (status) leds_ping(LED_FDC_A);
    return status;
}

u8 drive_take_bus_status(Drive *d) {
    return virtual_drive_take_bus_status(&d->virtual_drive);
}
