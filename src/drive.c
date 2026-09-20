#include "drive.h"
#include "leds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Pluggage drive interface.                                                 */

void drive_init(Drive *d, Config *cfg) {
    memset(d, 0, sizeof(*d));
    d->cfg = cfg;
    d->unit = cfg->drive_unit;
    drive_use_smart(d);
}

void drive_reset(Drive *d) {
    d->unit = d->cfg->drive_unit;
    if (d->ops && d->ops->reset) d->ops->reset(d);
}

int drive_load_rom(Drive *d, const char *dir) {
    if (!dir || !dir[0]) return -1;
    char path[1024];
    snprintf(path, sizeof(path), "%s/dos1571.bin", dir);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(d->rom, 1, DRIVE_ROM_SIZE, f);
    fclose(f);
    if (n < 0x4000) return -1;
    d->rom_loaded = true;
    return 0;
}

int drive_attach_disk(Drive *d, const char *path) {
    if (d->disk_attached) { d64_close(&d->d64); d->disk_attached = false; }
    if (!path || !path[0]) return 0;
    if (d64_open(&d->d64, path) != 0) return -1;
    d->disk_attached = true;
    return d->ops && d->ops->attach_disk ? d->ops->attach_disk(d, path) : 0;
}

void drive_set_unit(Drive *d, int unit) {
    d->unit = unit;
    if (d->ops && d->ops->set_unit) d->ops->set_unit(d, unit);
}

int drive_directory(const Drive *d, char *out, size_t cap) {
    if (!d->disk_attached) return 0;
    out[0] = '\0';
    return d64_read_directory(&d->d64, out, cap);
}

/* ------------------------------------------------------------------------- */
/* Minimal "smart" drive backend.                                            */

typedef struct {
    u8   device, secondary;
    int  listening, talking;     /* the drive is listening / talking */
    int  cmd_len;
    u8   cmd[256];
    int  cmd_done;               /* a complete command (0x0D) was received */
    u8   resp[65536];            /* the prepared response (directory PRG/file) */
    int  resp_len, resp_pos;
    int  status_pos;             /* position in the 2-byte status response */
    int  opened;                 /* a data channel is open */
    char filename[64];           /* the requested filename */
    int  want_dir;               /* the "$" directory was requested */
} SmartDrive;

static void smart_reset(Drive *d) {
    SmartDrive *s = d->impl;
    memset(s, 0, sizeof(*s));
    s->device = (u8)d->unit;
}

static int smart_attach_disk(Drive *d, const char *path) {
    (void)d; (void)path;
    return 0;
}

static void smart_set_unit(Drive *d, int unit) {
    SmartDrive *s = d->impl;
    s->device = (u8)unit;
}

/* Emit a BASIC PRINT statement that prints `text`, using CHR$(34) for any
 * literal '"' (a PRINT string literal cannot contain a quote). */
static void emit_print(u8 *resp, int *len, const char *text) {
    resp[(*len)++] = 0x98;   /* PRINT */
    const char *p = text;
    int first = 1;
    while (*p) {
        if (*p == '"') {
            if (!first) resp[(*len)++] = 0x3B;   /* ";" */
            resp[(*len)++] = 0xC6; resp[(*len)++] = 0x28;   /* CHR$ ( */
            resp[(*len)++] = 0x0B; resp[(*len)++] = 0x22;   /* number 34 */
            resp[(*len)++] = 0x29;                          /* ) */
            p++;
            first = 0;
            continue;
        }
        const char *q = p;
        while (*q && *q != '"') q++;
        if (!first) resp[(*len)++] = 0x3B;   /* ";" */
        resp[(*len)++] = 0x22;               /* '"' */
        while (p < q) resp[(*len)++] = (u8)*p++;
        resp[(*len)++] = 0x22;               /* '"' */
        first = 0;
    }
    resp[(*len)++] = 0x00;   /* end of line */
}

/* Build a BASIC program (directory listing) that loads at $0B00. Each line is
 * a PRINT statement, so RUN shows:
 *   0 "DISKNAME" 00 2A
 *   9 "FILE" PRG
 *   ...
 *   236 BLOCKS FREE.
 */
static void smart_build_directory(Drive *d) {
    SmartDrive *s = d->impl;

    char diskname[17] = "1986";
    char id[2] = { 0x00, 0x2A };
    u8   dos_type = 0x2A;
    int  free_blocks = 0;
    if (d->disk_attached)
        d64_read_bam(&d->d64, diskname, sizeof(diskname), id, &dos_type, &free_blocks);

    /* Collect the line texts (the text PRINT will emit). */
    char lines[4096][96];
    int  nlines = 0;
    snprintf(lines[nlines++], sizeof(lines[0]), "0 \"%s\" %02X %02X", diskname,
             (unsigned char)id[0], (unsigned char)id[1]);

    char out[4096];
    int  n = drive_directory(d, out, sizeof(out));
    char *p = out;
    while (*p && nlines < 500) {
        int blk = atoi(p);              /* file size in bytes */
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        char name[64]; int ni = 0;
        while (*p && *p != '\n' && ni < 60) name[ni++] = *p++;
        name[ni] = '\0';
        while (*p == '\n') p++;
        snprintf(lines[nlines++], sizeof(lines[0]), "%d \"%s\" PRG", blk / 256, name);
    }
    snprintf(lines[nlines++], sizeof(lines[0]), "%d BLOCKS FREE.", free_blocks);

    /* Load address $0B00. */
    s->resp_len = 0;
    s->resp[s->resp_len++] = 0x00;
    s->resp[s->resp_len++] = 0x0B;
    int base = 0x0B00;

    int off = 2;                        /* first line begins at base+2 */
    for (int i = 0; i < nlines; i++) {
        const char *txt = lines[i];
        /* Build the line into a temp buffer to learn its length. */
        u8 line[256];
        int ll = 0;
        line[ll++] = 0; line[ll++] = 0;               /* link (patched) */
        u16 lineno = (u16)(10 + i * 10);
        line[ll++] = (u8)(lineno & 0xFF);
        line[ll++] = (u8)(lineno >> 8);
        emit_print(line, &ll, txt);
        /* line length so far (link is 2 bytes at the start) */
        int linelen = ll;
        int next_off = off + linelen;
        u16 link = (i + 1 < nlines) ? (u16)(base + next_off) : 0x0000;
        line[0] = (u8)(link & 0xFF);
        line[1] = (u8)(link >> 8);
        for (int j = 0; j < ll; j++) s->resp[s->resp_len++] = line[j];
        off = next_off;
    }
    /* End of program. */
    s->resp[s->resp_len++] = 0x00;
    s->resp[s->resp_len++] = 0x00;
    s->resp_pos = 0;
    s->opened = 1;
}

static void smart_attention(Drive *d, u8 b) {
    SmartDrive *s = d->impl;
    if ((b & 0xF0) == 0x20) {          /* LISTEN */
        s->device = b & 0x0F;
        s->listening = 1; s->talking = 0; s->cmd_len = 0; s->cmd_done = 0;
    } else if ((b & 0xF0) == 0x40) {   /* TALK */
        s->device = b & 0x0F;
        s->talking = 1; s->listening = 0;
    } else if ((b & 0xF0) == 0x60) {   /* SECONDARY */
        s->secondary = b & 0x0F;
        if (s->talking && s->secondary == 15) s->status_pos = 0;
        else if (s->talking) { s->resp_pos = 0; s->opened = 1; }
    } else if (b == 0x3F) {            /* UNLISTEN: a command has been received */
        s->listening = 0;
        if (s->want_dir) smart_build_directory(d);
        s->cmd_len = 0; s->cmd_done = 0; s->want_dir = 0;
    } else if (b == 0x5F) {            /* UNTALK */
        s->talking = 0;
        s->resp_pos = 0;
        s->opened = 0;
    }
}

static void smart_send(Drive *d, u8 byte) {
    SmartDrive *s = d->impl;
    if (s->cmd_len < (int)sizeof(s->cmd)) {
        s->cmd[s->cmd_len++] = byte;
        if (byte == 0x0D) s->cmd_done = 1;
    }
    /* Detect a directory request: "$" in the command, or "P", or the C128
     * DOS "1:13" load-directory command. */
    if (byte == '$') s->want_dir = 1;
    if (s->cmd_len == 1 && (byte == 'P' || byte == 'p')) s->want_dir = 1;
    if (s->cmd_len == 4 && memcmp(s->cmd, "1:13", 4) == 0) s->want_dir = 1;
}

static int smart_receive(Drive *d, u8 *byte) {
    SmartDrive *s = d->impl;
    leds_ping(LED_FDC_A);   /* disk activity */
    if (s->secondary == 15) {
        /* Command channel: the directory PRG is read here (a directory was
         * built), otherwise the status (0x00 0x00) is returned. */
        if (s->resp_pos < s->resp_len) { *byte = s->resp[s->resp_pos++]; return 1; }
        if (s->status_pos < 2) { *byte = 0x00; s->status_pos++; return 1; }
        return 0;
    }
    if (s->resp_pos < s->resp_len) { *byte = s->resp[s->resp_pos++]; return 1; }
    return 0;
}

static const DriveOps g_smart_ops = {
    .reset = smart_reset,
    .attach_disk = smart_attach_disk,
    .set_unit = smart_set_unit,
    .attention = smart_attention,
    .send = smart_send,
    .receive = smart_receive,
    .unlisten = NULL,
    .untalk = NULL,
};

void drive_use_smart(Drive *d) {
    free(d->impl);
    d->impl = calloc(1, sizeof(SmartDrive));
    d->ops = &g_smart_ops;
    if (d->ops && d->ops->reset) d->ops->reset(d);
}
