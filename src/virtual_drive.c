#include "virtual_drive.h"
#include <ctype.h>
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

static void discard_channels(VirtualDrive *v) {
    for (int i = 0; i < VDRIVE_CHANNELS; ++i) {
        free(v->channel_data[i]);
        v->channel_data[i] = NULL;
    }
    free(v->response);
    v->response = NULL;
}

static bool reserve_response(VirtualDrive *v, size_t cap) {
    if (cap <= v->response_cap) return true;
    u8 *buffer = realloc(v->response, cap);
    if (!buffer) return false;
    v->response = buffer;
    v->response_cap = cap;
    return true;
}

static void close_channel(VirtualDrive *v, unsigned channel) {
    free(v->channel_data[channel]);
    v->channel_data[channel] = NULL;
    v->channel_len[channel] = v->channel_cap[channel] = 0;
    v->channel_open[channel] = v->channel_save[channel] = false;
    v->channel_direct[channel] = false;
    memset(v->block_buffer[channel], 0, DISK_SECTOR_BYTES);
    v->block_pos[channel] = v->block_limit[channel] = 0;
    v->channel_overflow[channel] = false;
    v->channel_name[channel][0] = '\0';
}

static void save_error(VirtualDrive *v, DiskSaveResult result) {
    switch (result) {
        case DISK_SAVE_OK: set_status(v, "00, OK,00,00\r"); return;
        case DISK_SAVE_EXISTS: set_status(v, "63,FILE EXISTS,00,00\r"); break;
        case DISK_SAVE_TYPE_MISMATCH:
            set_status(v, "64,FILE TYPE MISMATCH,00,00\r"); break;
        case DISK_SAVE_DOS_MISMATCH:
            set_status(v, "73,DOS MISMATCH,00,00\r"); break;
        case DISK_SAVE_DISK_FULL: set_status(v, "72,DISK FULL,00,00\r"); break;
        case DISK_SAVE_DIR_ERROR: set_status(v, "71,DIR ERROR,00,00\r"); break;
        case DISK_SAVE_WRITE_PROTECT:
            set_status(v, "26,WRITE PROTECT ON,00,00\r"); break;
        case DISK_SAVE_BAD_NAME: set_status(v, "33,SYNTAX ERROR,00,00\r"); break;
        case DISK_SAVE_NOT_FOUND:
            set_status(v, "62,FILE NOT FOUND,00,00\r"); break;
        case DISK_SAVE_IO_ERROR: set_status(v, "25,WRITE ERROR,00,00\r"); break;
    }
    v->bus_status |= 0x02;
}

static bool save_name(const char *raw, char name[17], bool *replace) {
    const char *p = raw;
    *replace = false;
    if (*p == '@') { *replace = true; ++p; }
    if (p[0] >= '0' && p[0] <= '9' && p[1] == ':') p += 2;
    else if (*p == ':') ++p;
    size_t n = 0;
    while (*p && *p != ',') {
        if (n >= 16) return false;
        name[n++] = *p++;
    }
    while (n && name[n - 1] == ' ') --n;
    name[n] = '\0';
    return n > 0;
}

static bool command_name(const char *raw, char name[17], bool require_prefix) {
    if (*raw >= '0' && *raw <= '9' && raw[1] == ':') raw += 2;
    else if (*raw == ':') ++raw;
    else if (require_prefix) return false;
    size_t n = strlen(raw);
    if (n == 0 || n > 16) return false;
    memcpy(name, raw, n + 1);
    return true;
}

static void command_error(VirtualDrive *v, int code, const char *message,
                          int track, int sector) {
    snprintf(v->status, sizeof(v->status), "%02d,%s,%02d,%02d\r",
             code, message, track, sector);
    v->bus_status |= 0x02;
}

/* VICE's vdrive accepts either commas or spaces between decimal block-command
 * arguments, with an optional colon after the command. Reject partial and
 * overflowing commands instead of silently reporting OK. */
static bool block_parameters(const char *p, int *values, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        while (*p == ' ' || *p == ',' || *p == ':' || *p == '#' ||
               *p == ')' || *p == 0x1d) ++p;
        if (!isdigit((unsigned char)*p)) return false;
        int value = 0;
        do {
            value = value * 10 + (*p++ - '0');
            if (value > 255) return false;
        } while (isdigit((unsigned char)*p));
        values[i] = value;
    }
    while (*p == ' ' || *p == ',' || *p == ':' || *p == 0x1d) ++p;
    return *p == '\0';
}

static void execute_block_command(VirtualDrive *v, const char *command) {
    bool old_style = toupper((unsigned char)command[0]) == 'B';
    char op = (char)toupper((unsigned char)command[old_style ? 2 : 1]);
    bool position = old_style && op == 'P';
    bool read = op == '1' || op == 'A' || (old_style && op == 'R');
    bool write = op == '2' || (old_style && op == 'W') ||
                 (!old_style && op == 'B');
    int args[4] = {0};
    if ((!read && !write && !position) ||
        !block_parameters(command + (old_style ? 3 : 2), args,
                          position ? 2 : 4)) {
        command_error(v, 31, "SYNTAX ERROR", 0, 0);
        return;
    }
    int channel = args[0];
    if (channel >= VDRIVE_CHANNELS || !v->channel_open[channel] ||
        !v->channel_direct[channel]) {
        command_error(v, 70, "NO CHANNEL", 0, 0);
        return;
    }
    if (position) {
        v->block_pos[channel] = (unsigned)args[1];
        set_status(v, "00, OK,00,00\r");
        return;
    }
    int drive = args[1], track = args[2], sector = args[3];
    if (drive != 0 || !v->disk || v->disk->format == DISK_FORMAT_PRG) {
        command_error(v, 74, "DRIVE NOT READY", 0, 0);
        return;
    }
    if (track < 1 || sector >= disk_image_track_sectors(v->disk, track)) {
        command_error(v, 66, "ILLEGAL TRACK OR SECTOR", track, sector);
        return;
    }
    if (read) {
        if (disk_image_read_sector(v->disk, track, sector,
                                   v->block_buffer[channel]) != 0) {
            command_error(v, 21, "READ ERROR", track, sector);
            return;
        }
        v->block_pos[channel] = old_style ? 1u : 0u;
        v->block_limit[channel] = old_style
            ? (unsigned)v->block_buffer[channel][0] + 1u
            : DISK_SECTOR_BYTES;
        if (v->block_limit[channel] < 2) v->block_limit[channel] = 2;
    } else {
        if (!v->disk->writable) {
            command_error(v, 26, "WRITE PROTECT ON", track, sector);
            return;
        }
        if (old_style) {
            unsigned length = v->block_pos[channel] > 1
                ? v->block_pos[channel] - 1u : 1u;
            v->block_buffer[channel][0] = (u8)length;
        }
        DiskSaveResult result = disk_image_write_sector(v->disk, track,
            sector, v->block_buffer[channel]);
        if (result != DISK_SAVE_OK) {
            command_error(v, result == DISK_SAVE_WRITE_PROTECT ? 26 : 25,
                          result == DISK_SAVE_WRITE_PROTECT
                              ? "WRITE PROTECT ON" : "WRITE ERROR",
                          track, sector);
            return;
        }
        v->block_pos[channel] = old_style ? 1u : 0u;
        v->block_limit[channel] = DISK_SECTOR_BYTES;
    }
    set_status(v, "00, OK,00,00\r");
}

static void execute_memory_command(VirtualDrive *v, const u8 *bytes,
                                   size_t length) {
    if (length < 5) {
        command_error(v, 31, "SYNTAX ERROR", 0, 0);
        return;
    }
    unsigned address = (unsigned)bytes[3] | (unsigned)bytes[4] << 8;
    switch (toupper((unsigned char)bytes[2])) {
        case 'W': {
            if (length < 6 || length - 6 < bytes[5]) {
                command_error(v, 31, "SYNTAX ERROR", 0, 0);
                return;
            }
            /* VICE's virtual drive keeps 32 KiB of drive RAM. This accepts
             * binary payloads, including NUL and CR, without executing the
             * uploaded 6502 program. */
            if (address < VDRIVE_RAM_SIZE)
                for (unsigned i = 0; i < bytes[5]; ++i)
                    v->ram[(address + i) & (VDRIVE_RAM_SIZE - 1)] = bytes[6 + i];
            set_status(v, "00, OK,00,00\r");
            return;
        }
        case 'R': {
            unsigned count = length < 6 || bytes[5] == 0
                ? (length < 6 ? 1u : DISK_SECTOR_BYTES) : bytes[5];
            for (unsigned i = 0; i < count; ++i)
                v->memory_read[i] = v->ram[(address + i) &
                                            (VDRIVE_RAM_SIZE - 1)];
            v->memory_read_len = count;
            v->memory_read_pending = true;
            set_status(v, "00, OK,00,00\r");
            return;
        }
        case 'E':
            /* Like VICE's virtual drive, M-E acknowledges the command but
             * cannot run uploaded drive code. True drive mode is required. */
            set_status(v, "00, OK,00,00\r");
            return;
        default:
            command_error(v, 31, "SYNTAX ERROR", 0, 0);
            return;
    }
}

static void execute_command(VirtualDrive *v, const u8 *bytes, size_t length,
                            bool overflow) {
    if (length == 0 && !overflow) return;
    char command[sizeof(v->write_buf) + 1];
    if (overflow || length >= sizeof(command)) {
        save_error(v, DISK_SAVE_BAD_NAME);
        return;
    }
    memcpy(command, bytes, length);
    command[length] = '\0';
    v->memory_read_pending = false;
    if (length >= 2 && toupper((unsigned char)bytes[0]) == 'M' &&
        bytes[1] == '-') {
        execute_memory_command(v, bytes, length);
        return;
    }
    while (length && (command[length - 1] == '\r' ||
                      command[length - 1] == '\n'))
        command[--length] = '\0';
    if (!length) return;
    if (memchr(bytes, 0, length)) {
        save_error(v, DISK_SAVE_BAD_NAME);
        return;
    }
    char op = (char)toupper((unsigned char)command[0]);
    if ((op == 'U' && length >= 2 &&
         strchr("12AB", toupper((unsigned char)command[1]))) ||
        (op == 'B' && length >= 3 && command[1] == '-' &&
         strchr("RWP", toupper((unsigned char)command[2])))) {
        execute_block_command(v, command);
        return;
    }
    if (op == 'I' || (op == 'U' && length == 2 &&
                      (command[1] == '0' || toupper((unsigned char)command[1]) == 'I'))) {
        set_status(v, "00, OK,00,00\r");
        return;
    }
    if (op == 'U') {
        command_error(v, 74, "DRIVE NOT READY", 0, 0);
        return;
    }
    if (op != 'S' && op != 'R') {
        set_status(v, "31,SYNTAX ERROR,00,00\r");
        v->bus_status |= 0x02;
        return;
    }
    if (!v->disk) {
        set_status(v, "74,DRIVE NOT READY,00,00\r");
        v->bus_status |= 0x02;
        return;
    }
    if (op == 'S') {
        char pattern[17];
        if (!command_name(command + 1, pattern, true)) {
            save_error(v, DISK_SAVE_BAD_NAME);
            return;
        }
        int removed = 0;
        DiskSaveResult result = disk_image_scratch(v->disk, pattern, &removed);
        if (result != DISK_SAVE_OK) save_error(v, result);
        else snprintf(v->status, sizeof(v->status),
                      "01,FILES SCRATCHED,%02d,00\r", removed);
    } else {
        char *equals = strchr(command + 1, '=');
        char new_name[17], old_name[17];
        if (!equals) { save_error(v, DISK_SAVE_BAD_NAME); return; }
        *equals = '\0';
        if (!command_name(command + 1, new_name, true) ||
            !command_name(equals + 1, old_name, false)) {
            save_error(v, DISK_SAVE_BAD_NAME);
            return;
        }
        save_error(v, disk_image_rename(v->disk, new_name, old_name));
    }
}

void virtual_drive_init(VirtualDrive *v, int unit) {
    memset(v, 0, sizeof(*v));
    v->unit = unit;
    v->response_channel = -1;
    set_status(v, "00, OK,00,00\r");
}

void virtual_drive_reset(VirtualDrive *v) {
    int unit = v->unit;
    DiskImage *disk = v->disk;
    discard_channels(v);
    virtual_drive_init(v, unit);
    v->disk = disk;
}

void virtual_drive_set_unit(VirtualDrive *v, int unit) {
    v->unit = unit;
}

void virtual_drive_attach(VirtualDrive *v, DiskImage *disk) {
    int unit = v->unit;
    discard_channels(v);
    virtual_drive_init(v, unit);
    v->disk = disk;
    if (disk) set_status(v, "00, OK,00,00\r");
    else set_status(v, "74,DRIVE NOT READY,00,00\r");
}

static void prepare_channel(VirtualDrive *v, unsigned channel) {
    v->response_len = v->response_pos = 0;
    v->response_channel = (int)channel;
    v->response_error = false;

    if (!v->channel_open[channel]) return;

    if (v->channel_save[channel]) {
        if (!v->disk) {
            set_status(v, "74,DRIVE NOT READY,00,00\r");
            v->bus_status |= 0x02;
        }
        else if (!v->disk->writable) save_error(v, DISK_SAVE_WRITE_PROTECT);
        else set_status(v, "00, OK,00,00\r");
        return;
    }

    const char *name = v->channel_name[channel];
    /* Direct-access buffer channels (OPEN "#") are used by C128 startup and
     * burst negotiation. They carry U1/U2 commands, not a disk filename. */
    if (name[0] == '#') {
        v->channel_direct[channel] = true;
        memset(v->block_buffer[channel], 0, DISK_SECTOR_BYTES);
        v->block_pos[channel] = 1;
        v->block_limit[channel] = DISK_SECTOR_BYTES;
        set_status(v, "00, OK,00,00\r");
        return;
    }
    if (name[0] == '$') {
        if (v->disk) {
            if (reserve_response(v, VDRIVE_DIRECTORY_MAX))
                v->response_len = disk_image_build_directory_program(
                    v->disk, v->response, v->response_cap);
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

    DiskDirEntry entry;
    if (disk_image_find_file(v->disk, name, &entry) != 0) {
        set_status(v, "62,FILE NOT FOUND,00,00\r");
        v->response_error = true;
        v->bus_status |= 0x02;
        return;
    }
    size_t max_bytes = v->disk->format == DISK_FORMAT_PRG
        ? v->disk->size
        : (size_t)disk_image_track_offset(
            v->disk, v->disk->tracks + 1) / DISK_SECTOR_BYTES * 254u;
    int length = reserve_response(v, max_bytes)
        ? disk_image_read_file(v->disk, &entry, v->response, v->response_cap)
        : -1;
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
        close_channel(v, channel);
        size_t n = v->write_len;
        if (n >= VDRIVE_NAME_MAX) n = VDRIVE_NAME_MAX - 1;
        memcpy(v->channel_name[channel], v->write_buf, n);
        v->channel_name[channel][n] = '\0';
        v->channel_open[channel] = true;
        v->channel_save[channel] = channel == 1 &&
                                   v->channel_name[channel][0] != '#';
        if (channel != 15) prepare_channel(v, channel);
        else execute_command(v, v->write_buf, v->write_len,
                             v->write_overflow);
    } else if (v->write_mode == VDRIVE_WRITE_DATA && channel == 15) {
        execute_command(v, v->write_buf, v->write_len, v->write_overflow);
    }
    v->write_len = 0;
    v->write_overflow = false;
    v->write_mode = VDRIVE_WRITE_NONE;
}

static void commit_save(VirtualDrive *v, unsigned channel) {
    char name[17];
    bool replace;
    if (v->channel_overflow[channel]) {
        save_error(v, DISK_SAVE_DISK_FULL);
    } else if (!save_name(v->channel_name[channel], name, &replace)) {
        save_error(v, DISK_SAVE_BAD_NAME);
    } else if (!v->disk) {
        set_status(v, "74,DRIVE NOT READY,00,00\r");
        v->bus_status |= 0x02;
    } else {
        DiskSaveResult result = disk_image_save_prg(v->disk, name,
            v->channel_data[channel], v->channel_len[channel], replace);
        if (trace_enabled())
            fprintf(stderr, "[IEC:SAVE] raw='%s' name='%s' replace=%d bytes=%zu result=%d\n",
                    v->channel_name[channel], name, replace,
                    v->channel_len[channel], result);
        save_error(v, result);
    }
}

static void prepare_talk(VirtualDrive *v) {
    unsigned channel = v->secondary & 0x0f;

    if (channel == 15) {
        v->response_len = v->response_pos = 0;
        v->response_channel = 15;
        v->response_error = false;
        size_t length = v->memory_read_pending ? v->memory_read_len
                                               : strlen(v->status);
        if (reserve_response(v, length)) {
            v->response_len = length;
            memcpy(v->response,
                   v->memory_read_pending ? v->memory_read
                                          : (const u8 *)v->status, length);
        }
        v->memory_read_pending = false;
        return;
    }
    if (v->channel_open[channel] && v->channel_direct[channel]) {
        v->response_channel = (int)channel;
        v->response_error = false;
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
            v->write_overflow = false;
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
                v->write_overflow = false;
            }
            break;
        case IEC_CLOSE:
            if (v->addressed && v->listening) {
                unsigned channel = byte & 0x0f;
                if (v->channel_open[channel] && v->channel_save[channel])
                    commit_save(v, channel);
                close_channel(v, channel);
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
                    v->write_overflow = false;
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
    unsigned channel = v->secondary & 0x0f;
    if (v->write_mode == VDRIVE_WRITE_DATA && v->channel_direct[channel]) {
        unsigned pos = v->block_pos[channel];
        v->block_buffer[channel][pos] = byte;
        ++pos;
        v->block_pos[channel] = pos >= v->block_limit[channel] ? 0u : pos;
        return;
    }
    if (v->write_mode == VDRIVE_WRITE_DATA && v->channel_save[channel]) {
        if (v->channel_overflow[channel]) return;
        size_t len = v->channel_len[channel];
        if (len >= VDRIVE_SAVE_MAX) {
            v->channel_overflow[channel] = true;
            return;
        }
        if (len == v->channel_cap[channel]) {
            size_t cap = v->channel_cap[channel] ? v->channel_cap[channel] * 2 : 512;
            if (cap > VDRIVE_SAVE_MAX) cap = VDRIVE_SAVE_MAX;
            u8 *data = realloc(v->channel_data[channel], cap);
            if (!data) { v->channel_overflow[channel] = true; return; }
            v->channel_data[channel] = data;
            v->channel_cap[channel] = cap;
        }
        v->channel_data[channel][len] = byte;
        v->channel_len[channel] = len + 1;
        return;
    }
    if (v->write_len < sizeof(v->write_buf))
        v->write_buf[v->write_len++] = byte;
    else v->write_overflow = true;
}

int virtual_drive_receive(VirtualDrive *v, u8 *byte) {
    if (v->addressed && v->talking && v->response_error) {
        if (trace_enabled()) fprintf(stderr, "[IEC:R] serial error\n");
        return -1;
    }
    unsigned channel = v->secondary & 0x0f;
    if (v->addressed && v->talking && v->channel_open[channel] &&
        v->channel_direct[channel]) {
        unsigned pos = v->block_pos[channel];
        if (pos >= DISK_SECTOR_BYTES) pos = 0;
        *byte = v->block_buffer[channel][pos++];
        if (pos >= v->block_limit[channel]) {
            v->block_pos[channel] = 1;
            return 2;
        }
        v->block_pos[channel] = pos;
        return 1;
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
