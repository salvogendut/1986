#include "snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static Cpu8502State core_state;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

/* Small host stubs: this test exercises the VSF codec without pulling the
 * SDL machine scheduler into the unit-test binary. */
void cpu_state_get(const Cpu8502 *cpu, Cpu8502State *state) {
    (void)cpu; *state = core_state;
}
void cpu_state_set(Cpu8502 *cpu, const Cpu8502State *state) {
    core_state = *state;
    cpu->a = state->a; cpu->pc = state->pc; cpu->cycles = state->cycles;
}
void cpu_set_stack_page(u8 *page) { (void)page; }
void mem_set_processor_port(Mem *mem, u8 dir, u8 data) {
    mem->pla_data = (u8)((data & dir) | (u8)~dir);
}
u32 mem_cpu_page_offset(const Mem *mem, unsigned page) {
    return ((page ? mem->mmu.page1_bank : mem->mmu.page0_bank) << 16) |
           ((page ? mem->mmu.page1 : mem->mmu.page0) << 8);
}
void c128_reset(C128 *c) { memset(c->mem.ram, 0, sizeof(c->mem.ram)); }
void c128_set_4080(C128 *c, bool col80) { c->col_mode_80 = col80; }

static void put16(FILE *f, unsigned v) {
    fputc(v, f); fputc(v >> 8, f);
}
static void put32(FILE *f, unsigned v) {
    fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f);
}
static void put64(FILE *f, u64 v) {
    for (int i = 0; i < 8; ++i) fputc((int)(v >> (i * 8)), f);
}
static void module_header(FILE *f, const char *name, int major, int minor,
                          unsigned payload) {
    char padded[16] = {0}; snprintf(padded, sizeof(padded), "%s", name);
    fwrite(padded, 1, 16, f); fputc(major, f); fputc(minor, f);
    put32(f, payload + 22);
}

/* Construct the immediately preceding private ABI from a current file,
 * retaining every other VSF module and its original bytes. */
static void make_legacy_vic(const char *path, const char *legacy) {
    FILE *f = fopen(path, "rb");
    if (!f) { CHECK(false, "open current snapshot fixture"); return; }
    fseek(f, 0, SEEK_END);
    size_t n = (size_t)ftell(f);
    rewind(f);
    u8 *bytes = malloc(n);
    if (!bytes) { fclose(f); CHECK(false, "allocate legacy fixture"); return; }
    CHECK(fread(bytes, 1, n, f) == n, "read snapshot fixture");
    fclose(f);
    bool converted = false;
    for (size_t at = 58; at + 22 <= n;) {
        u8 *h = bytes + at;
        u32 size = h[18] | h[19] << 8 | h[20] << 16 | (u32)h[21] << 24;
        if (size < 22 || size > n - at) break;
        if (!memcmp(h, "1986STATE", 9)) {
            size_t old = offsetof(Vic, beam_half_clock);
            size_t removed = sizeof(Vic) - old;
            size_t vic = at + 22 + 36 + 32 + sizeof(Z80) + sizeof(Mmu) +
                         RAM_TOTAL + 0x800 + 1;
            memmove(bytes + vic + old, bytes + vic + sizeof(Vic),
                    n - vic - sizeof(Vic));
            size -= removed; n -= removed;
            for (unsigned b = 0; b < 4; b++) {
                h[18 + b] = size >> (8 * b);
                h[22 + 16 + b] = old >> (8 * b);
            }
            converted = true;
            break;
        }
        at += size;
    }
    CHECK(converted, "find private snapshot module");
    f = fopen(legacy, "wb");
    CHECK(f && fwrite(bytes, 1, n, f) == n, "write legacy snapshot fixture");
    if (f) fclose(f);
    free(bytes);
}

static void make_vice_projection(const char *path) {
    FILE *f = fopen(path, "wb");
    char machine[16] = "C128";
    fwrite("VICE Snapshot File\032", 1, 19, f); fputc(1, f); fputc(0, f);
    fwrite(machine, 1, 16, f); fwrite("VICE Version\032", 1, 13, f);
    fputc(3, f); fputc(10, f); fputc(0, f); fputc(0, f); put32(f, 0);
    module_header(f, "MAINCPU", 1, 3, 79);
    put64(f, 0x123456); fputc(0x11, f); fputc(0x22, f); fputc(0x33, f);
    fputc(0x44, f); put16(f, 0x5678); fputc(0x25, f);
    put32(f, 0xA5); put32(f, 0); put32(f, 0);
    for (int i = 0; i < 52; ++i) fputc(0, f);
    module_header(f, "C128MEM", 0, 0, 11 + 0x40000);
    u8 mmu[11] = {0x41, 2, 3, 4, 5, 0x81, 6, 7, 1, 9, 0};
    fwrite(mmu, 1, sizeof(mmu), f);
    for (unsigned i = 0; i < 0x40000; ++i) fputc(i ^ 0x5a, f);
    fclose(f);
}

/* Rewrite only the expansion module, or reproduce a pre-REU major-1 file. */
static void make_reu_variant(const char *path, const char *variant, int mode) {
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL, "open REU fixture");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    size_t n = (size_t)ftell(f);
    rewind(f);
    u8 *bytes = malloc(n);
    if (!bytes) { fclose(f); CHECK(false, "allocate REU fixture"); return; }
    CHECK(fread(bytes, 1, n, f) == n, "read REU fixture");
    fclose(f);
    bool found = false;
    for (size_t at = 58; at + 22 <= n;) {
        u8 *h = bytes + at;
        u32 size = h[18] | h[19] << 8 | h[20] << 16 | (u32)h[21] << 24;
        if (size < 22 || size > n - at) break;
        if (mode == 0 && !memcmp(h, "1986STATE", 9)) h[16] = 1;
        if (!memcmp(h, "1986REU", 7)) {
            found = true;
            if (mode <= 1) { /* old file / missing required module */
                memmove(h, h + size, n - at - size);
                n -= size;
            } else if (mode == 2) { /* incomplete RAM payload */
                n = at + size - 1;
            } else if (mode == 3) {
                h[22 + 23] = 2; /* invalid active flag */
            } else if (mode == 4) {
                h[22] = 1; /* unsupported 513 KiB */
            } else if (mode == 5) {
                h[16] = 2; /* unsupported module version */
            } else {
                h[22 + 34] = 4; /* invalid DMA phase */
            }
            break;
        }
        at += size;
    }
    CHECK(found, "find REU module");
    f = fopen(variant, "wb");
    CHECK(f && fwrite(bytes, 1, n, f) == n, "write REU fixture variant");
    if (f) fclose(f);
    free(bytes);
}

static u8 reu_host_read(void *ctx, u16 address) {
    C128 *c = ctx;
    return c->mem.ram[address];
}
static void reu_host_write(void *ctx, u16 address, u8 value) {
    C128 *c = ctx;
    c->mem.ram[address] = value;
}

static void test_reu_snapshot(C128 *c, const char *path, const char *variant) {
    Config cfg = {0};
    c->cfg = &cfg;
    reu_configure(&c->reu, 512);
    for (unsigned i = 0; i < REU_MAX_RAM; ++i)
        c->reu.ram[i] = (u8)(i ^ (i >> 8) ^ (i >> 16));
    c->mem.ram[0x2000] = 0x75;
    c->reu.ram[0x70000] = 0x82;
    reu_write(&c->reu, 2, 0); reu_write(&c->reu, 3, 0x20);
    reu_write(&c->reu, 4, 0); reu_write(&c->reu, 5, 0);
    reu_write(&c->reu, 6, 7);
    reu_write(&c->reu, 7, 1); reu_write(&c->reu, 8, 0);
    reu_write(&c->reu, 9, 0xc0);
    reu_write(&c->reu, 1, 0xb2); /* swap, autoload, EOB IRQ */
    ReuBus bus = {c, reu_host_read, reu_host_write};
    reu_tick(&c->reu, &bus); /* takeover */
    reu_tick(&c->reu, &bus); /* latch both bytes before writing */
    CHECK(c->reu.active && c->reu.phase == 2, "snapshot mid-swap fixture");
    static Reu saved;
    saved = c->reu;
    CHECK(snapshot_save(c, path) == SNAPSHOT_OK, "save active 512K REU");
    reu_configure(&c->reu, 0);
    c->mem.ram[0x2000] = 0;
    CHECK(snapshot_load(c, path) == SNAPSHOT_OK, "load active REU");
    CHECK(cfg.reu_enabled && cfg.reu_size_kb == 512 && c->reu.size_kb == 512,
          "snapshot restores expansion selection");
    CHECK(!memcmp(saved.ram, c->reu.ram, REU_MAX_RAM) &&
          !memcmp(saved.regs, c->reu.regs, REU_REG_COUNT) &&
          !memcmp(saved.shadow, c->reu.shadow, sizeof(saved.shadow)),
          "all expansion banks, registers and autoload shadows round trip");
    CHECK(c->reu.active && c->reu.phase == 2 && c->reu.remaining == 1 &&
          c->reu.host_address == 0x2000 && c->reu.reu_address == 0x70000 &&
          c->reu.host_latch == 0x75 && c->reu.reu_latch == 0x82 &&
          c->reu.floating_bus == saved.floating_bus,
          "DMA address, phase and byte latches round trip");
    reu_tick(&c->reu, &bus);
    CHECK(!c->reu.active && c->mem.ram[0x2000] == 0x82 &&
          c->reu.ram[0x70000] == 0x75 && reu_irq(&c->reu) &&
          reu_peek(&c->reu, 2) == 0 && reu_peek(&c->reu, 7) == 1,
          "resumed swap completes once, autoloads and asserts IRQ");

    /* Invalid expansion state must not partially load the CPU/RAM first. */
    for (int mode = 1; mode <= 6; ++mode) {
        make_reu_variant(path, variant, mode);
        core_state.pc = 0xbeef; c->mem.ram[0x2000] = 0xa5;
        SnapshotResult result = snapshot_load(c, variant);
        CHECK(result == (mode == 5 ? SNAPSHOT_ERR_VERSION : SNAPSHOT_ERR_STATE),
              "reject missing, truncated or malformed REU state");
        CHECK(core_state.pc == 0xbeef && c->mem.ram[0x2000] == 0xa5 &&
              !c->reu.active && c->reu.ram[0x70000] == 0x75 && reu_irq(&c->reu),
              "rejected expansion snapshot leaves running machine untouched");
    }
    CHECK(snapshot_save(c, variant) == SNAPSHOT_OK, "save pending REU IRQ");
    reu_read(&c->reu, 0);
    CHECK(snapshot_load(c, variant) == SNAPSHOT_OK && reu_irq(&c->reu),
          "latched REU IRQ survives snapshot restore");

    reu_write(&c->reu, 1, 0x81);
    CHECK(snapshot_save(c, variant) == SNAPSHOT_OK, "save armed FF00 trigger");
    reu_reset(&c->reu);
    CHECK(snapshot_load(c, variant) == SNAPSHOT_OK && c->reu.armed && !c->reu.active,
          "armed FF00 command survives snapshot restore");
    reu_ff00_trigger(&c->reu);
    CHECK(c->reu.active && !c->reu.armed, "restored command still triggers");

    make_reu_variant(path, variant, 0);
    CHECK(snapshot_load(c, variant) == SNAPSHOT_OK && !c->reu.size_kb &&
          !cfg.reu_enabled && !c->reu.active && !reu_irq(&c->reu),
          "pre-REU major-1 snapshots load with expansion detached");
    for (unsigned kb = 0; kb <= 256; kb += 128) {
        reu_configure(&c->reu, kb);
        if (kb) c->reu.ram[kb * 1024 - 1] = 0x63;
        CHECK(snapshot_save(c, variant) == SNAPSHOT_OK, "save smaller/detached REU");
        reu_configure(&c->reu, 512);
        CHECK(snapshot_load(c, variant) == SNAPSHOT_OK && c->reu.size_kb == kb &&
              cfg.reu_enabled == (kb != 0) &&
              (!kb || (cfg.reu_size_kb == (int)kb && c->reu.ram[kb * 1024 - 1] == 0x63)),
              "smaller and detached expansions restore their exact sizes");
    }
    c->cfg = NULL;
}

int main(void) {
    static C128 c;
    const char *path = "/tmp/1986-test-snapshot.vsf";
    const char *vice = "/tmp/1986-test-vice.vsf";
    const char *legacy = "/tmp/1986-test-legacy-vic.vsf";
    c.vdc.fb_w = 800; c.vdc.fb_h = 400;
    c.vdc.fb = calloc((size_t)c.vdc.fb_w * c.vdc.fb_h, sizeof(*c.vdc.fb));
    c.vdc.display_fb = calloc((size_t)c.vdc.fb_w * c.vdc.fb_h,
                              sizeof(*c.vdc.display_fb));
    u32 *host_vdc_fb = c.vdc.fb;
    CHECK(c.vdc.fb && c.vdc.display_fb, "allocate host VDC scanout buffers");
    c.mem.ram[0x1234] = 0xA7;
    c.mem.color_ram[0x321] = 0x0E;
    c.mem.mmu.mcr = 0x42; c.mem.mmu.page1 = 0x73;
    c.z80.pc = 0xCAFE; c.z80.af = 0xBEEF; c.z80.iff1 = true;
    c.vic.raster_irq_line = 0x101; c.vic.sprite_enable = 0x81;
    c.vic.beam_half_clock = 123456;
    c.vic.clocked = true; c.vic.vc = 37; c.vic.vcbase = 40;
    c.vic.fetch_row_counter = 5;
    c.vic.sprite_line_data[60][2] = 0xaabbcc;
    c.vic.beam_pixels[70][50] = 0x87;
    c.vdc.regs[12] = 0x20; c.vdc.ram[0x4567] = 0xCC;
    c.vdc.draw_screen_pending = true; c.vdc.draw_attribute_pending = true;
    c.cia1.ta_counter = 0x1234; c.cia2.tod[2] = 0x59;
    c.sid.regs[0x18] = 0x0f; c.sid.voice[1].phase = 0x123456;
    c.kbd.matrix[3] = 0x40; c.joyports.mouse_x[1] = 0x91;
    c.fast = true; c.col_mode_80 = false; c.total_cycles = 0x112233445566ULL;
    core_state = (Cpu8502State){
        .a = 0x12, .x = 0x34, .y = 0x56, .sp = 0x78, .p = 0x25,
        .pc = 0x9abc, .clock = 0x102030405ULL, .cycles = 987654,
        .last_opcode_info = 0xa5, .fast = true, .io_ddr = 0x2f, .io_port = 0x37
    };

    CHECK(snapshot_save(&c, path) == SNAPSHOT_OK, "save full snapshot");
    FILE *f = fopen(path, "rb");
    char magic[19]; CHECK(f && fread(magic, 1, sizeof(magic), f) == sizeof(magic),
                          "read VSF header");
    CHECK(!memcmp(magic, "VICE Snapshot File\032", 19), "VICE magic");
    if (f) fclose(f);

    c.mem.ram[0x1234] = 0; c.mem.color_ram[0x321] = 0;
    c.z80.pc = 0; c.vic.sprite_enable = 0; c.vdc.ram[0x4567] = 0;
    c.vdc.draw_screen_pending = false; c.vdc.draw_attribute_pending = false;
    c.cia1.ta_counter = 0; c.sid.regs[0x18] = 0; c.total_cycles = 0;
    c.vic.beam_half_clock = 0; c.vic.clocked = false;
    c.vic.sprite_line_data[60][2] = 0; c.vic.beam_pixels[70][50] = 0;
    core_state.pc = 0;
    CHECK(snapshot_load(&c, path) == SNAPSHOT_OK, "load full snapshot");
    CHECK(c.mem.ram[0x1234] == 0xA7 && c.mem.color_ram[0x321] == 0x0E,
          "RAM and color RAM round trip");
    CHECK(c.z80.pc == 0xCAFE && c.z80.af == 0xBEEF && c.z80.iff1,
          "Z80 round trip");
    CHECK(c.vic.sprite_enable == 0x81 && c.vdc.ram[0x4567] == 0xCC,
          "VIC and VDC round trip");
    CHECK(c.vdc.draw_screen_pending && c.vdc.draw_attribute_pending,
          "pending VDC raster address steps survive a snapshot round trip");
    CHECK(c.cia1.ta_counter == 0x1234 && c.sid.regs[0x18] == 0x0f,
          "CIA and SID round trip");
    CHECK(core_state.pc == 0x9abc && core_state.clock == 0x102030405ULL,
          "8502 core round trip");
    CHECK(c.vdc.fb == host_vdc_fb, "host VDC pointer preserved");
    CHECK(c.vic.beam_half_clock == 123456 && c.vic.clocked &&
          c.vic.vc == 37 && c.vic.vcbase == 40 && c.vic.fetch_row_counter == 5 &&
          c.vic.sprite_line_data[60][2] == 0xaabbcc &&
          c.vic.beam_pixels[70][50] == 0x87,
          "VIC beam, fetch counters and captured pixels round trip");
    CHECK(!c.cpu_clock_active && c.cpu_clock_synced == core_state.clock,
          "snapshot restores an inactive CPU clock cursor");

    make_legacy_vic(path, legacy);
    CHECK(snapshot_load(&c, legacy) == SNAPSHOT_OK,
          "load pre-timing VIC snapshot");
    CHECK(c.vic.sprite_enable == 0x81 && !c.vic.clocked &&
          c.vic.beam_half_clock == 0 && c.vic.vertical_border &&
          c.vdc.ram[0x4567] == 0xcc && core_state.pc == 0x9abc,
          "legacy snapshot retains machine state and restarts the beam");

    make_vice_projection(vice);
    core_state.pc = 0xabcd;
    c.mem.ram[0x1234] = 0x6e;
    c.mem.mmu.mcr = 0x9a;
    CHECK(snapshot_load(&c, vice) == SNAPSHOT_ERR_FOREIGN_STATE,
          "foreign VICE machine state is rejected");
    CHECK(core_state.pc == 0xabcd && c.mem.ram[0x1234] == 0x6e &&
          c.mem.mmu.mcr == 0x9a,
          "rejected VICE snapshot does not partially mutate the machine");

    test_reu_snapshot(&c, path, legacy);

    remove(path); remove(vice); remove(legacy);
    free(c.vdc.display_fb);
    free(c.vdc.fb);
    if (failures) return 1;
    puts("snapshot tests passed");
    return 0;
}
