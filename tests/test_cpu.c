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

    if (failures == 0) { printf("test-cpu: OK\n"); return 0; }
    printf("test-cpu: %d failure(s)\n", failures);
    return 1;
}
