#pragma once
#include "types.h"
#include "d64.h"
#include <stdbool.h>
#include <stddef.h>

/* Fast, command-level IEC device used by the KERNAL ROM traps.
 *
 * This is intentionally not a 1571 hardware model. A future true-drive
 * implementation will connect to line-level IEC signals and run its own CPU;
 * the two implementations share disk-image code only. */

#define VDRIVE_CHANNELS     16
#define VDRIVE_NAME_MAX     64
#define VDRIVE_RESPONSE_MAX 65536

typedef enum {
    VDRIVE_WRITE_NONE = 0,
    VDRIVE_WRITE_OPEN,
    VDRIVE_WRITE_DATA,
} VirtualDriveWriteMode;

typedef struct {
    int unit;
    const D64 *disk;

    bool addressed;
    bool listening;
    bool talking;
    u8 secondary;
    VirtualDriveWriteMode write_mode;

    bool channel_open[VDRIVE_CHANNELS];
    char channel_name[VDRIVE_CHANNELS][VDRIVE_NAME_MAX];

    u8 write_buf[256];
    size_t write_len;
    u8 response[VDRIVE_RESPONSE_MAX];
    size_t response_len;
    size_t response_pos;
    int response_channel;
    bool response_error;
    u8 bus_status;
    char status[64];
} VirtualDrive;

void virtual_drive_init(VirtualDrive *v, int unit);
void virtual_drive_reset(VirtualDrive *v);
void virtual_drive_set_unit(VirtualDrive *v, int unit);
void virtual_drive_attach(VirtualDrive *v, const D64 *disk);

void virtual_drive_attention(VirtualDrive *v, u8 byte);
void virtual_drive_send(VirtualDrive *v, u8 byte);
int  virtual_drive_receive(VirtualDrive *v, u8 *byte);
u8   virtual_drive_take_bus_status(VirtualDrive *v);
