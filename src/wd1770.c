#include "wd1770.h"
#include <string.h>

/* Hardware bindings and command timing checked against VICE 3.10's
 * drive/iec/{wd1770,fdd}.c. Sector storage stays in our DiskImage layer.
 * A D81 cylinder contains 2 heads x 10 physical 512-byte sectors, exposed
 * to CBM DOS as forty 256-byte blocks. VICE's 1581 head polarity is inverted. */
enum { IDLE, PREPARE, STEP, VERIFY, SEARCH, TRANSFER, CRC_END };
static const unsigned step_cycles[4] = {12000, 24000, 40000, 60000};

static bool ready(const Wd1770 *f) { return f->image && f->motor; }

static void finish(Wd1770 *f, u8 error) {
    f->status = (u8)((f->status & ~WD1770_BUSY) | error);
    f->phase = IDLE;
    f->idle_cycles = 0;
    f->irq = true;
}

void wd1770_init(Wd1770 *f) {
    memset(f, 0, sizeof(*f));
    f->direction = -1;
    f->type1 = true;
    f->disk_changed = true;
}

void wd1770_reset(Wd1770 *f) {
    const DiskImage *image = f->image;
    unsigned head = f->head_track, side = f->side;
    bool changed = f->disk_changed;
    wd1770_init(f);
    f->image = image;
    f->head_track = head;
    f->side = side;
    f->disk_changed = changed;
}

bool wd1770_attach(Wd1770 *f, const DiskImage *image) {
    if (image && (image->format != DISK_FORMAT_D81 || !image->data ||
                  image->tracks != 80 || image->size != 819200)) return false;
    /* Eject/insert aborts an in-flight read; no buffered bytes may leak from
     * the previous disk after its owner frees it. */
    if (f->phase != IDLE) finish(f, WD1770_RNF);
    f->status &= (u8)~WD1770_DRQ;
    f->image = image;
    f->disk_changed = true;
    f->position = f->length = 0;
    return true;
}

void wd1770_set_motor(Wd1770 *f, bool on) {
    f->motor = on;
    if (!on && f->phase != IDLE && !f->type1) {
        f->status &= (u8)~WD1770_DRQ;
        finish(f, WD1770_RNF);
    }
}

void wd1770_set_side(Wd1770 *f, unsigned side) {
    side &= 1;
    if (f->side != side && (f->phase == TRANSFER || f->phase == CRC_END)) {
        f->status &= (u8)~WD1770_DRQ;
        finish(f, WD1770_RNF);
    }
    f->side = side;
}

static u8 status(const Wd1770 *f) {
    u8 value = f->status;
    if (f->type1) {
        value &= (u8)~(WD1770_DRQ | WD1770_LOST);
        value |= WD1770_WP;
        if (f->head_track == 0) value |= 0x04; /* track-zero sensor */
        if (ready(f) && f->rotation < 4000) value |= 0x02; /* index */
    }
    return value;
}

u8 wd1770_read(Wd1770 *f, u16 addr) {
    switch (addr & 3) {
        case 0: f->irq = false; return status(f);
        case 1: return f->track;
        case 2: return f->sector;
        default: f->status &= (u8)~WD1770_DRQ; return f->data;
    }
}

void wd1770_write(Wd1770 *f, u16 addr, u8 value) {
    switch (addr & 3) {
        case 1: f->track = value; return;
        case 2: f->sector = value; return;
        case 3: f->data = value; f->status &= (u8)~WD1770_DRQ; return;
        default: break;
    }
    if ((value & 0xf0) == 0xd0) {
        /* I3 immediate / I2 next index. Ready-transition interrupt inputs
         * (I0/I1) are unused on the WD1770 and not synthesized here. */
        f->command = value;
        f->phase = IDLE;
        f->idle_cycles = 0;
        f->status &= (u8)~(WD1770_BUSY | WD1770_DRQ);
        f->type1 = true;
        f->irq = (value & 0x08) != 0;
        return;
    }
    if (f->phase != IDLE) return; /* only FORCE interrupts a busy command */
    bool spinning = (f->status & WD1770_MOTOR) != 0;
    f->command = value;
    f->type1 = !(value & 0x80);
    f->irq = false;
    f->status = WD1770_BUSY | WD1770_MOTOR;
    f->position = f->length = 0;
    f->phase = PREPARE;
    f->delay = 48 + ((!spinning && !(value & 8)) ? 6 * WD1770_REV_CYCLES : 0);
    if (!f->type1 && (value & 4)) f->delay += 60000; /* head settling */
}

static void search(Wd1770 *f) {
    f->phase = SEARCH;
    unsigned sector = f->sector;
    if ((f->command & 0xf0) == 0xc0)
        sector = f->rotation / (WD1770_REV_CYCLES / 10) + 1;
    if (!ready(f) || f->head_track >= 80 ||
        ((f->command & 0xf0) != 0xc0 && f->track != f->head_track) ||
        sector < 1 || sector > 10) {
        f->delay = 5 * WD1770_REV_CYCLES;
    } else {
        /* Regular, evenly spaced D81 sectors. Raw gaps, interleave and
         * missing-address/CRC tracks need a future MFM stream backend. */
        unsigned target = (sector - 1) * (WD1770_REV_CYCLES / 10) + 4096;
        f->delay = (target + WD1770_REV_CYCLES - f->rotation) % WD1770_REV_CYCLES;
        if (!f->delay) f->delay = WD1770_REV_CYCLES;
    }
}

static u16 crc_byte(u16 crc, u8 value) {
    crc ^= (u16)value << 8;
    for (int bit = 0; bit < 8; ++bit)
        crc = (u16)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    return crc;
}

static void event(Wd1770 *f) {
    switch (f->phase) {
        case PREPARE:
            if (f->type1) {
                f->status |= WD1770_SPINUP;
                if ((f->command & 0xf0) == 0x00) {
                    f->direction = -1;
                    f->steps = f->head_track;
                    f->track = (u8)f->head_track;
                } else if ((f->command & 0xf0) == 0x10) {
                    f->direction = f->data >= f->track ? 1 : -1;
                    f->steps = f->data >= f->track ? f->data - f->track :
                                                               f->track - f->data;
                } else {
                    if ((f->command & 0xe0) == 0x40) f->direction = 1;
                    if ((f->command & 0xe0) == 0x60) f->direction = -1;
                    f->steps = 1;
                }
                f->phase = STEP;
                f->delay = f->steps ? step_cycles[f->command & 3] : 1;
            } else if ((f->command & 0xe0) == 0xa0 ||
                       (f->command & 0xf0) == 0xf0) {
                finish(f, WD1770_WP); /* deliberate read-only first slice */
            } else if ((f->command & 0xf0) == 0xe0) {
                finish(f, WD1770_RNF); /* raw track read not implemented */
            } else search(f);
            break;
        case STEP:
            if (f->steps) {
                if (f->direction > 0 && f->head_track < 83) f->head_track++;
                if (f->direction < 0 && f->head_track) f->head_track--;
                if (f->command < 0x20 || (f->command & 0x10))
                    f->track = (u8)(f->track + f->direction);
                if (f->direction < 0 && !f->head_track) f->track = 0;
                f->disk_changed = !f->image;
                f->head_steps++;
                f->steps--;
            }
            if (f->steps) f->delay = step_cycles[f->command & 3];
            else if (f->command & 4) { f->phase = VERIFY; f->delay = 60000; }
            else finish(f, 0);
            break;
        case VERIFY:
            finish(f, ready(f) && f->head_track < 80 &&
                      f->track == f->head_track ? 0 : WD1770_RNF);
            break;
        case SEARCH: {
            bool address = (f->command & 0xf0) == 0xc0;
            if (!ready(f) || f->head_track >= 80 || (!address &&
                (f->track != f->head_track || f->sector < 1 || f->sector > 10))) {
                finish(f, WD1770_RNF);
                break;
            }
            if (address) {
                f->buffer[0] = (u8)f->head_track;
                f->buffer[1] = (u8)(f->side ^ 1);
                f->buffer[2] = (u8)(f->rotation / (WD1770_REV_CYCLES / 10) + 1);
                f->buffer[3] = 2; /* 128 << 2 = 512 bytes */
                u16 crc = 0xb230; /* MFM A1 A1 A1 FE */
                for (int i = 0; i < 4; ++i) crc = crc_byte(crc, f->buffer[i]);
                f->buffer[4] = (u8)(crc >> 8);
                f->buffer[5] = (u8)crc;
                f->sector = f->buffer[0]; /* WD read-address side effect */
                f->length = 6;
            } else {
                int block = (int)((f->side ^ 1) * 20 + (f->sector - 1) * 2);
                if (disk_image_read_sector(f->image, (int)f->head_track + 1,
                                           block, f->buffer) ||
                    disk_image_read_sector(f->image, (int)f->head_track + 1,
                                           block + 1, f->buffer + 256)) {
                    finish(f, WD1770_RNF);
                    break;
                }
                f->length = 512;
            }
            f->position = 0;
            f->phase = TRANSFER;
            f->delay = WD1770_BYTE_CYCLES;
            break;
        }
        case TRANSFER:
            if (f->status & WD1770_DRQ) f->status |= WD1770_LOST;
            f->data = f->buffer[f->position++];
            f->status |= WD1770_DRQ;
            f->read_bytes++;
            f->delay = WD1770_BYTE_CYCLES;
            if (f->position == f->length) {
                f->phase = CRC_END;
                f->delay *= 2;
            }
            break;
        case CRC_END:
            if (f->status & WD1770_DRQ) f->status |= WD1770_LOST;
            if ((f->command & 0xe0) == 0x80) {
                f->sectors_read++;
                if (f->command & 0x10) { f->sector++; search(f); break; }
            }
            finish(f, 0);
            break;
        default: break;
    }
}

void wd1770_tick(Wd1770 *f, unsigned cycles) {
    while (cycles) {
        unsigned n = f->phase == IDLE || cycles < f->delay ? cycles : f->delay;
        f->cycles += n;
        if (ready(f)) {
            u64 rotation = (u64)f->rotation + n;
            if (rotation >= WD1770_REV_CYCLES && f->phase == IDLE &&
                (f->command & 0xf4) == 0xd4) f->irq = true;
            f->rotation = (unsigned)(rotation % WD1770_REV_CYCLES);
        }
        cycles -= n;
        if (f->phase == IDLE) {
            if (n >= 10 * WD1770_REV_CYCLES - f->idle_cycles) {
                f->status &= (u8)~WD1770_MOTOR;
                f->idle_cycles = 10 * WD1770_REV_CYCLES;
            } else f->idle_cycles += n;
        } else {
            f->delay -= n;
            if (!f->delay) event(f);
        }
    }
}
