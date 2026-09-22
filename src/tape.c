#include "tape.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAPE_MAX_SIZE (64u * 1024u * 1024u)
#define TAPE_ZERO_GAP 2500u /* VICE DatasetteZeroGapDelay default. */

static u16 le16(const u8 *p) { return (u16)(p[0] | ((u16)p[1] << 8)); }
static u32 le32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) |
           ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

void tape_init(Tape *t) { memset(t, 0, sizeof(*t)); }

void tape_eject(Tape *t) {
    free(t->data);
    free(t->files);
    tape_init(t);
}

static bool validate_tap(Tape *t) {
    if (t->size < 20 || memcmp(t->data, "C64-TAPE-RAW", 12)) return false;
    t->version = t->data[12];
    if (t->version > 2) return false;
    u32 declared = le32(t->data + 16);
    if (!declared || declared > t->size - 20) return false;
    t->payload_end = 20u + declared;
    for (size_t p = 20; p < t->payload_end;) {
        if (t->data[p++]) continue;
        if (t->version != 0) {
            if (t->payload_end - p < 3) return false;
            p += 3;
        }
    }
    t->kind = TAPE_TAP;
    t->position = 20;
    return true;
}

static bool validate_t64(Tape *t) {
    if (t->size < 96 ||
        (memcmp(t->data, "C64 tape image file", 19) &&
         memcmp(t->data, "C64S tape file", 14) &&
         memcmp(t->data, "C64S tape image file", 20))) return false;
    unsigned count = le16(t->data + 34);
    /* VICE accepts the common malformed T64 header with zero max entries:
     * the first record still follows the 64-byte header. */
    if (!count) count = 1;
    if (count > 1024 || count > (t->size - 64) / 32) return false;
    t->files = calloc(count, sizeof(*t->files));
    if (!t->files) return false;
    for (unsigned i = 0; i < count; ++i) {
        const u8 *r = t->data + 64 + 32u * i;
        if (r[0] != 1) continue;
        TapeFile *f = &t->files[t->file_count];
        f->start = le16(r + 2);
        f->end = le16(r + 4);
        f->offset = le32(r + 8);
        if (f->end <= f->start || f->offset < 64u + 32u * count ||
            f->offset > t->size ||
            (size_t)(f->end - f->start) > t->size - f->offset)
            return false;
        memcpy(f->name, r + 16, 16);
        t->file_count++;
    }
    if (!t->file_count) return false;
    t->kind = TAPE_T64;
    t->current_file = (size_t)-1;
    return true;
}

bool tape_mount(Tape *t, const char *path) {
    if (!t || !path || !*path) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return false;
    }
    long length = ftell(file);
    if (length < 0 || (unsigned long)length > TAPE_MAX_SIZE) {
        fclose(file);
        return false;
    }
    size_t size = (size_t)length;
    rewind(file);
    Tape next;
    tape_init(&next);
    next.data = malloc(size ? size : 1);
    next.size = size;
    bool ok = next.data && fread(next.data, 1, size, file) == size;
    fclose(file);
    if (ok) ok = validate_tap(&next) || validate_t64(&next);
    if (!ok) { tape_eject(&next); return false; }
    if (next.kind == TAPE_T64) next.play_button = true;
    tape_eject(t);
    *t = next;
    return true;
}

void tape_play(Tape *t) {
    if ((t->kind == TAPE_TAP && t->position < t->payload_end) ||
        t->kind == TAPE_T64)
        t->play_button = true;
}

void tape_stop(Tape *t) { t->play_button = false; }

void tape_rewind(Tape *t) {
    t->play_button = false;
    t->pulse_total = t->pulse_remaining = 0;
    t->frame_edges = 0;
    t->scope_head = t->scope_count = 0;
    if (t->kind == TAPE_TAP) t->position = 20;
    if (t->kind == TAPE_T64) {
        t->next_file = 0;
        t->current_file = (size_t)-1;
        t->file_position = 0;
    }
}

void tape_set_motor(Tape *t, bool motor_on) { t->motor_on = motor_on; }
bool tape_running(const Tape *t) {
    return t->kind == TAPE_TAP && t->play_button && t->motor_on &&
           (t->pulse_remaining || t->position < t->payload_end);
}

static u32 next_gap(Tape *t) {
    if (t->position >= t->payload_end) return 0;
    u8 b = t->data[t->position++];
    if (b) return (u32)b * 8u;
    if (t->version == 0) return TAPE_ZERO_GAP;
    u32 gap = (u32)t->data[t->position] |
              ((u32)t->data[t->position + 1] << 8) |
              ((u32)t->data[t->position + 2] << 16);
    t->position += 3;
    return gap ? gap : TAPE_ZERO_GAP;
}

void tape_advance(Tape *t, unsigned cycles, void (*pulse)(void *), void *ctx) {
    while (cycles && tape_running(t)) {
        if (!t->pulse_remaining) {
            t->pulse_total = t->pulse_remaining = next_gap(t);
            if (!t->pulse_total) { t->play_button = false; break; }
            t->scope[t->scope_head] = t->pulse_total;
            t->scope_head = (t->scope_head + 1) % TAPE_SCOPE_SAMPLES;
            if (t->scope_count < TAPE_SCOPE_SAMPLES) t->scope_count++;
        }
        unsigned used = cycles < t->pulse_remaining ? cycles : t->pulse_remaining;
        cycles -= used;
        t->pulse_remaining -= used;
        if (!t->pulse_remaining) {
            t->frame_edges++;
            if (pulse) pulse(ctx);
            if (t->position == t->payload_end) t->play_button = false;
        }
    }
}

void tape_mix_audio(const Tape *t, s16 *samples, int count, bool enabled) {
    if (!enabled || !tape_running(t) || !t->pulse_total) return;
    /* VICE splits each TAP gap into positive/negative halfwaves for sound.
     * This monitor plays that signal, never synthetic motor noise. */
    int add = t->pulse_remaining > t->pulse_total / 2 ? 900 : -900;
    for (int i = 0; i < count; ++i) {
        int mixed = (int)samples[i] + add;
        samples[i] = (s16)(mixed > 32767 ? 32767 : mixed < -32768 ? -32768 : mixed);
    }
}

bool tape_t64_next_header(Tape *t, u8 header[21]) {
    if (t->kind != TAPE_T64 || !t->file_count) return false;
    t->current_file = t->next_file++ % t->file_count;
    t->file_position = 0;
    const TapeFile *f = &t->files[t->current_file];
    header[0] = 3; /* C128 KERNAL absolute PRG tape header. */
    header[1] = (u8)f->start;
    header[2] = (u8)(f->start >> 8);
    header[3] = (u8)f->end;
    header[4] = (u8)(f->end >> 8);
    memcpy(header + 5, f->name, 16);
    return true;
}

int tape_t64_read_byte(Tape *t) {
    if (t->kind != TAPE_T64 || t->current_file >= t->file_count) return -1;
    const TapeFile *f = &t->files[t->current_file];
    if (t->file_position >= (size_t)(f->end - f->start)) return -1;
    return t->data[f->offset + t->file_position++];
}
