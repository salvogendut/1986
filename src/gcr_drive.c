#include "gcr_drive.h"
#include <string.h>

static const u8 gcr_code[16] = {
    0x0a, 0x0b, 0x12, 0x13, 0x0e, 0x0f, 0x16, 0x17,
    0x09, 0x19, 0x1a, 0x1b, 0x0d, 0x1d, 0x1e, 0x15
};
static const unsigned track_lengths[4] = { 6250, 6666, 7142, 7692 };
static const unsigned sector_gaps[4] = { 9, 12, 17, 8 };
static const unsigned bit_rates[4] = { 250000, 266667, 285714, 307692 };

static unsigned zone_for_track(unsigned track) {
    if (track <= 17) return 3;
    if (track <= 24) return 2;
    if (track <= 30) return 1;
    return 0;
}

static void encode4(const u8 in[4], u8 out[5]) {
    u64 word = 0;
    for (int i = 0; i < 4; ++i) {
        word = (word << 5) | gcr_code[in[i] >> 4];
        word = (word << 5) | gcr_code[in[i] & 15];
    }
    for (int i = 4; i >= 0; --i) {
        out[i] = (u8)word;
        word >>= 8;
    }
}

static void build_track(GcrDrive *g) {
    unsigned physical = g->half_track / 2;
    unsigned zone = zone_for_track(physical);
    g->track_length = track_lengths[zone];
    g->byte_pos = 0;
    g->read_byte = 0x55;
    g->sync = g->byte_ready = false;
    g->bit_budget = 0;
    memset(g->track_data, 0x55, g->track_length);
    memset(g->track_sync, 0, g->track_length);

    if (!g->image || !g->image->data || (g->half_track & 1)) return;
    if (g->side && g->image->format != DISK_FORMAT_D71) return;
    unsigned image_track = physical + (g->side ? 35 : 0);
    int sectors = disk_image_track_sectors(g->image, (int)image_track);
    if (sectors <= 0) return;

    char name[17], id[2] = { (char)0xa0, (char)0xa0 };
    if (disk_image_read_bam(g->image, name, sizeof(name), id, NULL, NULL))
        return;
    unsigned header_track = image_track;
    if (g->side && g->image->format == DISK_FORMAT_D71) {
        u8 bam[256];
        if (disk_image_read_sector(g->image, 18, 0, bam)) return;
        if (!(bam[3] & 0x80)) { /* two separately formatted 1541 sides */
            if (disk_image_read_sector(g->image, 53, 0, bam)) return;
            id[0] = (char)bam[0xa2];
            id[1] = (char)bam[0xa3];
            header_track = physical;
        }
    }
    unsigned pos = 0;
    for (int sector = 0; sector < sectors; ++sector) {
        u8 raw[256];
        if (disk_image_read_sector(g->image, (int)image_track, sector, raw))
            return;
        unsigned need = 5 + 10 + 9 + 5 + 325 + sector_gaps[zone];
        if (pos + need > g->track_length) return;

        memset(g->track_data + pos, 0xff, 5);
        memset(g->track_sync + pos, 1, 5);
        pos += 5;
        u8 header[8] = {
            0x08,
            (u8)((u8)sector ^ (u8)header_track ^ (u8)id[0] ^ (u8)id[1]),
            (u8)sector, (u8)header_track,
            (u8)id[1], (u8)id[0], 0x0f, 0x0f
        };
        encode4(header, g->track_data + pos); pos += 5;
        encode4(header + 4, g->track_data + pos); pos += 5;
        pos += 9; /* header gap stays at the prefilled $55 */
        memset(g->track_data + pos, 0xff, 5);
        memset(g->track_sync + pos, 1, 5);
        pos += 5;

        u8 block[260] = { 0x07 };
        memcpy(block + 1, raw, sizeof(raw));
        for (unsigned i = 0; i < sizeof(raw); ++i) block[257] ^= raw[i];
        for (unsigned i = 0; i < sizeof(block); i += 4) {
            encode4(block + i, g->track_data + pos);
            pos += 5;
        }
        pos += sector_gaps[zone];
    }
}

void gcr_drive_init(GcrDrive *g) {
    memset(g, 0, sizeof(*g));
    g->half_track = 2;
    g->zone = 3;
    build_track(g);
}

void gcr_drive_reset(GcrDrive *g) {
    g->motor = g->led = false;
    g->step_events = g->read_events = 0;
    g->zone = zone_for_track(g->half_track / 2);
    build_track(g);
}

void gcr_drive_attach(GcrDrive *g, const DiskImage *image) {
    g->image = image && (image->format == DISK_FORMAT_D64 ||
                         image->format == DISK_FORMAT_D71) ? image : NULL;
    build_track(g);
}

void gcr_drive_set_side(GcrDrive *g, unsigned side) {
    side = !!side;
    if (g->side != side) {
        g->side = side;
        build_track(g);
    }
}

void gcr_drive_set_port_b(GcrDrive *g, u8 pins) {
    unsigned phase = pins & 3;
    bool motor = (pins & 4) != 0;
    if (motor) {
        unsigned delta = (phase - ((g->half_track - 2) & 3)) & 3;
        unsigned next = g->half_track;
        if (delta == 1 && next < 70) next++;
        if (delta == 3 && next > 2) next--;
        if (next != g->half_track) {
            g->half_track = next;
            g->step_events++;
            build_track(g);
        }
    }
    g->motor = motor;
    g->led = (pins & 8) != 0;
    g->zone = (pins >> 5) & 3;
    if (!motor) g->byte_ready = g->sync = false;
}

void gcr_drive_update_via(GcrDrive *g, Via6522 *via) {
    via6522_set_input_a(via, g->read_byte);
    u8 pins = (u8)(via->input_b & (u8)~0x90);
    if (!g->sync) pins |= 0x80; /* PB7 is low during sync */
    if (!g->image || g->image->writable) pins |= 0x10;
    via6522_set_input_b(via, pins);
}

bool gcr_drive_tick(GcrDrive *g, Via6522 *via, unsigned cycles, bool fast) {
    if (!g->motor || !g->track_length || !cycles) return false;
    g->bit_budget += (u64)cycles * bit_rates[g->zone];
    u64 byte_threshold = (fast ? 2000000u : 1000000u) * 8ull;
    bool ready = false;
    while (g->bit_budget >= byte_threshold) {
        g->bit_budget -= byte_threshold;
        g->read_byte = g->track_data[g->byte_pos];
        g->sync = g->track_sync[g->byte_pos] != 0;
        g->byte_pos = (g->byte_pos + 1) % g->track_length;
        if (!g->sync && (via->pcr & 0x22) == 0x22) {
            g->byte_ready = true;
            via6522_set_ca1(via, true);
            via6522_set_ca1(via, false);
            ready = true;
        }
    }
    gcr_drive_update_via(g, via);
    return ready;
}

u8 gcr_drive_read_byte(GcrDrive *g) {
    if (g->byte_ready) g->read_events++;
    g->byte_ready = false;
    return g->read_byte;
}
