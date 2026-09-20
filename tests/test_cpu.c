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
static void test_attention(void *ctx, u8 byte) { (void)ctx; attention_byte = byte; }
static void test_send(void *ctx, u8 byte) { (void)ctx; send_byte = byte; }
static int test_receive(void *ctx, u8 *byte) {
    (void)ctx;
    *byte = 0x80;
    return receive_result;
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

    /* C128 KERNAL IEC traps exchange bytes through BSOUR ($95) and the serial
     * input temporary ($A4), rather than assuming the accumulator is the
     * transport. */
    IecCallbacks iec = {
        .ctx = NULL,
        .attention = test_attention,
        .send = test_send,
        .receive = test_receive,
    };
    cpu_install_iec_traps(&ram[0xE000], &iec);

    ram[0x95] = 0x29;
    maincpu_regs.a = 0xEE;
    reg_pc = 0xE355;
    CHECK(traps_handler() == 0, "attention trap handled");
    CHECK(attention_byte == 0x29, "attention reads BSOUR");

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

    if (failures == 0) { printf("test-cpu: OK\n"); return 0; }
    printf("test-cpu: %d failure(s)\n", failures);
    return 1;
}
