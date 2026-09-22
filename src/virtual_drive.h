#pragma once
#include "types.h"
#include "disk_image.h"
#include <stdbool.h>
#include <stddef.h>

/* Fast, command-level IEC device used by the KERNAL ROM traps.
 *
 * This is intentionally not a 1571 hardware model. A future true-drive
 * implementation will connect to line-level IEC signals and run its own CPU;
 * the two implementations share disk-image code only. */

#define VDRIVE_CHANNELS     16
#define VDRIVE_NAME_MAX     64
#define VDRIVE_DIRECTORY_MAX (32u + 512u * 32u + 31u)
#define VDRIVE_SAVE_MAX     (3200u * 254u)
#define VDRIVE_RAM_SIZE     0x8000u

typedef enum {
    VDRIVE_WRITE_NONE = 0,
    VDRIVE_WRITE_OPEN,
    VDRIVE_WRITE_DATA,
} VirtualDriveWriteMode;

typedef struct {
    int unit;
    DiskImage *disk;

    bool addressed;
    bool listening;
    bool talking;
    u8 secondary;
    VirtualDriveWriteMode write_mode;

    bool channel_open[VDRIVE_CHANNELS];
    bool channel_save[VDRIVE_CHANNELS];
    bool channel_direct[VDRIVE_CHANNELS];
    u8 block_buffer[VDRIVE_CHANNELS][DISK_SECTOR_BYTES];
    unsigned block_pos[VDRIVE_CHANNELS];
    unsigned block_limit[VDRIVE_CHANNELS];
    char channel_name[VDRIVE_CHANNELS][VDRIVE_NAME_MAX];
    u8 *channel_data[VDRIVE_CHANNELS];
    size_t channel_len[VDRIVE_CHANNELS];
    size_t channel_cap[VDRIVE_CHANNELS];
    bool channel_overflow[VDRIVE_CHANNELS];

    u8 ram[VDRIVE_RAM_SIZE];
    u8 memory_read[DISK_SECTOR_BYTES];
    size_t memory_read_len;
    bool memory_read_pending;

    u8 write_buf[512];
    size_t write_len;
    bool write_overflow;
    u8 *response;
    size_t response_cap;
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
void virtual_drive_attach(VirtualDrive *v, DiskImage *disk);

void virtual_drive_attention(VirtualDrive *v, u8 byte);
void virtual_drive_send(VirtualDrive *v, u8 byte);
int  virtual_drive_receive(VirtualDrive *v, u8 *byte);
u8   virtual_drive_take_bus_status(VirtualDrive *v);
