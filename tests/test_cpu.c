#include "cpu.h"
#include "vice/maincpu.h"
#include "vice/types.h"
#include <stdio.h>
#include <string.h>

static u8 ram[0x10000];
static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

static u8  bus_read (void *ctx, u16 addr) { (void)ctx; return ram[addr]; }
static void bus_write(void *ctx, u16 addr, u8 val) { (void)ctx; ram[addr] = val; }

static void load(u16 addr, const u8 *bytes, int n) {
    memcpy(&ram[addr], bytes, n);
}

extern DWORD traps_handler(void);

static u8 attention_byte, send_byte;
static int receive_result = 2;
static u8 pending_status;
static void test_attention(void *ctx, u8 byte) { (void)ctx; attention_byte = byte; }
static void test_send(void *ctx, u8 byte) { (void)ctx; send_byte = byte; }
static int test_receive(void *ctx, u8 *byte) {
    (void)ctx;
    *byte = 0x80;
    return receive_result;
}
static u8 test_take_status(void *ctx) {
    (void)ctx;
    u8 status = pending_status;
    pending_status = 0;
    return status;
}
static bool test_tape_header(void *ctx, u8 header[21]) {
    (void)ctx;
    memset(header, 0, 21);
    header[0] = 1;
    header[1] = 0x00;
    header[2] = 0x20;
    header[3] = 0x02;
    header[4] = 0x20;
    return true;
}
static int test_tape_byte(void *ctx) {
    u8 *next = ctx;
    return (*next)++;
}

/* cpu_step() runs a whole frame (CPU_PAL_FRAME_CYCLES) of the VICE 6502 core.
 * The tiny program below loops, so the assertions hold after one frame. */
int main(void) {
    Cpu8502 cpu;
    memset(ram, 0, sizeof(ram));
    cpu_init(&cpu, (CpuBus){ .read = bus_read, .write = bus_write, .ctx = NULL });
    cpu_attach_mem(&cpu, ram);

    /* Reset + IRQ vectors -> $0000. Program:
     *   LDA #$05 ; CLC ; ADC #$03 ; STA $0200 ; BRK
     */
    ram[0xFFFC] = 0x00; ram[0xFFFD] = 0x00;
    ram[0xFFFE] = 0x00; ram[0xFFFF] = 0x00;
    load(0x0000, (u8[]){ 0xA9, 0x05, 0x18, 0x69, 0x03, 0x8D, 0x00, 0x02, 0x00 }, 9);

    cpu_reset(&cpu);
    CHECK(cpu.pc == 0x0000, "reset PC");

    cpu_step(&cpu);   /* run one frame */
    /* The tiny program loops (LDA/CLC/ADC/STA/BRK->IRQ->$0000), so the STA
     * runs every iteration. A may be either 5 or 8 depending on where the
     * frame boundary lands, so assert the stable memory result. */
    CHECK(ram[0x0200] == 0x08, "STA stored result");

    /* The 8502 core pushes/pulls directly through PAGE_ONE. Relocating the
     * MMU stack page must move those accesses, not just bus reads/writes. */
    memset(&ram[0x0400], 0, 0x100);
    ram[0x01FC] = 0;
    ram[0x01FD] = 0;
    load(0x0200, (u8[]){ 0x20, 0x06, 0x02, 0x4C, 0x00, 0x02,
                        0xEE, 0x00, 0x05, 0x60 }, 10);
    ram[0xFFFC] = 0x00; ram[0xFFFD] = 0x02;
    cpu_set_stack_page(&ram[0x0400]);
    cpu_reset(&cpu);
    cpu_step_budget(&cpu, 100);
    CHECK(ram[0x0500] != 0, "subroutine returns from relocated stack");
    CHECK(ram[0x04FC] == 0x02 && ram[0x04FD] == 0x02 &&
          ram[0x01FC] == 0 && ram[0x01FD] == 0,
          "JSR writes relocated stack page, not physical page one");
    cpu_set_stack_page(&ram[0x0100]);

    /* C128 KERNAL IEC traps exchange bytes through BSOUR ($95) and the serial
     * input temporary ($A4), rather than assuming the accumulator is the
     * transport. */
    IecCallbacks iec = {
        .ctx = NULL,
        .force_slow_serial = true,
        .attention = test_attention,
        .send = test_send,
        .receive = test_receive,
        .take_status = test_take_status,
    };
    cpu_install_iec_traps(&ram[0xE000], &iec);

    ram[0x0A1C] = 0x40;
    ram[0x95] = 0x29;
    maincpu_regs.a = 0xEE;
    reg_pc = 0xE355;
    CHECK(traps_handler() == 0, "attention trap handled");
    CHECK(attention_byte == 0x29, "attention reads BSOUR");
    CHECK((ram[0x0A1C] & 0x40) == 0,
          "command-level IEC disables unsupported C128 burst mode");

    pending_status = 0x02;
    ram[0x90] = 0;
    ram[0x95] = 0x3F;
    reg_pc = 0xE355;
    CHECK(traps_handler() == 0, "status-producing attention trap handled");
    CHECK((ram[0x90] & 0x02) != 0, "attention propagates IEC error status");

    ram[0x95] = 0x42;
    maincpu_regs.a = 0xEE;
    reg_pc = 0xE38C;
    CHECK(traps_handler() == 0, "send trap handled");
    CHECK(send_byte == 0x42, "send reads BSOUR");

    ram[0x90] = 0;
    ram[0xA4] = 0;
    reg_pc = 0xE43E;
    CHECK(traps_handler() == 0, "receive trap handled");
    CHECK(maincpu_regs.a == 0x80, "receive sets A");
    CHECK(ram[0xA4] == 0x80, "receive stores C128 serial input temp");
    CHECK((ram[0x90] & 0x40) != 0, "receive sets EOI status");
    CHECK(MOS6510_REGS_GET_SIGN(&maincpu_regs) != 0,
          "receive updates negative flag");
    CHECK(!MOS6510_REGS_GET_ZERO(&maincpu_regs),
          "receive updates zero flag");

    receive_result = 0;
    ram[0x90] = 0;
    reg_pc = 0xE43E;
    CHECK(traps_handler() == 0, "empty receive trap handled");
    CHECK((ram[0x90] & 0x80) != 0, "empty receive reports device error");

    receive_result = -1;
    ram[0x90] = 0;
    reg_pc = 0xE43E;
    CHECK(traps_handler() == 0, "failed receive trap handled");
    CHECK((ram[0x90] & 0x02) != 0, "failed receive reports serial error");

    /* VICE x128 uses the C64 KERNAL's own T64 entry points and workspace
     * after switching personality. */
    u8 tape_byte = 0xA0;
    TapeCallbacks tape = {
        .ctx = &tape_byte,
        .next_header = test_tape_header,
        .read_byte = test_tape_byte,
    };
    cpu_set_c64_tape_traps(&ram[0xE000], &tape);
    ram[0xB2] = 0x00;
    ram[0xB3] = 0x03;
    reg_pc = 0xF72F;
    CHECK(traps_handler() == 0 && ram[0x0300] == 1 &&
          ram[0x0301] == 0x00 && ram[0x0302] == 0x20,
          "C64 T64 header trap writes the KERNAL tape buffer");
    ram[0xC1] = 0x00;
    ram[0xC2] = 0x20;
    ram[0xAE] = 0x02;
    ram[0xAF] = 0x20;
    ram[0x029F] = ram[0x02A0] = 0xFF;
    ram[0x0A09] = 0x55;
    maincpu_regs.x = 0x0E;
    reg_pc = 0xF8A1;
    CHECK(traps_handler() == 0 && ram[0x2000] == 0xA0 &&
          ram[0x2001] == 0xA1,
          "C64 T64 receive trap copies file bytes");
    CHECK(ram[0x029F] == 0 && ram[0x02A0] == 0 && ram[0x0A09] == 0x55,
          "C64 T64 trap uses C64 workspace rather than C128 workspace");
    cpu_set_c64_tape_traps(&ram[0xE000], NULL);

    if (failures == 0) { printf("test-cpu: OK\n"); return 0; }
    printf("test-cpu: %d failure(s)\n", failures);
    return 1;
}
