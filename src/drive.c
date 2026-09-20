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

/* ------------------------------------------------------------------------- */
/* Minimal "smart" drive backend.                                            */

typedef struct {
    u8   device, secondary;
    int  listening, talking;     /* the drive is listening / talking */
    int  cmd_len;
    u8   cmd[256];
    int  cmd_done;               /* a complete command (0x0D) was received */
    u8   resp[65536];            /* the prepared response (directory/file) */
    int  resp_len, resp_pos;
    int  status_pos;             /* position in the 2-byte status response */
    int  opened;                 /* a data channel is open */
    char filename[64];           /* the requested filename */
    int  want_dir;               /* the "$" directory was requested */
} SmartDrive;

/* CBM DOS file-type name (indexed by the low 3 bits of the type byte). */
static const char *const k_filetype[8] = {
    "DEL", "SEQ", "PRG", "USR", "REL", "CBM", "DIR", "???"
};

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

/* Emit a CBM DOS directory listing as a raw tokenized BASIC program, exactly
 * the way VICE's vdrive-dir.c does. The C128's DIRECTORY command OPENs the
 * "$" channel and reads these bytes via CHRIN, printing each one with CHROUT
 * (which writes to whichever screen is active, 40-col VIC or 80-col VDC). It
 * does NOT load-and-run the program, so the lines are ordinary BASIC lines
 * (start address, line link, line number = block count, then the text). */
static void smart_build_directory(Drive *d) {
    SmartDrive *s = d->impl;

    /* Read the raw BAM sector (track 18, sector 0) so the header matches the
     * disk exactly, like VICE. Layout: disk name at $90 (16 bytes, 0xA0
     * padded), disk ID at $A2 (2 bytes), DOS version at $A5 (2 bytes, e.g.
     * "2A"); $A4 is a 0xA0 pad that shows as a space. */
    u8 bam[256];
    u8   diskname[16] = { 0 };
    char id[2] = { '0', '0' };
    u8   dos[2] = { '2', 'A' };
    int  free_blocks = 0;
    if (d->disk_attached && d64_read_sector(&d->d64, 18, 0, bam) == 0) {
        for (int i = 0; i < 16; i++) diskname[i] = (bam[0x90 + i] == 0xA0) ? 0x20 : bam[0x90 + i];
        id[0] = bam[0xA2]; id[1] = bam[0xA3];
        dos[0] = bam[0xA5]; dos[1] = bam[0xA6];
        char tmp[17];
        d64_read_bam(&d->d64, tmp, sizeof(tmp), id, NULL, &free_blocks);
    }

    /* The directory program lives at $0401 (C64/C128 BASIC start). */
    u8 *resp = s->resp;
    int rl = 0;
    resp[rl++] = 0x01; resp[rl++] = 0x04;   /* load address $0401 */

    /* Header line: link $0101, line number 0, then RVS-on + "DISKNAME" + id. */
    resp[rl++] = 0x01; resp[rl++] = 0x01;   /* line link */
    resp[rl++] = 0x00; resp[rl++] = 0x00;   /* line number 0 */
    resp[rl++] = 0x12;                      /* reverse on */
    resp[rl++] = '"';
    for (int i = 0; i < 16; i++) resp[rl++] = diskname[i];
    resp[rl++] = '"';
    resp[rl++] = ' ';
    resp[rl++] = (u8)id[0];
    resp[rl++] = (u8)id[1];
    resp[rl++] = ' ';                       /* the $A4 0xA0 pad */
    resp[rl++] = (u8)dos[0];
    resp[rl++] = (u8)dos[1];
    resp[rl++] = 0x00;                      /* end of line */

    /* One line per directory entry. */
    D64DirEntry ents[512];
    int n = d64_read_directory_entries(&d->d64, ents, 512);
    for (int i = 0; i < n; i++) {
        D64DirEntry *e = &ents[i];
        resp[rl++] = 0x01; resp[rl++] = 0x01;        /* line link */
        resp[rl++] = (u8)(e->blocks & 0xFF);          /* line number = blocks */
        resp[rl++] = (u8)((e->blocks >> 8) & 0xFF);
        resp[rl++] = ' ';                             /* 1 space before name */
        resp[rl++] = '"';
        int len = (int)strlen(e->name);
        for (int j = 0; j < 16; j++) {
            char c = (j < len) ? e->name[j] : 0x20;
            resp[rl++] = (u8)c;
        }
        resp[rl++] = '"';
        resp[rl++] = e->closed ? ' ' : '*';           /* open-file marker */
        resp[rl++] = ' ';
        const char *ft = k_filetype[e->type & 0x07];
        resp[rl++] = (u8)ft[0];
        resp[rl++] = (u8)ft[1];
        resp[rl++] = (u8)ft[2];
        resp[rl++] = e->locked ? '<' : ' ';           /* locked marker */
        resp[rl++] = 0x00;                            /* end of line */
    }

    /* Trailing "BLOCKS FREE." line. */
    resp[rl++] = 0x01; resp[rl++] = 0x01;   /* line link */
    resp[rl++] = (u8)(free_blocks & 0xFF);   /* line number = free blocks */
    resp[rl++] = (u8)((free_blocks >> 8) & 0xFF);
    resp[rl++] = ' ';
    const char *bf = "BLOCKS FREE.";
    while (*bf) resp[rl++] = (u8)*bf++;
    resp[rl++] = 0x00;

    s->resp_len = rl;
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
        /* Command channel: the directory listing is read here (a directory was
         * built), otherwise the status (0x00 0x00) is returned. */
        if (s->resp_pos < s->resp_len) {
            *byte = s->resp[s->resp_pos++];
            return (s->resp_pos == s->resp_len) ? 2 : 1;   /* last byte = EOI */
        }
        if (s->status_pos < 2) { *byte = 0x00; s->status_pos++; return 1; }
        return 0;
    }
    if (s->resp_pos < s->resp_len) {
        *byte = s->resp[s->resp_pos++];
        return (s->resp_pos == s->resp_len) ? 2 : 1;       /* last byte = EOI */
    }
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
