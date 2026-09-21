#include "drive.h"
#include "leds.h"
#include <string.h>

static LedId drive_led(const Drive *d) {
    return d->slot == 1 ? LED_FDC_B : LED_FDC_A;
}

static void drive_activity(const Drive *d) {
    leds_ping(drive_led(d));
}

void drive_init(Drive *d, Config *cfg) {
    memset(d, 0, sizeof(*d));
    d->cfg = cfg;
    d->unit = cfg->drive_unit;
    virtual_drive_init(&d->virtual_drive, d->unit);
    leds_set_drive_unit(drive_led(d), d->unit);
}

void drive_reset(Drive *d) {
    d->unit = d->cfg->drive_unit;
    virtual_drive_set_unit(&d->virtual_drive, d->unit);
    virtual_drive_reset(&d->virtual_drive);
    leds_set_drive_unit(drive_led(d), d->unit);
}

int drive_attach_disk(Drive *d, const char *path) {
    bool had_disk = d->disk_attached;
    virtual_drive_attach(&d->virtual_drive, NULL);
    if (d->disk_attached) {
        disk_image_close(&d->image);
        d->disk_attached = false;
    }
    if (!path || !path[0]) {
        if (had_disk) drive_activity(d);
        return 0;
    }
    if (disk_image_open(&d->image, path) != 0) {
        if (had_disk) drive_activity(d);
        return -1;
    }
    d->disk_attached = true;
    virtual_drive_attach(&d->virtual_drive, &d->image);
    drive_activity(d);
    return 0;
}

void drive_set_unit(Drive *d, int unit) {
    d->unit = unit;
    virtual_drive_set_unit(&d->virtual_drive, unit);
    leds_set_drive_unit(drive_led(d), unit);
}

void drive_set_slot(Drive *d, unsigned slot) {
    d->slot = slot == 1 ? 1 : 0;
    leds_set_drive_unit(drive_led(d), d->unit);
}

void drive_attention(Drive *d, u8 byte) {
    /* cfg->real_disk_drive is a saved request for the future cycle-level
     * backend. Until that backend exists, keep the working virtual IEC path
     * connected even when the Advanced overlay preference is on. */
    bool was_addressed = d->virtual_drive.addressed;
    virtual_drive_attention(&d->virtual_drive, byte);
    if (was_addressed || d->virtual_drive.addressed) drive_activity(d);
}

void drive_send(Drive *d, u8 byte) {
    bool accepting = d->virtual_drive.addressed && d->virtual_drive.listening &&
                     d->virtual_drive.write_mode != VDRIVE_WRITE_NONE;
    virtual_drive_send(&d->virtual_drive, byte);
    if (accepting) drive_activity(d);
}

int drive_receive(Drive *d, u8 *byte) {
    int status = virtual_drive_receive(&d->virtual_drive, byte);
    if (status > 0) drive_activity(d);
    return status;
}

u8 drive_take_bus_status(Drive *d) {
    return virtual_drive_take_bus_status(&d->virtual_drive);
}

void drive_pair_attention(Drive *first, Drive *second, bool second_enabled, u8 byte) {
    drive_attention(first, byte);
    if (second_enabled) drive_attention(second, byte);
}

void drive_pair_send(Drive *first, Drive *second, bool second_enabled, u8 byte) {
    drive_send(first, byte);
    if (second_enabled) drive_send(second, byte);
}

int drive_pair_receive(Drive *first, Drive *second, bool second_enabled, u8 *byte) {
    int status = drive_receive(first, byte);
    if (!status && second_enabled) status = drive_receive(second, byte);
    return status;
}

u8 drive_pair_take_bus_status(Drive *first, Drive *second, bool second_enabled) {
    u8 status = drive_take_bus_status(first);
    if (second_enabled) status |= drive_take_bus_status(second);
    return status;
}
