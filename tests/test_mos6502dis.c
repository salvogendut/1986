#include "mos6502dis.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", m); failures++; } } while (0)

static void expect(const u8 *bytes, int expected_size, const char *expected) {
    u8 mem[65536] = {0};
    char text[64];
    memcpy(mem + 0x2000, bytes, 3);
    int size = mos6502dis(mem, 0x2000, text, sizeof(text));
    CHECK(size == expected_size, "instruction size");
    CHECK(!strcmp(text, expected), expected);
}

int main(void) {
    expect((u8[]){0xA9, 0x42, 0}, 2, "LDA #$42");
    expect((u8[]){0xBD, 0x34, 0x12}, 3, "LDA $1234,X");
    expect((u8[]){0x6C, 0x00, 0xFF}, 3, "JMP ($FF00)");
    expect((u8[]){0xD0, 0xFC, 0}, 2, "BNE $1FFE");
    expect((u8[]){0x0A, 0, 0}, 1, "ASL A");
    expect((u8[]){0x02, 0, 0}, 1, "JAM");
    expect((u8[]){0x1C, 0x34, 0x12}, 3, "NOP $1234,X");
    expect((u8[]){0xA3, 0x44, 0}, 2, "LAX ($44,X)");
    if (!failures) puts("test-mos6502dis: OK");
    return failures ? 1 : 0;
}
