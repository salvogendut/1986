#include "compat_win.h"
#include "snapshot.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VSF_MAGIC "VICE Snapshot File\032"
#define VSF_MAGIC_LEN 19
#define VSF_MACHINE_LEN 16
#define VSF_VERSION_MAGIC "VICE Version\032"
#define VSF_VERSION_MAGIC_LEN 13
#define VSF_HEADER_SIZE 58
#define VSF_MODULE_HEADER_SIZE 22
#define PRIVATE_MAGIC 0x36383931u /* bytes: 31 39 38 36 ("1986") */
#define PRIVATE_SCHEMA 1u

typedef struct {
    FILE *file;
    long start;
    bool failed;
} ModuleWriter;

typedef struct {
    const u8 *data;
    size_t size;
    size_t pos;
    bool failed;
} Reader;

typedef struct {
    const u8 *payload;
    size_t size;
    u8 major, minor;
} ModuleView;

static bool host_little_endian(void) {
    const u16 value = 1;
    return *(const u8 *)&value == 1;
}

static void write_u8(ModuleWriter *w, u8 value) {
    if (!w->failed && fwrite(&value, 1, 1, w->file) != 1) w->failed = true;
}

static void write_u16(ModuleWriter *w, u16 value) {
    write_u8(w, (u8)value);
    write_u8(w, (u8)(value >> 8));
}

static void write_u32(ModuleWriter *w, u32 value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        write_u8(w, (u8)(value >> shift));
}

static void write_u64(ModuleWriter *w, u64 value) {
    for (unsigned shift = 0; shift < 64; shift += 8)
        write_u8(w, (u8)(value >> shift));
}

static void write_blob(ModuleWriter *w, const void *data, size_t size) {
    if (!w->failed && size && fwrite(data, 1, size, w->file) != size)
        w->failed = true;
}

static bool module_begin(ModuleWriter *w, FILE *file, const char *name,
                         u8 major, u8 minor) {
    memset(w, 0, sizeof(*w));
    w->file = file;
    w->start = ftell(file);
    if (w->start < 0) return false;
    char padded[16] = {0};
    snprintf(padded, sizeof(padded), "%s", name);
    if (fwrite(padded, 1, sizeof(padded), file) != sizeof(padded)) return false;
    if (fputc(major, file) == EOF || fputc(minor, file) == EOF) return false;
    u8 zero[4] = {0};
    return fwrite(zero, 1, sizeof(zero), file) == sizeof(zero);
}

static bool module_end(ModuleWriter *w) {
    long end = ftell(w->file);
    if (w->failed || end < 0 || end - w->start > (long)UINT32_MAX)
        return false;
    u32 size = (u32)(end - w->start);
    if (fseek(w->file, w->start + 18, SEEK_SET) != 0) return false;
    u8 encoded[4] = {(u8)size, (u8)(size >> 8), (u8)(size >> 16), (u8)(size >> 24)};
    if (fwrite(encoded, 1, 4, w->file) != 4) return false;
    return fseek(w->file, end, SEEK_SET) == 0;
}

static u8 read_u8(Reader *r) {
    if (r->pos >= r->size) { r->failed = true; return 0; }
    return r->data[r->pos++];
}

static u16 read_u16(Reader *r) {
    u16 value = read_u8(r);
    value |= (u16)read_u8(r) << 8;
    return value;
}

static u32 read_u32(Reader *r) {
    u32 value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
        value |= (u32)read_u8(r) << shift;
    return value;
}

static u64 read_u64(Reader *r) {
    u64 value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
        value |= (u64)read_u8(r) << shift;
    return value;
}

static void read_blob(Reader *r, void *data, size_t size) {
    if (size > r->size - r->pos) {
        r->failed = true;
        return;
    }
    memcpy(data, r->data + r->pos, size);
    r->pos += size;
}

static u32 load_u32(const u8 *p) {
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static bool find_module(const u8 *file, size_t size, const char *wanted,
                        ModuleView *view) {
    size_t pos = VSF_HEADER_SIZE;
    while (pos + VSF_MODULE_HEADER_SIZE <= size) {
        char name[17];
        memcpy(name, file + pos, 16);
        name[16] = '\0';
        u32 module_size = load_u32(file + pos + 18);
        if (module_size < VSF_MODULE_HEADER_SIZE || module_size > size - pos)
            return false;
        if (!strncmp(name, wanted, 16)) {
            view->major = file[pos + 16];
            view->minor = file[pos + 17];
            view->payload = file + pos + VSF_MODULE_HEADER_SIZE;
            view->size = module_size - VSF_MODULE_HEADER_SIZE;
            return true;
        }
        pos += module_size;
    }
    return false;
}

static bool write_header(FILE *file) {
    char machine[VSF_MACHINE_LEN] = {0};
    snprintf(machine, sizeof(machine), "C128");
    const u8 version[4] = {3, 10, 0, 0};
    const u8 revision[4] = {0, 0, 0, 0};
    return fwrite(VSF_MAGIC, 1, VSF_MAGIC_LEN, file) == VSF_MAGIC_LEN &&
           fputc(1, file) != EOF && fputc(0, file) != EOF &&
           fwrite(machine, 1, sizeof(machine), file) == sizeof(machine) &&
           fwrite(VSF_VERSION_MAGIC, 1, VSF_VERSION_MAGIC_LEN, file) == VSF_VERSION_MAGIC_LEN &&
           fwrite(version, 1, sizeof(version), file) == sizeof(version) &&
           fwrite(revision, 1, sizeof(revision), file) == sizeof(revision);
}

static void write_cpu_private(ModuleWriter *w, const Cpu8502State *s) {
    write_u8(w, s->a); write_u8(w, s->x); write_u8(w, s->y);
    write_u8(w, s->sp); write_u8(w, s->p); write_u16(w, s->pc);
    write_u64(w, s->clock); write_u64(w, s->cycles);
    write_u32(w, s->last_opcode_info);
    write_u8(w, s->irq_level); write_u8(w, s->nmi_level);
    write_u8(w, s->fast); write_u8(w, s->io_ddr); write_u8(w, s->io_port);
}

static void read_cpu_private(Reader *r, Cpu8502State *s) {
    s->a = read_u8(r); s->x = read_u8(r); s->y = read_u8(r);
    s->sp = read_u8(r); s->p = read_u8(r); s->pc = read_u16(r);
    s->clock = read_u64(r); s->cycles = read_u64(r);
    s->last_opcode_info = read_u32(r);
    s->irq_level = read_u8(r) != 0; s->nmi_level = read_u8(r) != 0;
    s->fast = read_u8(r) != 0; s->io_ddr = read_u8(r); s->io_port = read_u8(r);
}

static void write_tape_state(ModuleWriter *w, const Tape *t) {
    write_u32(w, (u32)t->kind); write_u64(w, t->position);
    write_u32(w, t->pulse_total); write_u32(w, t->pulse_remaining);
    write_u8(w, t->play_button); write_u8(w, t->motor_on);
    write_u64(w, t->next_file); write_u64(w, t->current_file);
    write_u64(w, t->file_position);
}

static void read_tape_state(Reader *r, Tape *t) {
    TapeKind kind = (TapeKind)read_u32(r);
    size_t position = (size_t)read_u64(r);
    u32 pulse_total = read_u32(r), pulse_remaining = read_u32(r);
    bool play = read_u8(r) != 0, motor = read_u8(r) != 0;
    size_t next = (size_t)read_u64(r), current = (size_t)read_u64(r);
    size_t file_position = (size_t)read_u64(r);
    if (!r->failed && t->kind == kind) {
        if (position <= t->payload_end) t->position = position;
        t->pulse_total = pulse_total;
        t->pulse_remaining = pulse_remaining;
        t->play_button = play;
        t->motor_on = motor;
        if (next <= t->file_count) t->next_file = next;
        if (current < t->file_count || !t->file_count) t->current_file = current;
        t->file_position = file_position;
    }
}

static bool write_private(FILE *file, C128 *c) {
    ModuleWriter w;
    if (!module_begin(&w, file, "1986STATE", 1, 0)) return false;
    write_u32(&w, PRIVATE_MAGIC);
    write_u16(&w, PRIVATE_SCHEMA);
    write_u8(&w, host_little_endian() ? 1 : 0);
    write_u8(&w, (u8)sizeof(bool));
    write_u32(&w, sizeof(Z80)); write_u32(&w, sizeof(Mmu));
    write_u32(&w, sizeof(Vic)); write_u32(&w, sizeof(Cia));
    write_u32(&w, sizeof(Sid)); write_u32(&w, sizeof(Kbd));
    write_u32(&w, sizeof(JoyPorts));

    Cpu8502State cpu;
    cpu_state_get(&c->cpu, &cpu);
    write_cpu_private(&w, &cpu);
    write_blob(&w, &c->z80, sizeof(c->z80));
    write_blob(&w, &c->mem.mmu, sizeof(c->mem.mmu));
    write_blob(&w, c->mem.ram, sizeof(c->mem.ram));
    write_blob(&w, c->mem.color_ram, sizeof(c->mem.color_ram));
    write_u8(&w, c->mem.pla_data);
    write_blob(&w, &c->vic, sizeof(c->vic));

    /* The VDC framebuffer pointer is host state; the emulated fields on both
     * sides of it are stored separately. */
    write_u32(&w, (u32)offsetof(Vdc, fb));
    write_blob(&w, &c->vdc, offsetof(Vdc, fb));
    write_u32(&w, (u32)c->vdc.fb_w); write_u32(&w, (u32)c->vdc.fb_h);
    write_u8(&w, c->vdc.dirty);
    write_blob(&w, &c->cia1, sizeof(c->cia1));
    write_blob(&w, &c->cia2, sizeof(c->cia2));
    write_blob(&w, &c->sid, sizeof(c->sid));
    write_blob(&w, &c->kbd, sizeof(c->kbd));
    write_blob(&w, &c->joyports, sizeof(c->joyports));

    write_u8(&w, c->fast); write_u8(&w, c->col_mode_80);
    write_u8(&w, c->restore_down); write_u8(&w, c->paused);
    write_u32(&w, (u32)c->frames_since_reset);
    write_u32(&w, (u32)c->cpu_frame_debt);
    write_u32(&w, (u32)c->z80_frame_debt);
    write_u32(&w, (u32)c->peripheral_fast_remainder);
    write_u32(&w, (u32)c->z80_peripheral_remainder);
    write_u64(&w, c->bus_cycles); write_u64(&w, c->total_cycles);
    write_tape_state(&w, &c->tape);
    return module_end(&w);
}

static SnapshotResult load_private(C128 *c, const ModuleView *module) {
    if (module->major != 1) return SNAPSHOT_ERR_VERSION;
    const size_t expected =
        4 + 2 + 1 + 1 + 7 * 4 + /* private header and ABI sizes */
        32 +                       /* portable 8502 state */
        sizeof(Z80) + sizeof(Mmu) + RAM_TOTAL + 0x800 + 1 + sizeof(Vic) +
        4 + offsetof(Vdc, fb) + 4 + 4 + 1 +
        2 * sizeof(Cia) + sizeof(Sid) + sizeof(Kbd) + sizeof(JoyPorts) +
        4 + 5 * 4 + 2 * 8 +       /* machine flags, debts, clocks */
        4 + 8 + 4 + 4 + 1 + 1 + 8 + 8 + 8; /* tape transport */
    if (module->size != expected) return SNAPSHOT_ERR_STATE;
    Reader r = {module->payload, module->size, 0, false};
    if (read_u32(&r) != PRIVATE_MAGIC || read_u16(&r) != PRIVATE_SCHEMA)
        return SNAPSHOT_ERR_STATE;
    bool little = read_u8(&r) != 0;
    u8 bool_size = read_u8(&r);
    u32 z80_size = read_u32(&r), mmu_size = read_u32(&r);
    u32 vic_size = read_u32(&r), cia_size = read_u32(&r);
    u32 sid_size = read_u32(&r), kbd_size = read_u32(&r);
    u32 joy_size = read_u32(&r);
    if (r.failed || !little || !host_little_endian() || bool_size != sizeof(bool) ||
        z80_size != sizeof(Z80) || mmu_size != sizeof(Mmu) ||
        vic_size != sizeof(Vic) || cia_size != sizeof(Cia) ||
        sid_size != sizeof(Sid) || kbd_size != sizeof(Kbd) ||
        joy_size != sizeof(JoyPorts))
        return SNAPSHOT_ERR_STATE;

    Cpu8502State cpu;
    read_cpu_private(&r, &cpu);
    read_blob(&r, &c->z80, sizeof(c->z80));
    read_blob(&r, &c->mem.mmu, sizeof(c->mem.mmu));
    read_blob(&r, c->mem.ram, sizeof(c->mem.ram));
    read_blob(&r, c->mem.color_ram, sizeof(c->mem.color_ram));
    c->mem.pla_data = read_u8(&r);
    read_blob(&r, &c->vic, sizeof(c->vic));

    u32 vdc_prefix = read_u32(&r);
    if (vdc_prefix != offsetof(Vdc, fb)) return SNAPSHOT_ERR_STATE;
    read_blob(&r, &c->vdc, vdc_prefix);
    (void)read_u32(&r); (void)read_u32(&r); /* allocation dimensions are host-owned */
    c->vdc.dirty = read_u8(&r) != 0;
    read_blob(&r, &c->cia1, sizeof(c->cia1));
    read_blob(&r, &c->cia2, sizeof(c->cia2));
    read_blob(&r, &c->sid, sizeof(c->sid));
    read_blob(&r, &c->kbd, sizeof(c->kbd));
    read_blob(&r, &c->joyports, sizeof(c->joyports));

    c->fast = read_u8(&r) != 0; c->col_mode_80 = read_u8(&r) != 0;
    c->restore_down = read_u8(&r) != 0; c->paused = read_u8(&r) != 0;
    c->frames_since_reset = (int)read_u32(&r);
    c->cpu_frame_debt = (int)read_u32(&r);
    c->z80_frame_debt = (int)read_u32(&r);
    c->peripheral_fast_remainder = (int)read_u32(&r);
    c->z80_peripheral_remainder = (int)read_u32(&r);
    c->bus_cycles = read_u64(&r); c->total_cycles = read_u64(&r);
    read_tape_state(&r, &c->tape);
    if (r.failed) return SNAPSHOT_ERR_STATE;

    cpu_state_set(&c->cpu, &cpu);
    mem_set_processor_port(&c->mem, cpu.io_ddr, cpu.io_port);
    cpu_set_stack_page(c->mem.ram + mem_cpu_page_offset(&c->mem, 1));
    c128_set_4080(c, c->col_mode_80);
    c->vdc.dirty = true;
    c->audio_count = 0;
    return SNAPSHOT_OK;
}

SnapshotResult snapshot_save(C128 *c, const char *path) {
    if (!c || !path || !path[0]) return SNAPSHOT_ERR_ARGUMENT;
    size_t length = strlen(path);
    char *temporary = malloc(length + 6);
    if (!temporary) return SNAPSHOT_ERR_IO;
    snprintf(temporary, length + 6, "%s.tmp", path);
    FILE *file = fopen(temporary, "wb");
    if (!file) { free(temporary); return SNAPSHOT_ERR_IO; }
    /* Do not emit an incomplete set of standard C128 modules.  VICE 3.10
     * partially applies MAINCPU/C128MEM before discovering a missing CIA or
     * VIC-II module, which can leave x128 unstable.  The file remains a real
     * VSF container, but VICE rejects it before changing machine state. */
    bool ok = write_header(file) && write_private(file, c);
    if (fclose(file) != 0) ok = false;
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(temporary, path,
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        ok = rename(temporary, path) == 0;
#endif
    }
    if (!ok) remove(temporary);
    free(temporary);
    return ok ? SNAPSHOT_OK : SNAPSHOT_ERR_IO;
}

SnapshotResult snapshot_load(C128 *c, const char *path) {
    if (!c || !path || !path[0]) return SNAPSHOT_ERR_ARGUMENT;
    FILE *file = fopen(path, "rb");
    if (!file) return SNAPSHOT_ERR_IO;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return SNAPSHOT_ERR_IO; }
    long end = ftell(file);
    if (end < VSF_HEADER_SIZE || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file); return SNAPSHOT_ERR_FORMAT;
    }
    u8 *data = malloc((size_t)end);
    if (!data) { fclose(file); return SNAPSHOT_ERR_IO; }
    bool read_ok = fread(data, 1, (size_t)end, file) == (size_t)end;
    fclose(file);
    if (!read_ok) { free(data); return SNAPSHOT_ERR_IO; }
    if (memcmp(data, VSF_MAGIC, VSF_MAGIC_LEN) ||
        memcmp(data + 21, "C128", 4) ||
        memcmp(data + 37, VSF_VERSION_MAGIC, VSF_VERSION_MAGIC_LEN)) {
        free(data); return SNAPSHOT_ERR_MACHINE;
    }

    ModuleView private_state, maincpu, memory;
    SnapshotResult result;
    if (find_module(data, (size_t)end, "1986STATE", &private_state)) {
        result = load_private(c, &private_state);
    } else if (find_module(data, (size_t)end, "MAINCPU", &maincpu) &&
               find_module(data, (size_t)end, "C128MEM", &memory)) {
        /* CPU+RAM alone cannot resume a running C128. VICE's accompanying
         * interrupt, VIC-II, CIA, drive and input modules use implementation-
         * specific state and its C128 writer omits Z80/VDC state altogether.
         * The old projection copied CPU/RAM after resetting every peripheral;
         * raster-sensitive code then diverged into data and could JAM the
         * 8502. Reject before changing any live state. */
        result = SNAPSHOT_ERR_FOREIGN_STATE;
    } else {
        result = SNAPSHOT_ERR_FORMAT;
    }
    free(data);
    return result;
}

const char *snapshot_result_name(SnapshotResult result) {
    switch (result) {
        case SNAPSHOT_OK: return "OK";
        case SNAPSHOT_ERR_ARGUMENT: return "INVALID SNAPSHOT PATH";
        case SNAPSHOT_ERR_IO: return "SNAPSHOT I/O ERROR";
        case SNAPSHOT_ERR_FORMAT: return "INVALID OR TRUNCATED VICE SNAPSHOT";
        case SNAPSHOT_ERR_MACHINE: return "SNAPSHOT IS NOT FOR A C128";
        case SNAPSHOT_ERR_VERSION: return "UNSUPPORTED SNAPSHOT VERSION";
        case SNAPSHOT_ERR_STATE: return "INCOMPATIBLE 1986 SNAPSHOT STATE";
        case SNAPSHOT_ERR_FOREIGN_STATE:
            return "VICE MACHINE STATE IS NOT YET IMPORTABLE";
        default: return "SNAPSHOT ERROR";
    }
}
