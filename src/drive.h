#pragma once
#include "types.h"
#include "config.h"
#include "disk_image.h"
#include "virtual_drive.h"
#include <stdbool.h>
#include <stddef.h>

/* Machine-facing drive holder.
 *
 * VirtualDrive is a fast logical IEC device used by the ROM traps. It is not
 * a hardware abstraction for the future true 1571, which will live beside it
 * and connect through physical IEC lines.
 */

typedef struct Drive {
    Config *cfg;
    int     unit;       /* IEC device number (8-11) */
    VirtualDrive virtual_drive;

    DiskImage image;          /* attached D64, D71, or D81 image */
    bool    disk_attached;
} Drive;

void drive_init(Drive *d, Config *cfg);
void drive_reset(Drive *d);

/* Attach (or detach with path=NULL) a D64, D71, or D81 image. */
int  drive_attach_disk(Drive *d, const char *path);

/* Select the IEC device number. */
void drive_set_unit(Drive *d, int unit);

/* Logical IEC callbacks used by the KERNAL trap frontend. */
void drive_attention(Drive *d, u8 byte);
void drive_send(Drive *d, u8 byte);
int  drive_receive(Drive *d, u8 *byte);
u8   drive_take_bus_status(Drive *d);

/* Dispatch trapped logical IEC bytes to two independent virtual devices.
 * Only one can be addressed at a time because their units are distinct. */
void drive_pair_attention(Drive *first, Drive *second, bool second_enabled, u8 byte);
void drive_pair_send(Drive *first, Drive *second, bool second_enabled, u8 byte);
int  drive_pair_receive(Drive *first, Drive *second, bool second_enabled, u8 *byte);
u8   drive_pair_take_bus_status(Drive *first, Drive *second, bool second_enabled);
