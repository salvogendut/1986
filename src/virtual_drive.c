#include "virtual_drive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IEC_LISTEN       0x20
#define IEC_UNLISTEN     0x3f
#define IEC_TALK         0x40
#define IEC_UNTALK       0x5f
#define IEC_SECONDARY    0x60
#define IEC_CLOSE        0xe0
#define IEC_OPEN         0xf0

static bool trace_enabled(void) {
    static int enabled = -1;
    if (enabled < 0) enabled = getenv("C128_IEC_TRACE") != NULL;
    return enabled != 0;
}

static void set_status(VirtualDrive *v, const char *status) {
    snprintf(v->status, sizeof(v->status), "%s", status);
}

void virtual_drive_init(VirtualDrive *v, int unit) {
    memset(v, 0, sizeof(*v));
    v->unit = unit;
    v->response_channel = -1;
    set_status(v, "00, OK,00,00\r");
}

void virtual_drive_reset(VirtualDrive *v) {
    int unit = v->unit;
    const D64 *disk = v->disk;
    virtual_drive_init(v, unit);
    v->disk = disk;
}

void virtual_drive_set_unit(VirtualDrive *v, int unit) {
    v->unit = unit;
}

void virtual_drive_attach(VirtualDrive *v, const D64 *disk) {
    v->disk = disk;
    v->response_len = v->response_pos = 0;
    v->response_channel = -1;
    if (disk) set_status(v, "00, OK,00,00\r");
    else set_status(v, "74,DRIVE NOT READY,00,00\r");
}

static void prepare_channel(VirtualDrive *v, unsigned channel) {
    v->response_len = v->response_pos = 0;
    v->response_channel = (int)channel;
    v->response_error = false;

    if (!v->channel_open[channel]) return;

    const char *name = v->channel_name[channel];
    /* Direct-access buffer channels (OPEN "#") are used by C128 startup and
     * burst negotiation. They carry U1/U2 commands, not a disk filename. */
    if (name[0] == '#') {
        set_status(v, "00, OK,00,00\r");
        return;
    }
    if (name[0] == '$') {
        if (v->disk) {
            v->response_len = d64_build_directory_program(
                v->disk, v->response, sizeof(v->response));
            if (v->response_len) {
                set_status(v, "00, OK,00,00\r");
            } else {
                set_status(v, "71,DIR ERROR,00,00\r");
                v->response_error = true;
                v->bus_status |= 0x02;
            }
        } else {
            set_status(v, "74,DRIVE NOT READY,00,00\r");
            v->response_error = true;
            v->bus_status |= 0x02;
        }
        return;
    }

    if (!v->disk) {
        set_status(v, "74,DRIVE NOT READY,00,00\r");
        v->response_error = true;
        v->bus_status |= 0x02;
        return;
    }

    D64DirEntry entry;
    if (d64_find_file(v->disk, name, &entry) != 0) {
        set_status(v, "62,FILE NOT FOUND,00,00\r");
        v->response_error = true;
        v->bus_status |= 0x02;
        return;
    }
    int length = d64_read_file(v->disk, &entry, v->response,
                               sizeof(v->response));
    if (length < 0) {
        set_status(v, "27,READ ERROR,00,00\r");
        v->response_error = true;
        v->bus_status |= 0x02;
        return;
    }
    v->response_len = (size_t)length;
    set_status(v, "00, OK,00,00\r");
}

static void finish_write(VirtualDrive *v) {
    if (!v->addressed || !v->listening) return;

    unsigned channel = v->secondary & 0x0f;
    if (v->write_mode == VDRIVE_WRITE_OPEN) {
        size_t n = v->write_len;
        if (n >= VDRIVE_NAME_MAX) n = VDRIVE_NAME_MAX - 1;
        memcpy(v->channel_name[channel], v->write_buf, n);
        v->channel_name[channel][n] = '\0';
        v->channel_open[channel] = true;
        if (channel != 15) prepare_channel(v, channel);
    } else if (v->write_mode == VDRIVE_WRITE_DATA && channel == 15) {
        /* The command channel is intentionally small for now. I initializes
         * virtual DOS state; U1/U2 commands used by C128 startup are accepted
         * without pretending that a true 1571 mechanism exists. */
        if (v->write_len > 0 &&
            (v->write_buf[0] == 'I' || v->write_buf[0] == 'i' ||
             v->write_buf[0] == 'U' || v->write_buf[0] == 'u'))
            set_status(v, "00, OK,00,00\r");
    }
    v->write_len = 0;
    v->write_mode = VDRIVE_WRITE_NONE;
}

static void prepare_talk(VirtualDrive *v) {
    unsigned channel = v->secondary & 0x0f;

    if (channel == 15) {
        v->response_len = v->response_pos = 0;
        v->response_channel = 15;
        v->response_error = false;
        v->response_len = strlen(v->status);
        memcpy(v->response, v->status, v->response_len);
        return;
    }
    if (v->response_channel != (int)channel) prepare_channel(v, channel);
    v->response_pos = 0;
}

void virtual_drive_attention(VirtualDrive *v, u8 byte) {
    if (trace_enabled()) fprintf(stderr, "[IEC:A] %02X\n", byte);

    if (byte == IEC_UNLISTEN) {
        finish_write(v);
        v->listening = false;
        v->addressed = false;
        return;
    }
    if (byte == IEC_UNTALK) {
        v->talking = false;
        v->addressed = false;
        return;
    }

    switch (byte & 0xf0) {
        case IEC_LISTEN:
            v->addressed = (byte & 0x0f) == v->unit;
            v->listening = v->addressed;
            v->talking = false;
            v->secondary = 0;
            v->write_mode = VDRIVE_WRITE_NONE;
            v->write_len = 0;
            break;
        case IEC_TALK:
            v->addressed = (byte & 0x0f) == v->unit;
            v->talking = v->addressed;
            v->listening = false;
            v->secondary = 0;
            break;
        case IEC_OPEN:
            if (v->addressed && v->listening) {
                v->secondary = byte & 0x0f;
                v->write_mode = VDRIVE_WRITE_OPEN;
                v->write_len = 0;
            }
            break;
        case IEC_CLOSE:
            if (v->addressed && v->listening) {
                unsigned channel = byte & 0x0f;
                v->channel_open[channel] = false;
                v->channel_name[channel][0] = '\0';
                if (v->response_channel == (int)channel) {
                    v->response_len = v->response_pos = 0;
                    v->response_channel = -1;
                    v->response_error = false;
                }
            }
            break;
        case IEC_SECONDARY:
            if (v->addressed) {
                v->secondary = byte & 0x0f;
                if (v->listening) {
                    v->write_mode = VDRIVE_WRITE_DATA;
                    v->write_len = 0;
                } else if (v->talking) {
                    prepare_talk(v);
                }
            }
            break;
        default:
            break;
    }
}

void virtual_drive_send(VirtualDrive *v, u8 byte) {
    if (trace_enabled())
        fprintf(stderr, "[IEC:S] %02X '%c'\n", byte,
                byte >= 0x20 && byte < 0x7f ? byte : ' ');
    if (!v->addressed || !v->listening || v->write_mode == VDRIVE_WRITE_NONE)
        return;
    if (v->write_len < sizeof(v->write_buf))
        v->write_buf[v->write_len++] = byte;
}

int virtual_drive_receive(VirtualDrive *v, u8 *byte) {
    if (v->addressed && v->talking && v->response_error) {
        if (trace_enabled()) fprintf(stderr, "[IEC:R] serial error\n");
        return -1;
    }
    if (!v->addressed || !v->talking || v->response_pos >= v->response_len)
        return 0;
    *byte = v->response[v->response_pos++];
    if (trace_enabled() && v->response_pos == v->response_len)
        fprintf(stderr, "[IEC:R] EOI after %zu bytes\n", v->response_len);
    return v->response_pos == v->response_len ? 2 : 1;
}

u8 virtual_drive_take_bus_status(VirtualDrive *v) {
    u8 status = v->bus_status;
    v->bus_status = 0;
    return status;
}
