#include "cpu.h"
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

int main(void) {
    Cpu8502 cpu;
    memset(ram, 0, sizeof(ram));
    cpu_init(&cpu, (CpuBus){ .read = bus_read, .write = bus_write, .ctx = NULL });

    /* Reset vector -> $0000. Program:
     *   LDA #$05 ; CLC ; ADC #$03 ; STA $0200 ; BRK
     */
    ram[0xFFFC] = 0x00;
    ram[0xFFFD] = 0x00;
    load(0x0000, (u8[]){ 0xA9, 0x05, 0x18, 0x69, 0x03, 0x8D, 0x00, 0x02, 0x00 }, 9);

    cpu_reset(&cpu);
    CHECK(cpu.pc == 0x0000, "reset PC");

    /* Run 4 instructions (up to and including the STA). */
    for (int i = 0; i < 4; i++) cpu_step(&cpu);
    CHECK(cpu.a == 0x08, "LDA+ADC yields 0x08");
    CHECK(ram[0x0200] == 0x08, "STA stored result");

    /* Carry should be clear. */
    CHECK((cpu.p & P_C) == 0, "carry clear");

    /* Test a branch: BEQ self (-2). LDA #$00 sets Z so BEQ is taken. */
    memset(ram, 0, sizeof(ram));
    ram[0xFFFC] = 0x00; ram[0xFFFD] = 0x00;
    load(0x0000, (u8[]){ 0xA9, 0x00, 0xF0, 0xFE }, 4); /* LDA #0 ; BEQ -2 */
    cpu_reset(&cpu);
    cpu_step(&cpu);  /* LDA */
    cpu_step(&cpu);  /* BEQ taken -> PC wraps to 0x0002 */
    CHECK(cpu.pc == 0x0002, "BEQ taken");

    if (failures == 0) { printf("test-cpu: OK\n"); return 0; }
    printf("test-cpu: %d failure(s)\n", failures);
    return 1;
}
