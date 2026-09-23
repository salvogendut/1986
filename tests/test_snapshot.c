#include "snapshot.h"

#include <stdio.h>
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

int main(void) {
    static C128 c;
    const char *path = "/tmp/1986-test-snapshot.vsf";
    const char *vice = "/tmp/1986-test-vice.vsf";
    c.vdc.fb = (u32 *)(uintptr_t)0x1234;
    c.vdc.fb_w = 800; c.vdc.fb_h = 400;
    c.mem.ram[0x1234] = 0xA7;
    c.mem.color_ram[0x321] = 0x0E;
    c.mem.mmu.mcr = 0x42; c.mem.mmu.page1 = 0x73;
    c.z80.pc = 0xCAFE; c.z80.af = 0xBEEF; c.z80.iff1 = true;
    c.vic.raster_irq_line = 0x101; c.vic.sprite_enable = 0x81;
    c.vdc.regs[12] = 0x20; c.vdc.ram[0x4567] = 0xCC;
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
    c.cia1.ta_counter = 0; c.sid.regs[0x18] = 0; c.total_cycles = 0;
    core_state.pc = 0;
    CHECK(snapshot_load(&c, path) == SNAPSHOT_OK, "load full snapshot");
    CHECK(!snapshot_last_load_was_partial(), "full snapshot marker");
    CHECK(c.mem.ram[0x1234] == 0xA7 && c.mem.color_ram[0x321] == 0x0E,
          "RAM and color RAM round trip");
    CHECK(c.z80.pc == 0xCAFE && c.z80.af == 0xBEEF && c.z80.iff1,
          "Z80 round trip");
    CHECK(c.vic.sprite_enable == 0x81 && c.vdc.ram[0x4567] == 0xCC,
          "VIC and VDC round trip");
    CHECK(c.cia1.ta_counter == 0x1234 && c.sid.regs[0x18] == 0x0f,
          "CIA and SID round trip");
    CHECK(core_state.pc == 0x9abc && core_state.clock == 0x102030405ULL,
          "8502 core round trip");
    CHECK(c.vdc.fb == (u32 *)(uintptr_t)0x1234, "host VDC pointer preserved");

    make_vice_projection(vice);
    CHECK(snapshot_load(&c, vice) == SNAPSHOT_OK, "load VICE projection");
    CHECK(snapshot_last_load_was_partial(), "VICE import marked partial");
    CHECK(core_state.pc == 0x5678 && core_state.a == 0x11,
          "VICE MAINCPU imported");
    CHECK(c.mem.ram[0x1234] == (u8)(0x1234 ^ 0x5a), "VICE C128MEM imported");
    CHECK(c.mem.mmu.mcr == 0x41 && c.mem.mmu.page0 == 7,
          "VICE MMU imported");

    remove(path); remove(vice);
    if (failures) return 1;
    puts("snapshot tests passed");
    return 0;
}
